// SPDX-License-Identifier: Apache-2.0
#include "fsim/semantic/vhdl_hir.hpp"
#include "fsim/support/vhdl_identifier.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>

namespace fsim::semantic::vhdl {

bool operator_designator(std::string_view name)
{
    if (name.size() >= 2U && name.front() == '"' && name.back() == '"') {
        name = name.substr(1U, name.size() - 2U);
    }
    if (name.empty()) {
        return false;
    }
    static constexpr std::string_view word_operators[] {
        "and", "or", "nand", "nor", "xor", "xnor", "not", "abs", "mod",
        "rem", "sll", "srl", "sla", "sra", "rol", "ror",
    };
    return std::ranges::none_of(name, [](const char c) {
               return std::isalnum(static_cast<unsigned char>(c)) != 0
                   || c == '_';
           })
        || std::ranges::any_of(word_operators,
            [&](const std::string_view word) {
                return support::equal_ignoring_ascii_case(name, word);
            });
}

std::optional<ForeignDirectBinding> foreign_direct_binding(
    const std::string_view value, const bool architecture)
{
    // The fields are separated by SPACE or NBSP characters; in the object
    // library path a backslash escapes the next SPACE or backslash
    // (IEEE 1076-2019 20.2.4.3).
    std::vector<std::string> words;
    std::string word;
    bool open { };
    for (std::size_t index { }; index < value.size(); ++index) {
        const auto character = value[index];
        if (character == '\\' && index + 1U < value.size()
            && (value[index + 1U] == ' ' || value[index + 1U] == '\\')) {
            word.push_back(value[++index]);
            open = true;
        } else if (character == ' ' || character == '\xa0') {
            if (open) {
                words.push_back(std::move(word));
                word.clear();
                open = false;
            }
        } else {
            word.push_back(character);
            open = true;
        }
    }
    if (open) {
        words.push_back(std::move(word));
    }
    // VHPIDIRECT and null are case sensitive.
    if (words.empty() || words.front() != "VHPIDIRECT"
        || words.size() != (architecture ? 4U : 3U)) {
        return std::nullopt;
    }
    ForeignDirectBinding binding;
    binding.library = std::move(words[1]);
    if (architecture) {
        if (words[2] != "null") {
            binding.elaboration = std::move(words[2]);
        }
        binding.execution = std::move(words[3]);
    } else {
        binding.execution = std::move(words[2]);
    }
    return binding;
}

std::optional<ForeignArchitectureBinding> foreign_architecture_binding(
    const Hir& hir, const Unit& architecture)
{
    if (architecture.kind != UnitKind::architecture) {
        return std::nullopt;
    }
    // The attribute is specified in the architecture's own declarative part
    // (IEEE 1076-2019 7.2), naming the architecture or `all`.
    for (const auto id : architecture.declarations) {
        const auto* const candidate = hir.find_declaration(id);
        if (candidate == nullptr
            || candidate->form != DeclarationForm::attribute_specification
            || !candidate->attribute || !candidate->attribute->value
            || !support::equal_ignoring_ascii_case(candidate->name, "foreign")
            || !support::equal_ignoring_ascii_case(
                candidate->attribute->entity_class, "architecture")
            || std::ranges::none_of(candidate->attribute->entity_names,
                [&](const Name& name) {
                    return support::equal_ignoring_ascii_case(
                               name.spelling, architecture.name)
                        || support::equal_ignoring_ascii_case(
                            name.spelling, "all");
                })) {
            continue;
        }
        const auto& expressions = hir.expressions();
        const auto value = std::ranges::find(
            expressions, *candidate->attribute->value, &Expression::id);
        if (value == expressions.end() || !value->decoded_string) {
            return std::nullopt;
        }
        const auto& text = *value->decoded_string;
        if (const auto direct = foreign_direct_binding(text, true)) {
            return ForeignArchitectureBinding { text, true, direct->library,
                architecture.name, direct->elaboration, direct->execution };
        }
        // Identifiers separated by one or more SPACE or NBSP characters,
        // with leading and trailing ones ignored (20.2.4.1).
        std::vector<std::string> words;
        std::string word;
        for (const auto character : text) {
            if (character == ' ' || character == '\xa0') {
                if (!word.empty()) {
                    words.push_back(std::move(word));
                    word.clear();
                }
            } else {
                word.push_back(character);
            }
        }
        if (!word.empty()) {
            words.push_back(std::move(word));
        }
        // The reserved words are case sensitive (20.2.4.2).
        if (words.size() == 3U && words.front() == "VHPI") {
            return ForeignArchitectureBinding { text, false, words[1],
                words[2], { }, { } };
        }
        return std::nullopt;
    }
    return std::nullopt;
}

SubtypeIndication constrained_element_subtype(
    const SubtypeIndication& owner,
    const std::string_view step,
    SubtypeIndication element)
{
    const auto same_step = [&](const std::string& candidate) {
        return support::equal_ignoring_ascii_case(candidate, step);
    };
    for (const auto& constraint : owner.element_constraints) {
        if (constraint.path.empty() || !same_step(constraint.path.front())) {
            continue;
        }
        if (constraint.path.size() == 1U) {
            std::erase_if(element.constraints,
                [](const RangeConstraint& range) {
                    return range.kind == RangeKind::array_index;
                });
            element.constraints.insert(element.constraints.begin(),
                constraint.constraints.begin(),
                constraint.constraints.end());
            element.unconstrained = false;
            element.executable_width.reset();
            continue;
        }
        ElementConstraint nested;
        nested.path.assign(
            std::next(constraint.path.begin()), constraint.path.end());
        nested.constraints = constraint.constraints;
        element.element_constraints.push_back(std::move(nested));
        element.executable_width.reset();
    }
    return element;
}

void Hir::note_mutation() noexcept { ++revision_; }

const std::vector<Unit>& Hir::units() const noexcept { return units_; }
const std::vector<Declaration>& Hir::declarations() const noexcept {
    return declarations_;
}
const std::vector<TypeDefinition>& Hir::types() const noexcept { return types_; }

const Declaration* Hir::find_declaration(const DeclarationId id) const noexcept
{
    return find_by_id(declarations_, id);
}

const TypeDefinition* Hir::find_type(const TypeId id) const noexcept
{
    return find_by_id(types_, id);
}
const std::vector<OverloadSet>& Hir::overload_sets() const noexcept {
    return overload_sets_;
}
const std::vector<Expression>& Hir::expressions() const noexcept {
    return expressions_;
}
const std::vector<Statement>& Hir::statements() const noexcept {
    return statements_;
}
const std::vector<Process>& Hir::processes() const noexcept {
    return processes_;
}
const std::vector<Instance>& Hir::instances() const noexcept {
    return instances_;
}

std::vector<Unit>& Hir::mutable_units() noexcept {
    note_mutation();
    return units_;
}
std::vector<Declaration>& Hir::mutable_declarations() noexcept {
    note_mutation();
    return declarations_;
}
std::vector<TypeDefinition>& Hir::mutable_types() noexcept {
    note_mutation();
    return types_;
}
std::vector<OverloadSet>& Hir::mutable_overload_sets() noexcept {
    note_mutation();
    return overload_sets_;
}
std::vector<Expression>& Hir::mutable_expressions() noexcept {
    note_mutation();
    return expressions_;
}
std::vector<Statement>& Hir::mutable_statements() noexcept {
    note_mutation();
    return statements_;
}
std::vector<Process>& Hir::mutable_processes() noexcept {
    note_mutation();
    return processes_;
}
std::vector<Instance>& Hir::mutable_instances() noexcept {
    note_mutation();
    return instances_;
}

namespace {

bool valid_instance_ids(
    const Model& semantics, const std::span<const Instance> instances)
{
    std::set<std::uint32_t> identities;
    return std::ranges::all_of(instances, [&](const Instance& instance) {
        return instance.id.valid()
            && instance.id.value() < semantics.instances().size()
            && identities.insert(instance.id.value()).second;
    });
}

} // namespace

bool top_level_hir_collections_well_formed(
    const Model& semantics, const TopLevelHirView view)
{
    if (!valid_instance_ids(semantics, view.instances)) {
        return false;
    }

    std::map<std::uint32_t, const Declaration*> declarations;
    for (const auto& declaration : view.declarations) {
        if (!declaration.id.valid()
            || declaration.id.value() >= semantics.declarations().size()
            || !declarations.emplace(
                    declaration.id.value(), &declaration).second) {
            return false;
        }
    }

    std::set<std::pair<std::uint32_t, std::string>> identities;
    std::set<std::uint32_t> members;
    for (const auto& overload : view.overload_sets) {
        if (!overload.scope.valid()
            || overload.scope.value() >= semantics.scopes().size()
            || overload.canonical_name.empty()
            || support::canonical_vhdl_spelling(overload.canonical_name)
                != overload.canonical_name
            || overload.declarations.empty()
            || !identities.emplace(
                    overload.scope.value(), overload.canonical_name).second) {
            return false;
        }
        std::set<std::uint32_t> local_members;
        for (const auto member : overload.declarations) {
            const auto declaration = member.valid()
                ? declarations.find(member.value())
                : declarations.end();
            if (declaration == declarations.end()
                || declaration->second->scope != overload.scope
                || support::canonical_vhdl_spelling(declaration->second->name)
                    != overload.canonical_name
                || !local_members.insert(member.value()).second
                || !members.insert(member.value()).second) {
                return false;
            }
        }
    }
    return true;
}

} // namespace fsim::semantic::vhdl
