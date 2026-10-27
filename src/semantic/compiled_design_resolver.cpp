// SPDX-License-Identifier: Apache-2.0
#include "fsim/semantic/compiled_design_resolver.hpp"
#include "fsim/semantic/systemverilog_type_descriptor.hpp"
#include "fsim/support/design_library.hpp"
#include "fsim/support/index_distance.hpp"
#include "fsim/support/vhdl_identifier.hpp"
#include "fsim/support/vhdl_standard_enumeration.hpp"
#include "fsim/support/vhdl_time.hpp"
#include "resolution_memo.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <ranges>
#include <set>
#include <span>
#include <tuple>
#include <unordered_set>

namespace fsim::semantic {
namespace {

using support::same_vhdl_identifier;

const bool semantic_profile_diagnostics_enabled
    = std::getenv("FSIM_PROFILE_PHASES") != nullptr;
// FSIM_VHDL_RESOLVE_TRACE traces VHDL overload resolution.
const bool vhdl_resolve_trace_enabled
    = std::getenv("FSIM_VHDL_RESOLVE_TRACE") != nullptr;

void profile_vhdl_package_member_lookup(const bool indexed_empty)
{
    struct LookupProfile {
        ~LookupProfile()
        {
            std::fprintf(stderr,
                "FSIM_VHDL_PACKAGE_MEMBER_LOOKUP total=%llu "
                "indexed_empty=%llu\n",
                static_cast<unsigned long long>(total.load()),
                static_cast<unsigned long long>(indexed_empty.load()));
        }

        std::atomic<std::uint64_t> total { };
        std::atomic<std::uint64_t> indexed_empty { };
    };
    static LookupProfile profile;
    profile.total.fetch_add(1U, std::memory_order_relaxed);
    if (indexed_empty) {
        profile.indexed_empty.fetch_add(1U, std::memory_order_relaxed);
    }
}

// An import written at or after a reference does not yet make its names
// visible there (IEEE 1800-2017 26.3); an unknown order (0) never hides.
bool import_follows_reference(
    const std::uint64_t import_order, const std::uint64_t reference_order)
{
    return reference_order != 0U && import_order != 0U
        && import_order >= reference_order;
}

using support::normalized_library;

const sv::Unit* source_package_for_compilation_unit(
    const CompiledDesign& design, const sv::Unit& compilation_unit)
{
    const auto owner_has_compilation_unit_scope
        = compilation_unit.kind == sv::UnitKind::compilation_unit
        || compilation_unit.kind == sv::UnitKind::module
        || compilation_unit.kind == sv::UnitKind::interface
        || compilation_unit.kind == sv::UnitKind::program;
    if (!owner_has_compilation_unit_scope
        || compilation_unit.compilation_unit_identity.empty()) {
        return nullptr;
    }
    if (const auto indexed
        = design.systemverilog_compilation_unit_package(compilation_unit)) {
        return *indexed;
    }
    const auto matches = [&](const sv::Unit& candidate) {
        return candidate.kind == sv::UnitKind::package
            && candidate.name.starts_with("fsim_unit_")
            && normalized_library(candidate.library)
                == normalized_library(compilation_unit.library)
            && candidate.compilation_unit_identity
                == compilation_unit.compilation_unit_identity;
    };
    const auto units = design.systemverilog_units();
    const auto found = std::ranges::find_if(units, matches);
    if (found == units.end()
        || std::ranges::count_if(units, matches) != 1U) {
        return nullptr;
    }
    return &*found;
}

bool simple_systemverilog_identifier(const std::string_view value)
{
    return !value.empty()
        && std::ranges::all_of(value, [](const char raw) {
            const auto character = static_cast<unsigned char>(raw);
            return std::isalnum(character) != 0 || raw == '_'
                || raw == '$' || raw == '\\';
        });
}

std::string_view vhdl_spelling(const vhdl::Name& name)
{
    return name.text();
}

class VhdlNameParts {
public:
    void push_back(const std::string_view part)
    {
        if (inline_size_ < inline_parts_.size()) {
            inline_parts_[inline_size_++] = part;
        } else {
            overflow_.push_back(part);
        }
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return inline_size_ == 0U;
    }

    [[nodiscard]] std::size_t size() const noexcept
    {
        return inline_size_ + overflow_.size();
    }

    [[nodiscard]] std::string_view front() const noexcept
    {
        return inline_parts_.front();
    }

    [[nodiscard]] std::string_view back() const noexcept
    {
        return overflow_.empty()
            ? inline_parts_[inline_size_ - 1U]
            : overflow_.back();
    }

    [[nodiscard]] std::string_view operator[](
        const std::size_t index) const noexcept
    {
        return index < inline_size_
            ? inline_parts_[index]
            : overflow_[index - inline_size_];
    }

private:
    std::array<std::string_view, 3U> inline_parts_ { };
    std::size_t inline_size_ { };
    std::vector<std::string_view> overflow_;
};

VhdlNameParts vhdl_name_parts(const std::string_view name)
{
    VhdlNameParts result;
    if (!support::visit_vhdl_selected_name(name,
            [&](const std::string_view part) { result.push_back(part); })) {
        return { };
    }
    return result;
}

std::string_view simple_vhdl_name(const std::string_view name)
{
    const auto separator = name.find_last_of(".:");
    return name.substr(separator == std::string_view::npos
            ? 0U
            : separator + 1U);
}

const Scope* find_scope(const CompiledDesign& design, const ScopeId id)
{
    if (!id.valid() || id.value() >= design.semantics.scopes().size()) {
        return nullptr;
    }
    const auto& candidate = design.semantics.scopes()[id.value()];
    return candidate.id == id ? &candidate : nullptr;
}


// The effective specialization's view of a type, expression or declaration
// when one is active and has it, else the design's.
std::optional<CompiledTypeView> find_type(const CompiledDesign& design,
    const SpecializedHirUnit* effective, const TypeId id)
{
    if (effective != nullptr) {
        if (const auto type = effective->find_type(id)) {
            return type;
        }
    }
    return design.find_type(id);
}

std::optional<CompiledExpressionView> find_expression(
    const CompiledDesign& design, const SpecializedHirUnit* effective,
    const ExpressionId id)
{
    if (effective != nullptr) {
        if (const auto expression = effective->find_expression(id)) {
            return expression;
        }
    }
    return design.find_expression(id);
}

std::optional<CompiledDeclarationView> find_declaration(
    const CompiledDesign& design, const SpecializedHirUnit* effective,
    const DeclarationId id)
{
    if (effective != nullptr) {
        if (const auto declaration = effective->find_declaration(id)) {
            return declaration;
        }
    }
    return design.find_declaration(id);
}

// The parameter and result type profile of a VHDL subprogram by base type,
// which decides homographs (IEEE 1076-2008 4.5.1, 12.3); nullopt when a type
// is unknown, which callers treat as a homograph of every declaration.
std::optional<std::string> vhdl_callable_homograph_key(
    const CompiledDesign& design, const SpecializedHirUnit* effective,
    const DeclarationId id)
{
    const auto type_key
        = [&](const std::optional<vhdl::SubtypeIndication>& subtype)
        -> std::string {
        if (!subtype) {
            return "-";
        }
        const auto type = effective != nullptr
            ? vhdl_root_type(*effective, subtype->type_mark.target)
            : vhdl_root_type(design, subtype->type_mark.target);
        if (type.valid()) {
            return "#" + std::to_string(type.value());
        }
        const std::string_view full { subtype->type_mark.spelling };
        const auto separator = full.find_last_of('.');
        auto spelling = std::string { separator == std::string_view::npos
                ? full
                : full.substr(separator + 1U) };
        std::ranges::transform(spelling, spelling.begin(),
            [](const unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
        return spelling.empty() ? "?" : "@" + spelling;
    };
    const auto declaration = find_declaration(design, effective, id);
    if (!declaration || declaration->vhdl == nullptr
        || !declaration->vhdl->callable) {
        return std::nullopt;
    }
    const auto& callable = *declaration->vhdl->callable;
    std::string key = callable.function ? "f" : "p";
    for (const auto formal_id : callable.formals) {
        const auto formal = find_declaration(design, effective, formal_id);
        if (!formal || formal->vhdl == nullptr) {
            return std::nullopt;
        }
        const auto formal_type = type_key(formal->vhdl->subtype);
        if (formal_type == "?" || formal_type == "-") {
            return std::nullopt;
        }
        key += "|" + formal_type;
    }
    if (callable.function) {
        const auto result_type = type_key(callable.return_type);
        if (result_type == "?" || result_type == "-") {
            return std::nullopt;
        }
        key += ">" + result_type;
    }
    return key;
}

// A `[package::]member` SystemVerilog reference and the SystemVerilog unit
// that owns its use scope (else the selected unit).
struct SystemVerilogScopedReference {
    std::string_view member;
    std::string_view package_name;
    std::optional<CompiledUnitView> owner;
};

std::optional<SystemVerilogScopedReference> systemverilog_scoped_reference(
    const CompiledDesign& design, const UnitId selected_unit,
    const std::string_view name, const ScopeId use_scope)
{
    const auto separator = name.rfind("::");
    const auto member = separator == std::string_view::npos
        ? name
        : name.substr(separator + 2U);
    const auto package_name = separator == std::string_view::npos
        ? std::string_view { }
        : name.substr(0U, separator);
    if (member.empty()
        || (separator != std::string_view::npos && package_name.empty())) {
        return std::nullopt;
    }
    auto owner = design.find_unit(selected_unit);
    if (const auto* scope = find_scope(design, use_scope)) {
        if (const auto scoped = design.find_unit(scope->unit);
            scoped && scoped->systemverilog != nullptr) {
            owner = scoped;
        }
    }
    if (!owner || owner->systemverilog == nullptr) {
        return std::nullopt;
    }
    return SystemVerilogScopedReference { member, package_name, owner };
}

template <typename Visitor>
void visit_systemverilog_declarations_in_scope(
    const CompiledDesign& design,
    const SpecializedHirUnit* const effective, const ScopeId scope,
    Visitor&& visitor)
{
    if (const auto indexed
        = design.systemverilog_declarations_in_scope(scope)) {
        for (const auto id : *indexed) {
            const auto declaration = find_declaration(
                design, effective, id);
            if (declaration && declaration->systemverilog != nullptr) {
                visitor(*declaration);
            }
        }
        return;
    }
    for (const auto& stored : design.systemverilog_hir.declarations()) {
        const auto declaration = find_declaration(
            design, effective, stored.id);
        if (declaration && declaration->systemverilog != nullptr
            && declaration->systemverilog->scope == scope) {
            visitor(*declaration);
        }
    }
}

// Declarations named `name` in one scope. The design's index is by compiled
// name, so a specialization that renames a declaration it replaces is
// scanned instead.
template <typename Visitor>
void visit_systemverilog_declarations_named_in_scope(
    const CompiledDesign& design,
    const SpecializedHirUnit* const effective, const ScopeId scope,
    const std::string_view name, Visitor&& visitor)
{
    const auto indexed
        = effective == nullptr
            || !effective->renames_systemverilog_declarations()
        ? design.systemverilog_declarations_named_in_scope(scope, name)
        : std::nullopt;
    if (indexed) {
        for (const auto id : *indexed) {
            const auto declaration = find_declaration(
                design, effective, id);
            if (declaration && declaration->systemverilog != nullptr) {
                visitor(*declaration);
            }
        }
        return;
    }
    visit_systemverilog_declarations_in_scope(design, effective, scope,
        [&](const CompiledDeclarationView& declaration) {
            if (declaration.systemverilog->name == name) {
                visitor(declaration);
            }
        });
}

template <typename Visitor>
void visit_vhdl_declarations_in_scope(const CompiledDesign& design,
    const SpecializedHirUnit* const effective, const ScopeId scope,
    const std::string_view name,
    Visitor&& visitor)
{
    if (effective == nullptr || effective->vhdl_declarations().empty()) {
        if (const auto indexed
            = design.vhdl_declarations_named_in_scope(scope, name)) {
            for (const auto id : *indexed) {
                const auto declaration = find_declaration(
                    design, effective, id);
                if (declaration && declaration->vhdl != nullptr) {
                    visitor(*declaration->vhdl);
                }
            }
            return;
        }
    }
    if (const auto indexed = design.vhdl_declarations_in_scope(scope)) {
        for (const auto id : *indexed) {
            const auto declaration = find_declaration(
                design, effective, id);
            if (declaration && declaration->vhdl != nullptr) {
                visitor(*declaration->vhdl);
            }
        }
        return;
    }
    for (const auto& stored : design.vhdl_hir.declarations()) {
        const auto declaration = find_declaration(
            design, effective, stored.id);
        if (declaration && declaration->vhdl != nullptr
            && declaration->vhdl->scope == scope) {
            visitor(*declaration->vhdl);
        }
    }
}

std::optional<TypeId> systemverilog_declared_type(
    const CompiledDesign& design, const SpecializedHirUnit* effective,
    const sv::Declaration& declaration)
{
    if (declaration.declared_type) {
        return declaration.declared_type;
    }
    // Decoded and specialized HIR can retain the canonical type-to-
    // declaration edge while omitting its mirrored declaration-to-type edge.
    // Treat that reverse edge as authoritative everywhere that identifies a
    // SystemVerilog named type. In particular, the parser may represent a type
    // designator with a synthetic value-namespace declaration whose form is
    // not typedef_declaration.
    if (effective != nullptr) {
        const auto types = effective->systemverilog_types();
        const auto found = std::ranges::find(
            types, declaration.id, &sv::TypeDefinition::declaration);
        if (found != types.end()) {
            return found->id;
        }
    }
    const auto& types = design.systemverilog_hir.types();
    const auto found = std::ranges::find(
        types, declaration.id, &sv::TypeDefinition::declaration);
    return found != types.end() ? std::optional { found->id }
                                : std::nullopt;
}

bool accepted(const CompiledDesign& design,
    const SpecializedHirUnit* effective, const DeclarationId id,
    const CompiledDeclarationPredicate& predicate)
{
    const auto declaration = find_declaration(design, effective, id);
    return declaration && (!predicate || predicate(*declaration));
}

template <typename Value>
void sort_unique(std::vector<Value>& values)
{
    std::ranges::sort(values);
    const auto duplicate = std::ranges::unique(values);
    values.erase(duplicate.begin(), duplicate.end());
}

CompiledResolutionStatus status_for(const std::size_t size)
{
    return size == 0U ? CompiledResolutionStatus::not_found
        : size == 1U ? CompiledResolutionStatus::unique
                     : CompiledResolutionStatus::ambiguous;
}

bool same_callable_resolution_identity(
    const CompiledVhdlCallableResolution& left,
    const CompiledVhdlCallableResolution& right)
{
    return left.body == right.body
        && left.package_instance == right.package_instance
        && left.generic_bindings == right.generic_bindings;
}

CompiledDeclarationResolution declaration_result(
    std::vector<DeclarationId> candidates)
{
    sort_unique(candidates);
    return { status_for(candidates.size()), std::move(candidates) };
}

struct SystemVerilogGenerateBranch {
    DeclarationId selection;
    DeclarationId branch;
};

struct SystemVerilogGenerateCandidate {
    std::vector<SystemVerilogGenerateBranch> branches;
};

std::optional<SystemVerilogGenerateCandidate>
systemverilog_generate_candidate(const CompiledDesign& design,
    const sv::Unit& unit, const DeclarationId candidate)
{
    // Only a declaration inside one of the unit's generate regions has a
    // path.
    if (unit.generates.empty()) {
        return std::nullopt;
    }
    std::vector<std::vector<const sv::GenerateRegion*>> paths;
    const std::vector<const sv::GenerateRegion*>* indexed_path { };
    std::size_t indexed_paths { };
    const auto in_unit = [&](const CompiledSystemVerilogGeneratePath& path) {
        const auto* outer = path.regions.front();
        return path.unit == unit.id
            && outer >= unit.generates.data()
            && outer < unit.generates.data() + unit.generates.size();
    };
    if (const auto indexed = design.systemverilog_generate_paths(candidate)) {
        for (const auto& path : *indexed) {
            if (in_unit(path)) {
                indexed_path = &path.regions;
                ++indexed_paths;
            }
        }
        if (indexed_paths != 1U) {
            return std::nullopt;
        }
    } else {
        std::vector<const sv::GenerateRegion*> path;
        const auto visit = [&](const auto& self,
                               const sv::GenerateRegion& region) -> void {
            path.push_back(&region);
            if (region.declaration == candidate
                || std::ranges::find(region.declarations, candidate)
                    != region.declarations.end()) {
                paths.push_back(path);
            }
            for (const auto& nested : region.nested) {
                self(self, nested);
            }
            path.pop_back();
        };
        for (const auto& region : unit.generates) {
            visit(visit, region);
        }
    }
    if (indexed_path == nullptr && paths.size() != 1U) {
        return std::nullopt;
    }

    SystemVerilogGenerateCandidate result;
    const auto& selected_path
        = indexed_path != nullptr ? *indexed_path : paths.front();
    for (std::size_t index = 0; index < selected_path.size(); ++index) {
        const auto& region = *selected_path[index];
        if (region.kind == sv::GenerateKind::conditional) {
            auto branch = region.declaration;
            if (index + 1U < selected_path.size()) {
                const auto& nested = *selected_path[index + 1U];
                if (nested.alternative_discriminator
                    != region.alternative_discriminator) {
                    branch = nested.declaration;
                }
            }
            result.branches.push_back(
                { region.declaration, branch });
        } else if (region.kind == sv::GenerateKind::selection) {
            if (index + 1U >= selected_path.size()) {
                return std::nullopt;
            }
            const auto& nested = *selected_path[index + 1U];
            const bool is_alternative = std::ranges::any_of(
                region.alternatives, [&](const sv::GenerateAlternative& alternative) {
                    return alternative.alternative_discriminator
                        == nested.alternative_discriminator;
                });
            if (!is_alternative) {
                return std::nullopt;
            }
            result.branches.push_back(
                { region.declaration, nested.declaration });
        }
    }
    return result;
}

bool systemverilog_generate_candidates_are_exclusive(
    const std::vector<SystemVerilogGenerateCandidate>& candidates)
{
    if (candidates.size() < 2U) {
        return false;
    }
    for (std::size_t left = 0; left < candidates.size(); ++left) {
        for (std::size_t right = left + 1U;
             right < candidates.size(); ++right) {
            const bool exclusive = std::ranges::any_of(
                candidates[left].branches,
                [&](const SystemVerilogGenerateBranch& left_branch) {
                    return std::ranges::any_of(
                        candidates[right].branches,
                        [&](const SystemVerilogGenerateBranch& right_branch) {
                            return left_branch.selection
                                    == right_branch.selection
                                && left_branch.branch
                                    != right_branch.branch;
                        });
                });
            if (!exclusive) {
                return false;
            }
        }
    }
    return true;
}

std::optional<std::vector<DeclarationId>> rank_systemverilog_constants(
    const CompiledDesign& design, const SpecializedHirUnit* const effective,
    const std::vector<DeclarationId>& candidates)
{
    using Form = sv::DeclarationForm;
    auto winning_rank = 0U;
    std::vector<DeclarationId> ranked;
    // A class-scope enum literal is also a class constant property; the
    // literal declaration denotes it (IEEE 1800-2017 8.23).
    const bool literal_present = std::ranges::any_of(candidates,
        [&](const DeclarationId id) {
            const auto declaration = find_declaration(design, effective, id);
            return declaration && declaration->systemverilog != nullptr
                && declaration->systemverilog->form
                    == Form::enumeration_literal;
        });
    for (const auto id : candidates) {
        const auto declaration = find_declaration(design, effective, id);
        if (!declaration || declaration->systemverilog == nullptr) {
            return std::nullopt;
        }
        const auto form = declaration->systemverilog->form;
        if (form != Form::parameter
            && form != Form::local_parameter
            && form != Form::enumeration_literal
            && !(form == Form::variable && literal_present)) {
            return std::nullopt;
        }
        const auto rank = form == Form::enumeration_literal ? 2U
            : form == Form::variable                     ? 0U
                                                         : 1U;
        if (rank > winning_rank) {
            ranked.clear();
            winning_rank = rank;
        }
        if (rank == winning_rank) {
            ranked.push_back(id);
        }
    }
    return ranked;
}

bool synthetic_enum_constant_duplicate(
    const CompiledDesign& design, const SpecializedHirUnit* const effective,
    const std::vector<DeclarationId>& candidates)
{
    if (candidates.size() != 2U) {
        return false;
    }
    const auto first = find_declaration(design, effective, candidates[0]);
    const auto second = find_declaration(design, effective, candidates[1]);
    if (!first || !second
        || first->systemverilog == nullptr
        || second->systemverilog == nullptr) {
        return false;
    }
    const auto is_pair = [](const sv::Declaration& enumeration,
                            const sv::Declaration& parameter) {
        // A class-scope literal also has a constant class property.
        return enumeration.form == sv::DeclarationForm::enumeration_literal
            && ((parameter.form == sv::DeclarationForm::local_parameter
                    && enumeration.source == parameter.source)
                || parameter.form == sv::DeclarationForm::variable)
            && enumeration.scope == parameter.scope
            && enumeration.name == parameter.name;
    };
    return is_pair(*first->systemverilog, *second->systemverilog)
        || is_pair(*second->systemverilog, *first->systemverilog);
}

sv::TypeReference retain_systemverilog_declarator(
    sv::TypeReference replacement, const sv::TypeReference& use,
    const bool packed_element = false)
{
    // `T [1:0]` with T a packed type bound to a type parameter is a packed
    // array of T: the use's dimensions come first (IEEE 1800-2017 7.4.1).
    const auto dimensions_of = [](const sv::TypeReference& type) {
        return type.packed_dimensions.empty() && type.packed_range
            ? std::vector<sv::PackedRange> { *type.packed_range }
            : type.packed_dimensions;
    };
    const auto extent = [](const sv::PackedRange& range)
        -> std::optional<std::uint64_t> {
        if (!range.left || !range.right) {
            return std::nullopt;
        }
        return static_cast<std::uint64_t>(*range.left >= *range.right
                       ? *range.left - *range.right
                       : *range.right - *range.left)
            + 1U;
    };
    std::vector<sv::PackedRange> composed;
    std::optional<std::uint64_t> composed_width { 1U };
    if (packed_element && use.packed_range && replacement.packed_range) {
        composed = dimensions_of(use);
        const auto element = dimensions_of(replacement);
        composed.insert(composed.end(), element.begin(), element.end());
        for (const auto& dimension : composed) {
            const auto size = extent(dimension);
            composed_width = size && composed_width
                ? std::optional { *composed_width * *size }
                : std::nullopt;
        }
    }
    if (!composed.empty() && composed_width && *composed_width != 0U) {
        const auto outer = extent(*use.packed_range).value_or(1U);
        replacement.packed_range = sv::PackedRange {
            static_cast<std::int64_t>(*composed_width) - 1, 0,
            std::nullopt, std::nullopt, true, use.packed_range->source };
        replacement.packed_dimensions = std::move(composed);
        if (replacement.executable_width) {
            replacement.executable_width
                = *replacement.executable_width * outer;
        }
    } else if (use.packed_range) {
        replacement.packed_range = use.packed_range;
    }
    replacement.signed_value
        = replacement.signed_value || use.signed_value;
    replacement.four_state = replacement.four_state || use.four_state;
    if (!use.container_form) {
        return replacement;
    }
    const auto nested_container = replacement.container_form
        ? std::optional { replacement }
        : std::nullopt;
    replacement.container_form = use.container_form;
    replacement.queue_maximum = use.queue_maximum;
    replacement.associative_index = use.associative_index;
    replacement.unpacked_dimensions = use.unpacked_dimensions;
    replacement.container_element_types = use.container_element_types;
    if (nested_container
        && replacement.container_element_types.empty()) {
        replacement.container_element_types.push_back(*nested_container);
    }
    return replacement;
}

const sv::Unit* systemverilog_package(const CompiledDesign& design,
    const std::string_view library, const std::string_view name)
{
    // Workspace analysis binds mapped-library imports to an explicit provider.
    // The qualifier is semantic metadata, preserving the provider's logical
    // identity even when the consumer is compiled into a different library.
    const auto separator = name.find("::");
    const auto provider_library = separator == std::string_view::npos
        ? library : name.substr(0U, separator);
    const auto provider_name = separator == std::string_view::npos
        ? name : name.substr(separator + 2U);
    const auto result = design.find_unit(
        UnitKind::systemverilog_package, provider_library, provider_name);
    return result && result->systemverilog != nullptr
        ? result->systemverilog
        : nullptr;
}

bool class_belongs_to_unit(const CompiledDesign& design,
    const sv::ClassDeclaration& declaration, const UnitId unit)
{
    const auto* scope = find_scope(design, declaration.scope);
    return scope != nullptr && scope->unit == unit;
}

bool scope_contains(const CompiledDesign& design,
    const ScopeId ancestor, ScopeId candidate)
{
    while (const auto* scope = find_scope(design, candidate)) {
        if (candidate == ancestor) {
            return true;
        }
        if (!scope->parent) {
            break;
        }
        candidate = *scope->parent;
    }
    return false;
}

std::size_t scope_depth(
    const CompiledDesign& design, ScopeId scope)
{
    std::size_t result { };
    while (const auto* record = find_scope(design, scope)) {
        ++result;
        if (!record->parent) {
            break;
        }
        scope = *record->parent;
    }
    return result;
}

bool systemverilog_package_references(const CompiledDesign& design,
    const sv::Unit& package, const std::string_view name)
{
    const auto& scopes = design.semantics.scopes();
    const auto in_package = [&](const ScopeId scope) {
        return scope.valid() && scope.value() < scopes.size()
            && scopes[scope.value()].unit == package.id;
    };
    const auto names = [&](const std::string_view text) {
        return text == name
            || (text.size() > name.size() && text.ends_with(name)
                && text.substr(0U, text.size() - name.size())
                       .ends_with("::"));
    };
    for (const auto& expression : design.systemverilog_hir.expressions()) {
        if (in_package(expression.scope)
            && (names(expression.text)
                || (expression.referenced_name
                    && names(expression.referenced_name->spelling)))) {
            return true;
        }
    }
    for (const auto& declaration : design.systemverilog_hir.declarations()) {
        // The frontend folds a referenced package constant to its value, so
        // a constant's use leaves no name behind; constants stay exported.
        if (declaration.name == name
            && (declaration.form == sv::DeclarationForm::parameter
                || declaration.form == sv::DeclarationForm::local_parameter)) {
            return true;
        }
        if (!in_package(declaration.scope)) {
            continue;
        }
        if (declaration.type && names(declaration.type->target.spelling)) {
            return true;
        }
        // Parameter initializers may live outside the package scope.
        const auto value = declaration.initializer
            ? design.find_expression(*declaration.initializer)
            : std::nullopt;
        if (value && value->systemverilog != nullptr
            && (names(value->systemverilog->text)
                || (value->systemverilog->referenced_name
                    && names(
                        value->systemverilog->referenced_name->spelling)))) {
            return true;
        }
    }
    return false;
}

template <typename AppendDirect>
void collect_systemverilog_package_exports(
    const CompiledDesign& design, const sv::Unit& package,
    const std::string_view member,
    std::unordered_set<std::uint32_t>& visiting,
    AppendDirect& append_direct)
{
    if (!visiting.insert(package.id.value()).second) {
        return;
    }
    if (append_direct(package)) {
        visiting.erase(package.id.value());
        return;
    }
    for (const auto& exported : package.exports) {
        if ((exported.package.spelling == "*" && exported.member)
            || (exported.member
                && exported.member->spelling != member)) {
            continue;
        }
        for (const auto& imported : package.imports) {
            if ((exported.package.spelling != "*"
                    && exported.package.spelling
                        != imported.package.spelling)
                || (imported.member
                    && imported.member->spelling != member)) {
                continue;
            }
            // A wildcard import only makes a name potentially visible; a
            // wildcard export carries it on only once the package refers
            // to it (IEEE 1800-2017 26.3, 26.6).
            if (!exported.member && !imported.member
                && !systemverilog_package_references(
                    design, package, member)) {
                continue;
            }
            const auto* source = systemverilog_package(design,
                package.library, imported.package.spelling);
            if (source != nullptr) {
                collect_systemverilog_package_exports(design, *source,
                    member, visiting, append_direct);
            }
        }
    }
    visiting.erase(package.id.value());
}

bool vhdl_callable_form(const vhdl::DeclarationForm form)
{
    using Form = vhdl::DeclarationForm;
    return form == Form::function || form == Form::procedure
        || form == Form::generic_function
        || form == Form::generic_procedure
        || form == Form::generic_function_instance
        || form == Form::generic_procedure_instance;
}

struct VhdlProfileNominalIdentity {
    std::optional<TypeId> type;
    vhdl::BuiltinTypeIdentity builtin { vhdl::BuiltinTypeIdentity::none };

    [[nodiscard]] bool known() const
    {
        return type.has_value()
            || builtin != vhdl::BuiltinTypeIdentity::none;
    }
};

// The protected type a protected type body completes: the declaration of
// the same name in the body's unit family (IEEE 1076-2008 5.6.3). A body
// does not declare a type of its own, so its type mark denotes that type.
std::optional<TypeId> vhdl_protected_type_of_body(
    const CompiledDesign& design, const vhdl::TypeDefinition& body)
{
    const auto unit_of = [&](const DeclarationId id) -> const vhdl::Unit* {
        const auto declaration = design.find_declaration(id);
        const auto* scope = declaration && declaration->vhdl != nullptr
            ? find_scope(design, declaration->vhdl->scope)
            : nullptr;
        const auto unit = scope != nullptr
            ? design.find_unit(scope->unit)
            : std::nullopt;
        return unit ? unit->vhdl : nullptr;
    };
    const auto* body_unit = unit_of(body.declaration);
    std::optional<TypeId> result;
    const auto& types = design.vhdl_hir.types();
    const auto positions = design.vhdl_type_positions_named(body.name);
    const auto count = positions ? positions->size() : types.size();
    for (std::size_t index { }; index < count; ++index) {
        const auto position = positions ? (*positions)[index] : index;
        if (position >= types.size()) {
            continue;
        }
        const auto& candidate = types[position];
        if (candidate.form != vhdl::TypeForm::protected_type
            || !same_vhdl_identifier(candidate.name, body.name)) {
            continue;
        }
        const auto* candidate_unit = unit_of(candidate.declaration);
        if (body_unit != nullptr && candidate_unit != nullptr
            && (!same_vhdl_identifier(candidate_unit->name, body_unit->name)
                || !same_vhdl_identifier(
                    normalized_library(candidate_unit->library),
                    normalized_library(body_unit->library)))) {
            continue;
        }
        if (result && *result != candidate.id) {
            return std::nullopt;
        }
        result = candidate.id;
    }
    return result;
}

// A protected type declaration and its body form one declarative region
// (IEEE 1076-2019 12.1): the body's scope also sees the declaration's
// items, such as its private variables. The scope of the declaration whose
// body has `scope`, when `scope` is a protected body's.
std::optional<ScopeId> vhdl_protected_declaration_scope(
    const CompiledDesign& design, const SpecializedHirUnit* effective,
    const ScopeId scope)
{
    const auto* record = find_scope(design, scope);
    if (record == nullptr || !record->parent || record->name.empty()) {
        return std::nullopt;
    }
    std::optional<DeclarationId> owner;
    visit_vhdl_declarations_in_scope(design, effective, *record->parent,
        record->name, [&](const vhdl::Declaration& declaration) {
            if (declaration.form == vhdl::DeclarationForm::type
                && declaration.nested_scope == scope) {
                owner = declaration.id;
            }
        });
    if (!owner) {
        return std::nullopt;
    }
    const auto& types = design.vhdl_hir.types();
    const auto body = std::ranges::find_if(types,
        [&](const vhdl::TypeDefinition& candidate) {
            return candidate.form == vhdl::TypeForm::protected_body
                && candidate.declaration == *owner;
        });
    const auto declared = body != types.end()
        ? vhdl_protected_type_of_body(design, *body)
        : std::nullopt;
    const auto type = declared
        ? find_type(design, effective, *declared)
        : std::nullopt;
    const auto declaration = type && type->vhdl != nullptr
        ? find_declaration(design, effective, type->vhdl->declaration)
        : std::nullopt;
    return declaration && declaration->vhdl != nullptr
        ? declaration->vhdl->nested_scope
        : std::nullopt;
}

VhdlProfileNominalIdentity vhdl_profile_nominal_identity(
    const CompiledDesign& design, const SpecializedHirUnit* effective,
    const vhdl::SubtypeIndication& subtype)
{
    auto type_id = subtype.type_mark.target;
    if (!type_id.valid()
        && subtype.builtin_type != vhdl::BuiltinTypeIdentity::none) {
        return { std::nullopt, subtype.builtin_type };
    }
    const auto unresolved_type = type_id;
    std::set<TypeId> visited;
    while (type_id.valid() && visited.insert(type_id).second) {
        const auto resolved = effective != nullptr
            ? effective->find_type(type_id)
            : std::nullopt;
        const auto type = resolved ? resolved : design.find_type(type_id);
        if (!type || type->vhdl == nullptr) {
            return { type_id, vhdl::BuiltinTypeIdentity::none };
        }
        const auto& definition = *type->vhdl;
        if (definition.form == vhdl::TypeForm::protected_body) {
            return { vhdl_protected_type_of_body(design, definition)
                         .value_or(type_id),
                vhdl::BuiltinTypeIdentity::none };
        }
        if (definition.form != vhdl::TypeForm::subtype
            && definition.form != vhdl::TypeForm::alias) {
            return { type_id, vhdl::BuiltinTypeIdentity::none };
        }
        if (definition.base.type_mark.target.valid()) {
            type_id = definition.base.type_mark.target;
            continue;
        }
        if (definition.base.builtin_type
            != vhdl::BuiltinTypeIdentity::none) {
            return { std::nullopt, definition.base.builtin_type };
        }
        return { type_id, vhdl::BuiltinTypeIdentity::none };
    }
    // A malformed/cyclic alias chain remains a known identity and must not
    // fall through to spelling-based equivalence.
    return unresolved_type.valid()
        ? VhdlProfileNominalIdentity {
              unresolved_type, vhdl::BuiltinTypeIdentity::none
          }
        : VhdlProfileNominalIdentity { };
}

bool same_vhdl_subtype(const CompiledDesign& design,
    const SpecializedHirUnit* effective,
    const vhdl::SubtypeIndication& left,
    const vhdl::SubtypeIndication& right)
{
    const auto left_identity = vhdl_profile_nominal_identity(
        design, effective, left);
    const auto right_identity = vhdl_profile_nominal_identity(
        design, effective, right);
    if (left_identity.type && right_identity.type) {
        return *left_identity.type == *right_identity.type;
    }
    if (left_identity.builtin != vhdl::BuiltinTypeIdentity::none
        || right_identity.builtin != vhdl::BuiltinTypeIdentity::none) {
        return left_identity.builtin != vhdl::BuiltinTypeIdentity::none
            && left_identity.builtin == right_identity.builtin;
    }
    if (left_identity.known() || right_identity.known()) {
        return false;
    }
    if (!left.type_mark.spelling.empty()
        && !right.type_mark.spelling.empty()) {
        return same_vhdl_identifier(
            simple_vhdl_name(left.type_mark.spelling),
            simple_vhdl_name(right.type_mark.spelling));
    }
    return left.domain == right.domain
        && left.unconstrained == right.unconstrained
        && left.unspecified_class == right.unspecified_class
        && left.executable_width == right.executable_width;
}

enum class VhdlProfileTypeClass : std::uint8_t {
    unknown,
    discrete,
    integer,
    physical,
    floating,
    array,
    access,
    file,
    protected_type,
    composite,
};

VhdlProfileTypeClass vhdl_profile_type_class(
    const CompiledDesign& design, const SpecializedHirUnit* effective,
    const vhdl::SubtypeIndication& subtype,
    std::set<TypeId>& active)
{
    if (subtype.type_mark.target.valid()
        && active.insert(subtype.type_mark.target).second) {
        const auto type = effective != nullptr
            ? effective->find_type(subtype.type_mark.target)
            : design.find_type(subtype.type_mark.target);
        const auto fallback = type ? type
                                   : design.find_type(
                                         subtype.type_mark.target);
        if (fallback && fallback->vhdl != nullptr) {
            using Form = vhdl::TypeForm;
            VhdlProfileTypeClass result {
                VhdlProfileTypeClass::unknown
            };
            switch (fallback->vhdl->form) {
            case Form::enumeration:
                result = VhdlProfileTypeClass::discrete;
                break;
            case Form::array:
                result = VhdlProfileTypeClass::array;
                break;
            case Form::record:
                result = VhdlProfileTypeClass::composite;
                break;
            case Form::access:
                result = VhdlProfileTypeClass::access;
                break;
            case Form::file:
                result = VhdlProfileTypeClass::file;
                break;
            case Form::protected_type:
            case Form::protected_body:
                result = VhdlProfileTypeClass::protected_type;
                break;
            case Form::physical:
                result = VhdlProfileTypeClass::physical;
                break;
            case Form::subtype:
            case Form::alias:
                result = vhdl_profile_type_class(
                    design, effective, fallback->vhdl->base, active);
                break;
            case Form::scalar:
            case Form::unresolved:
                break;
            }
            active.erase(subtype.type_mark.target);
            if (result != VhdlProfileTypeClass::unknown) {
                return result;
            }
        } else {
            active.erase(subtype.type_mark.target);
        }
    }
    const auto spelling = simple_vhdl_name(subtype.type_mark.spelling);
    if (support::is_vhdl_time_type_name(spelling)) {
        return VhdlProfileTypeClass::physical;
    }
    if (same_vhdl_identifier(spelling, "real")) {
        return VhdlProfileTypeClass::floating;
    }
    if (same_vhdl_identifier(spelling, "string")
        || spelling.ends_with("_vector")
        || same_vhdl_identifier(spelling, "signed")
        || same_vhdl_identifier(spelling, "unsigned")
        || same_vhdl_identifier(spelling, "ufixed")
        || same_vhdl_identifier(spelling, "sfixed")
        || same_vhdl_identifier(spelling, "unresolved_ufixed")
        || same_vhdl_identifier(spelling, "unresolved_sfixed")
        || same_vhdl_identifier(spelling, "float")
        || same_vhdl_identifier(spelling, "unresolved_float")
        || same_vhdl_identifier(spelling, "u_float")) {
        return VhdlProfileTypeClass::array;
    }
    switch (subtype.domain) {
    case vhdl::ValueDomain::integer:
        return VhdlProfileTypeClass::integer;
    case vhdl::ValueDomain::boolean:
    case vhdl::ValueDomain::bit2:
    case vhdl::ValueDomain::logic4:
    case vhdl::ValueDomain::logic9:
        return VhdlProfileTypeClass::discrete;
    case vhdl::ValueDomain::string:
        return VhdlProfileTypeClass::array;
    case vhdl::ValueDomain::unknown:
        break;
    }
    if (same_vhdl_identifier(spelling, "integer")
        || same_vhdl_identifier(spelling, "natural")
        || same_vhdl_identifier(spelling, "positive")) {
        return VhdlProfileTypeClass::integer;
    }
    return VhdlProfileTypeClass::unknown;
}

VhdlProfileTypeClass vhdl_profile_type_class(
    const CompiledDesign& design, const SpecializedHirUnit* effective,
    const vhdl::SubtypeIndication& subtype)
{
    std::set<TypeId> active;
    return vhdl_profile_type_class(
        design, effective, subtype, active);
}

bool vhdl_unspecified_profile_accepts(
    const vhdl::UnspecifiedTypeClass formal,
    const VhdlProfileTypeClass actual)
{
    if (formal == vhdl::UnspecifiedTypeClass::none) {
        return true;
    }
    // A restricted unspecified type is not a wildcard.  If its actual class
    // cannot be recovered, conformance has not been established; accepting
    // it here also disagrees with specialization-time inference and lets
    // incompatible overloads survive into lowering.
    if (actual == VhdlProfileTypeClass::unknown) {
        return false;
    }
    using Formal = vhdl::UnspecifiedTypeClass;
    using Actual = VhdlProfileTypeClass;
    switch (formal) {
    case Formal::private_type:
        return actual != Actual::file
            && actual != Actual::protected_type;
    case Formal::scalar:
        return actual == Actual::discrete
            || actual == Actual::integer
            || actual == Actual::physical
            || actual == Actual::floating;
    case Formal::discrete:
        return actual == Actual::discrete
            || actual == Actual::integer;
    case Formal::integer:
        return actual == Actual::integer;
    case Formal::physical:
        return actual == Actual::physical;
    case Formal::floating:
        return actual == Actual::floating;
    case Formal::array:
        return actual == Actual::array;
    case Formal::access:
        return actual == Actual::access;
    case Formal::file:
        return actual == Actual::file;
    case Formal::none:
        return true;
    }
    return false;
}

std::optional<const vhdl::Unit*> primary_vhdl_unit(
    const CompiledDesign& design, const vhdl::Unit& secondary)
{
    return design.vhdl_primary_unit(secondary);
}

std::vector<const vhdl::Unit*> vhdl_lookup_units(
    const CompiledDesign& design, const UnitId selected_unit,
    ScopeId use_scope)
{
    std::vector<const vhdl::Unit*> result;
    const auto append = [&](const vhdl::Unit* unit) {
        if (unit != nullptr
            && std::ranges::find(result, unit) == result.end()) {
            result.push_back(unit);
        }
    };
    if (const auto* scope_units = design.vhdl_scope_units(use_scope)) {
        result = *scope_units;
        use_scope = ScopeId { };
    }
    for (std::size_t depth { };
         use_scope.valid() && depth <= design.semantics.scopes().size();
         ++depth) {
        const auto* scope = find_scope(design, use_scope);
        if (scope == nullptr) {
            break;
        }
        const auto owner = design.find_unit(scope->unit);
        append(owner && owner->vhdl != nullptr ? owner->vhdl : nullptr);
        if (!scope->parent) {
            break;
        }
        use_scope = *scope->parent;
    }
    const auto selected = design.find_unit(selected_unit);
    append(selected && selected->vhdl != nullptr
            ? selected->vhdl
            : nullptr);

    // Secondary units inherit visibility from their primary unit. Keep both
    // owners in the lookup set because a projected specialization can retain
    // a process scope owned by one side of the pair while its context clauses
    // were attached to the other side during compilation.
    const auto direct_count = result.size();
    for (std::size_t index { }; index < direct_count; ++index) {
        const auto* unit = result[index];
        if (const auto primary = primary_vhdl_unit(design, *unit)) {
            append(*primary);
        }
    }
    return result;
}

std::vector<CompiledVhdlImport> collect_vhdl_linked_imports(
    const CompiledDesign& design,
    const std::vector<const vhdl::Unit*>& lookup_units)
{
    std::vector<CompiledVhdlImport> result;
    for (const auto* unit : lookup_units) {
        for (const auto& reference : design.references()) {
            if (reference.kind != CompiledReferenceKind::package
                || reference.owner != unit->id) {
                continue;
            }
            for (const auto& item : unit->context) {
                if (item.kind != vhdl::ContextKind::use_clause) {
                    continue;
                }
                for (const auto& selected : item.selected_names) {
                    const auto parts = vhdl_name_parts(
                        vhdl_spelling(selected));
                    if (parts.size() != 2U && parts.size() != 3U) {
                        continue;
                    }
                    const auto library_prefix
                        = vhdl::use_name_has_library_prefix(
                            *unit, parts.size(), parts.front());
                    const auto specified_library = library_prefix
                        ? std::string_view { parts.front() }
                        : normalized_library(unit->library);
                    const auto library = same_vhdl_identifier(
                            specified_library, "work")
                        ? normalized_library(unit->library)
                        : specified_library;
                    const auto package = library_prefix
                        ? std::string_view { parts[1] }
                        : std::string_view { parts.front() };
                    if (!same_vhdl_identifier(
                            library, reference.library)
                        || !same_vhdl_identifier(
                            package, reference.name)) {
                        continue;
                    }
                    const auto duplicate = std::ranges::any_of(
                        result, [&](const CompiledVhdlImport& imported) {
                            return same_vhdl_identifier(
                                       imported.library,
                                       reference.library)
                                && same_vhdl_identifier(
                                    imported.package, reference.name)
                                && same_vhdl_identifier(
                                    imported.member, parts.back());
                        });
                    if (!duplicate) {
                        result.push_back({ reference.library,
                            reference.name, std::string { parts.back() } });
                    }
                }
            }
        }
    }
    return result;
}

template <typename Visitor>
bool visit_vhdl_linked_imports(const CompiledDesign& design,
    const std::vector<const vhdl::Unit*>& lookup_units,
    Visitor&& visitor)
{
    bool indexed = true;
    for (const auto* unit : lookup_units) {
        const auto imports = design.vhdl_linked_imports(unit->id);
        if (!imports) {
            indexed = false;
            break;
        }
        for (const auto& imported : *imports) {
            if (visitor(imported)) {
                return true;
            }
        }
    }
    if (indexed) {
        return false;
    }
    for (const auto& imported : collect_vhdl_linked_imports(
             design, lookup_units)) {
        if (visitor(imported)) {
            return true;
        }
    }
    return false;
}

std::string_view effective_vhdl_library(const std::string_view requested,
    const std::string_view owner)
{
    return same_vhdl_identifier(requested, "work")
        ? normalized_library(owner)
        : normalized_library(requested);
}

// The library named by a context reference `[library.]context`, relative
// to the referencing unit's library (IEEE 1076-2019 13.4).
std::string_view context_reference_library(
    const VhdlNameParts& parts, const std::string_view owner_library)
{
    return parts.size() >= 2U
        ? effective_vhdl_library(parts[parts.size() - 2U], owner_library)
        : normalized_library(owner_library);
}

bool is_named_vhdl_context(const vhdl::Unit& candidate,
    const std::string_view library, const std::string_view name)
{
    return candidate.kind == vhdl::UnitKind::context
        && same_vhdl_identifier(normalized_library(candidate.library), library)
        && vhdl::unit_name_matches(candidate, name);
}

// Calls `visitor(context)` for each context declaration a context reference
// names; stops and returns true when the visitor does.
template <typename Visitor>
bool visit_referenced_vhdl_contexts(const CompiledDesign& design,
    const VhdlNameParts& parts, const std::string_view owner_library,
    Visitor&& visitor)
{
    if (parts.empty()) {
        return false;
    }
    const auto library = context_reference_library(parts, owner_library);
    for (const auto& context : design.vhdl_units()) {
        if (is_named_vhdl_context(context, library, parts.back())
            && visitor(context)) {
            return true;
        }
    }
    return false;
}

bool standard_package_exports(const vhdl::Unit& package,
    const std::string_view member)
{
    return std::ranges::any_of(package.standard_package_declarations,
        [&](const std::string_view candidate) {
            return same_vhdl_identifier(candidate, member);
        });
}

bool builtin_standard_discrete_type(const std::string_view name)
{
    // Match the implicit STD.STANDARD scalar types completed below. The
    // caller also requires unfiltered scoped lookup to find no declaration,
    // so a same-spelled user type or object keeps its own identity.
    constexpr auto names = std::to_array<std::string_view>({
        "boolean", "bit", "character", "integer", "natural", "positive",
    });
    return support::find_vhdl_standard_enumeration(name) != nullptr
        || std::ranges::any_of(names,
            [&](const std::string_view candidate) {
                return same_vhdl_identifier(candidate, name);
            });
}

CompiledVhdlStandardScalarIdentity builtin_standard_scalar_type_identity(
    const std::string_view spelling,
    const vhdl::PredefinedEnvironment& environment,
    const bool allow_unqualified)
{
    constexpr auto standard_profile
        = std::string_view { "ieee-1076-standard:" };
    if (spelling.empty() || spelling.find('@') != std::string_view::npos
        || !environment.identity.starts_with(standard_profile)) {
        return CompiledVhdlStandardScalarIdentity::none;
    }
    const auto listed = [&](const std::span<const std::string> names,
                            const std::string_view name) {
        return std::ranges::any_of(names,
            [&](const std::string& candidate) {
                return same_vhdl_identifier(candidate, name);
            });
    };
    auto simple = spelling;
    constexpr auto qualified_prefix
        = std::string_view { "std.standard." };
    if (spelling.size() >= qualified_prefix.size()
        && same_vhdl_identifier(
            spelling.substr(0U, qualified_prefix.size()), qualified_prefix)) {
        simple = spelling.substr(qualified_prefix.size());
        if (simple.empty() || simple.find_first_of(":.")
                != std::string_view::npos) {
            return CompiledVhdlStandardScalarIdentity::none;
        }
    } else if (spelling.find_first_of(":.") != std::string_view::npos
        || !allow_unqualified
        || !listed(environment.implicit_packages, "std.standard.all")) {
        return CompiledVhdlStandardScalarIdentity::none;
    }
    if (!listed(environment.declarations, simple)) {
        return CompiledVhdlStandardScalarIdentity::none;
    }
    if (same_vhdl_identifier(simple, "bit")) {
        return CompiledVhdlStandardScalarIdentity::bit;
    }
    if (same_vhdl_identifier(simple, "boolean")) {
        return CompiledVhdlStandardScalarIdentity::boolean;
    }
    if (same_vhdl_identifier(simple, "character")) {
        return CompiledVhdlStandardScalarIdentity::character;
    }
    if (same_vhdl_identifier(simple, "file_open_kind")) {
        return CompiledVhdlStandardScalarIdentity::file_open_kind;
    }
    if (same_vhdl_identifier(simple, "file_open_status")) {
        return CompiledVhdlStandardScalarIdentity::file_open_status;
    }
    if (same_vhdl_identifier(simple, "range_direction")) {
        return CompiledVhdlStandardScalarIdentity::range_direction;
    }
    if (same_vhdl_identifier(simple, "integer")
        || same_vhdl_identifier(simple, "natural")
        || same_vhdl_identifier(simple, "positive")) {
        return CompiledVhdlStandardScalarIdentity::integer;
    }
    if (same_vhdl_identifier(simple, "real")) {
        return CompiledVhdlStandardScalarIdentity::real;
    }
    if (same_vhdl_identifier(simple, "severity_level")) {
        return CompiledVhdlStandardScalarIdentity::severity_level;
    }
    if (support::is_vhdl_time_type_name(simple)) {
        return CompiledVhdlStandardScalarIdentity::time;
    }
    return CompiledVhdlStandardScalarIdentity::none;
}

bool builtin_standard_package_exports(const std::string_view library,
    const std::string_view package, const std::string_view member)
{
    if (same_vhdl_identifier(library, "std")
        && same_vhdl_identifier(package, "textio")) {
        // Keep the fallback in step with the compiler-owned std.textio
        // package declaration in third_party/ieee-1076-2019/std/textio.vhdl.
        constexpr auto members = std::to_array<std::string_view>({
            "line", "line_vector", "text", "side", "right", "left",
            "width", "justify", "input", "output", "readline", "read",
            "sread", "string_read", "bread", "binary_read", "oread",
            "octal_read", "hread", "hex_read", "writeline", "tee",
            "write", "swrite", "string_write", "bwrite", "binary_write",
            "owrite", "octal_write", "hwrite", "hex_write",
        });
        return std::ranges::any_of(members,
            [&](const std::string_view candidate) {
                return same_vhdl_identifier(candidate, member);
            });
    }
    if (same_vhdl_identifier(library, "ieee")
        && same_vhdl_identifier(package, "std_logic_1164")) {
        constexpr auto members = std::to_array<std::string_view>({
            "std_logic", "std_ulogic", "std_logic_vector",
            "std_ulogic_vector", "not", "and", "or", "nand",
            "nor", "xor", "xnor",
        });
        return std::ranges::any_of(members,
            [&](const std::string_view candidate) {
                return same_vhdl_identifier(candidate, member);
            });
    }
    if (same_vhdl_identifier(library, "ieee")
        && same_vhdl_identifier(package, "numeric_std")
        && (same_vhdl_identifier(member, "unresolved_signed")
            || same_vhdl_identifier(member, "unresolved_unsigned"))) {
        // NUMERIC_STD's TO_SIGNED/TO_UNSIGNED results use these exact
        // package types (IEEE 1076-2019, numeric_std D.3-D.4). They are not
        // members of NUMERIC_BIT.
        return true;
    }
    if (same_vhdl_identifier(library, "ieee")
        && (same_vhdl_identifier(package, "numeric_std")
            || same_vhdl_identifier(package, "numeric_bit"))) {
        // Standard-library package projections are optional compiled inputs.
        // Preserve the visibility established by an IEEE use clause even
        // when no owning package unit was retained in this object bundle.
        constexpr auto members = std::to_array<std::string_view>({
            "signed", "unsigned", "abs", "+", "-", "*", "/",
            "mod", "rem", "**", "=", "/=", "<", "<=", ">", ">=",
            "and", "or", "nand", "nor", "xor", "xnor",
            "sll", "srl", "rol", "ror", "shift_left", "shift_right",
            "rotate_left", "rotate_right", "resize", "to_integer",
            "to_unsigned", "to_signed", "to_01", "find_leftmost",
            "find_rightmost", "minimum", "maximum",
        });
        return std::ranges::any_of(members,
            [&](const std::string_view candidate) {
                return same_vhdl_identifier(candidate, member);
            });
    }
    if (same_vhdl_identifier(library, "ieee")
        && (same_vhdl_identifier(package, "fixed_pkg")
            || same_vhdl_identifier(package, "float_pkg"))) {
        // The compiler provides the fixed- and floating-point packages
        // (IEEE 1076-2008 G.3, G.4); their vector types stay visible
        // through an IEEE use clause without retained declarations.
        constexpr auto fixed_members = std::to_array<std::string_view>({
            "ufixed", "sfixed", "unresolved_ufixed", "unresolved_sfixed",
            "u_ufixed", "u_sfixed",
        });
        constexpr auto float_members = std::to_array<std::string_view>({
            "float", "unresolved_float", "u_float", "float32", "float64",
            "float128",
        });
        const auto& members = same_vhdl_identifier(package, "fixed_pkg")
            ? std::span<const std::string_view> { fixed_members }
            : std::span<const std::string_view> { float_members };
        return std::ranges::any_of(members,
            [&](const std::string_view candidate) {
                return same_vhdl_identifier(candidate, member);
            });
    }
    if (same_vhdl_identifier(library, "ieee")
        && (same_vhdl_identifier(package, "std_logic_arith")
            || same_vhdl_identifier(package, "std_logic_signed")
            || same_vhdl_identifier(package, "std_logic_unsigned")
            || same_vhdl_identifier(package, "std_logic_misc"))) {
        // The Synopsys compatibility packages are compiler-governed inputs
        // just like numeric_std.  Some object bundles intentionally omit the
        // owning package declarations, so retain the visibility established
        // by their IEEE use clauses in the compiled resolver itself.
        constexpr auto members = std::to_array<std::string_view>({
            "conv_integer", "conv_signed", "conv_unsigned",
            "conv_std_logic_vector", "ext", "sxt", "shl", "shr",
            "and_reduce", "nand_reduce", "or_reduce", "nor_reduce",
            "xor_reduce", "xnor_reduce", "+", "-", "*", "/", "mod",
            "rem", "=", "/=", "<", "<=", ">", ">=",
        });
        return std::ranges::any_of(members,
            [&](const std::string_view candidate) {
                return same_vhdl_identifier(candidate, member);
            });
    }
    if (!same_vhdl_identifier(library, "std")) {
        return false;
    }
    if (same_vhdl_identifier(package, "env")) {
        constexpr auto members = std::to_array<std::string_view>({
            "stop", "finish", "resolution_limit",
            "dayofweek", "time_record", "localtime", "gmtime", "epoch",
            "time_to_seconds", "seconds_to_time", "to_string", "getenv",
            "vhdl_version", "tool_type", "tool_vendor", "tool_name",
            "tool_edition", "tool_version", "call_path_element",
            "call_path_vector", "call_path_vector_ptr", "get_call_path",
            "file_name", "file_path", "file_line", "directory_items",
            "directory", "dir_open_status", "dir_create_status",
            "dir_delete_status", "file_delete_status", "dir_open",
            "dir_close", "dir_itemexists", "dir_itemisdir",
            "dir_itemisfile", "dir_workingdir", "dir_createdir",
            "dir_deletedir", "dir_deletefile", "dir_separator",
            "pslassertfailed", "psliscovered", "getpslcoverassert",
            "pslisassertcovered", "setpslcoverassert", "clearpslstate",
            "isvhdlassertfailed", "getvhdlassertcount",
            "clearvhdlassert", "setvhdlassertenable",
            "getvhdlassertenable", "setvhdlassertformat",
            "getvhdlassertformat", "setvhdlreadseverity",
            "getvhdlreadseverity",
        });
        return std::ranges::any_of(members,
            [&](const std::string_view candidate) {
                return same_vhdl_identifier(candidate, member);
            });
    }
    if (same_vhdl_identifier(package, "reflection")) {
        constexpr auto members = std::to_array<std::string_view>({
            "type_class", "value_class",
            "index", "natural_index", "positive_index", "dimension",
            "value_mirror", "subtype_mirror",
            "enumeration_value_mirror", "enumeration_subtype_mirror",
            "integer_value_mirror", "integer_subtype_mirror",
            "floating_value_mirror", "floating_subtype_mirror",
            "physical_value_mirror", "physical_subtype_mirror",
            "record_value_mirror", "record_subtype_mirror",
            "array_value_mirror", "array_subtype_mirror",
            "access_value_mirror", "access_subtype_mirror",
            "file_value_mirror", "file_subtype_mirror",
            "protected_value_mirror", "protected_subtype_mirror",
        });
        return std::ranges::any_of(members,
            [&](const std::string_view candidate) {
                return same_vhdl_identifier(candidate, member);
            });
    }
    return false;
}

// Whether `scope` is the block of a loop generate whose genvar is `name`;
// the genvar hides outer declarations of that name (IEEE 1800-2017 27.4).
bool systemverilog_genvar_scope(
    const CompiledDesign& design, const ScopeId scope,
    const std::string_view name)
{
    const auto* record = find_scope(design, scope);
    if (record == nullptr || !record->parent) {
        return false;
    }
    if (const auto indexed = design.systemverilog_scope_genvars(scope)) {
        return std::ranges::any_of(*indexed,
            [&](const CompiledSystemVerilogScopeGenvar& genvar) {
                return genvar.unit == record->unit
                    && genvar.iterator == name;
            });
    }
    const auto unit = design.find_unit(record->unit);
    if (!unit || unit->systemverilog == nullptr
        || unit->systemverilog->generates.empty()) {
        return false;
    }
    const auto visit = [&](const auto& self,
                           const std::vector<sv::GenerateRegion>& regions)
        -> bool {
        for (const auto& region : regions) {
            if (region.scope == scope) {
                return region.kind == sv::GenerateKind::iterative
                    && region.iterator == name;
            }
            // A process of the block has its own scope outside the block's
            // scope chain.
            if (region.kind == sv::GenerateKind::iterative
                && region.iterator == name
                && std::ranges::any_of(region.processes,
                    [&](const ProcessId process) {
                        const auto& processes
                            = design.systemverilog_hir.processes();
                        const auto found = std::ranges::find(
                            processes, process, &sv::Process::id);
                        return found != processes.end()
                            && found->scope == scope;
                    })) {
                return true;
            }
            if (self(self, region.nested)) {
                return true;
            }
        }
        return false;
    };
    return visit(visit, unit->systemverilog->generates);
}


// Reads a generic's actual from the nearest of `active` that names it. The
// nearest frame owns the name even when it carries a declaration or type
// binding rather than an expression; an older frame is not exposed through
// that shadowing boundary.
SpecializedHirIntegralBinding vhdl_frames_integral_binding(
    const std::span<const CompiledBindingFrame> active)
{
    return [active](const DeclarationId formal) -> std::optional<ExpressionId> {
        for (auto frame = active.rbegin(); frame != active.rend(); ++frame) {
            const auto binding = std::ranges::find(
                *frame, formal, &CompiledActualBinding::formal);
            if (binding != frame->end()) {
                return binding->expression;
            }
        }
        return std::nullopt;
    };
}

} // namespace

// Whether a genvar between `use_scope` and the scope of a declaration
// hides that declaration.
bool systemverilog_genvar_hides(
    const CompiledDesign& design, ScopeId use_scope,
    const ScopeId declaration_scope, const std::string_view name)
{
    std::size_t depth { };
    while (use_scope.valid() && use_scope != declaration_scope
        && depth++ <= design.semantics.scopes().size()) {
        if (systemverilog_genvar_scope(design, use_scope, name)) {
            return true;
        }
        const auto* record = find_scope(design, use_scope);
        if (record == nullptr || !record->parent) {
            break;
        }
        use_scope = *record->parent;
    }
    return false;
}

std::optional<DeclarationId>
CompiledDeclarationResolution::unique() const noexcept
{
    return status == CompiledResolutionStatus::unique
            && candidates.size() == 1U
        ? std::optional<DeclarationId> { candidates.front() }
        : std::nullopt;
}

const sv::LetDeclaration*
CompiledSystemVerilogLetResolution::unique() const noexcept
{
    return status == CompiledResolutionStatus::unique
            && candidates.size() == 1U
        ? candidates.front()
        : nullptr;
}

std::optional<CompiledVhdlPackageMember>
CompiledVhdlPackageResolution::unique() const
{
    return status == CompiledResolutionStatus::unique
            && candidates.size() == 1U
        ? std::optional<CompiledVhdlPackageMember> { candidates.front() }
        : std::nullopt;
}

std::optional<CompiledVhdlCallableResolution>
CompiledVhdlCallableResolutionResult::unique() const
{
    return status == CompiledResolutionStatus::unique
            && candidates.size() == 1U
        ? std::optional<CompiledVhdlCallableResolution> {
              candidates.front() }
        : std::nullopt;
}

const sv::ClassDeclaration*
CompiledSystemVerilogClassResolution::unique() const noexcept
{
    return status == CompiledResolutionStatus::unique
            && candidates.size() == 1U
        ? candidates.front()
        : nullptr;
}

const sv::ClassProperty*
CompiledSystemVerilogClassPropertyResolution::unique() const noexcept
{
    return status == CompiledResolutionStatus::unique
            && candidates.size() == 1U
        ? candidates.front()
        : nullptr;
}

const sv::ClassMethod*
CompiledSystemVerilogClassMethodResolution::unique() const noexcept
{
    return status == CompiledResolutionStatus::unique
            && candidates.size() == 1U
        ? candidates.front()
        : nullptr;
}

void normalize_vhdl_callable_resolutions(
    std::vector<CompiledVhdlCallableResolution>& candidates)
{
    std::ranges::sort(candidates, {},
        [](const CompiledVhdlCallableResolution& candidate) {
            return std::tuple { candidate.body,
                candidate.package_instance, candidate.key };
        });
    std::vector<CompiledVhdlCallableResolution> normalized;
    normalized.reserve(candidates.size());
    for (auto& candidate : candidates) {
        if (std::ranges::none_of(normalized,
                [&](const auto& retained) {
                    return same_callable_resolution_identity(
                        retained, candidate);
                })) {
            normalized.push_back(std::move(candidate));
        }
    }
    candidates = std::move(normalized);
}

CompiledDesignResolver::CompiledDesignResolver(
    const CompiledDesign& design, const UnitId selected_unit,
    const SpecializedHirUnit* const effective,
    const std::span<const CompiledBindingFrame> binding_frames)
    : design_ { &design }
    , effective_ { effective }
    , selected_unit_ { selected_unit }
    , binding_frames_ { binding_frames }
{
}

CompiledDesignResolver::CompiledDesignResolver(
    const SpecializedHirUnit& effective,
    const std::span<const CompiledBindingFrame> binding_frames)
    : CompiledDesignResolver(effective.design(), effective.unit(),
          &effective, binding_frames)
{
}

std::optional<DeclarationId>
CompiledDesignResolver::actual_declaration(
    const DeclarationId formal) const
{
    auto current = formal;
    std::set<DeclarationId> visited;
    while (visited.insert(current).second) {
        // An unpacked array parameter holds its own container of the
        // actual's element values; it never aliases the actual (IEEE
        // 1800-2023 6.20.2).
        if (const auto view = find_declaration(*design_, effective_, current);
            view && view->systemverilog != nullptr
            && (view->systemverilog->array_parameter
                || view->systemverilog->array_local_parameter)) {
            return current == formal
                ? std::nullopt
                : std::optional<DeclarationId> { current };
        }
        std::optional<DeclarationId> next;
        for (auto frame = binding_frames_.rbegin();
             frame != binding_frames_.rend(); ++frame) {
            const auto binding = std::ranges::find(
                *frame, current, &CompiledActualBinding::formal);
            if (binding == frame->end()) {
                continue;
            }
            next = binding->actual_declaration;
            if (!next && binding->expression) {
                const auto view = find_expression(
                    *design_, effective_, *binding->expression);
                if (view && view->systemverilog != nullptr
                    && view->systemverilog->referenced_name) {
                    next = view->systemverilog->referenced_name->selected;
                } else if (view && view->vhdl != nullptr
                    && view->vhdl->referenced_name) {
                    next = view->vhdl->referenced_name->selected;
                    if (!next
                        && view->vhdl->referenced_name->overloads.size()
                            == 1U) {
                        next = view->vhdl->referenced_name
                                   ->overloads.front();
                    }
                    // `f => r.e` associates an element of `r`, not `r`
                    // (IEEE 1076-2008 8.3): the formal does not alias the
                    // root object the name retains.
                    const std::string_view spelling
                        = view->vhdl->referenced_name->canonical;
                    const auto separator = spelling.find_last_of('.');
                    const auto root = next
                        ? find_declaration(*design_, effective_, *next)
                        : std::nullopt;
                    if (separator != std::string_view::npos && root
                        && root->vhdl != nullptr
                        && !support::same_vhdl_identifier(
                            spelling.substr(separator + 1U),
                            root->vhdl->name)) {
                        next.reset();
                    }
                }
            }
            break;
        }
        if (!next && effective_ != nullptr) {
            const auto& actuals
                = effective_->specialization().actual_identities;
            const auto actual = std::ranges::find(actuals, current,
                &SpecializedHirActualIdentity::declaration);
            if (actual != actuals.end()) {
                next = actual->actual_declaration;
                // A materialized type actual is terminal: its expression
                // was written in the instantiating parent's scope and names
                // a formal this specialization does not bind (IEEE
                // 1800-2017 6.20.3).
                // A function call actual is a value, not a name of the
                // callee (IEEE 1800-2023 23.10).
                const auto expression = actual->actual_expression
                    ? find_expression(
                          *design_, effective_, *actual->actual_expression)
                    : std::nullopt;
                const bool call_value = expression
                    && expression->systemverilog != nullptr
                    && expression->systemverilog->kind
                        == sv::ExpressionKind::call
                    && !expression->systemverilog->text.starts_with('@');
                if (!next && actual->actual_expression
                    && !actual->systemverilog_type && !call_value) {
                    const auto resolved = resolve_expression_name(
                        *actual->actual_expression, { }, true);
                    next = resolved.unique();
                }
            }
        }
        if (!next || *next == current) {
            return current == formal
                ? std::nullopt
                : std::optional<DeclarationId> { current };
        }
        current = *next;
    }
    return std::nullopt;
}

std::optional<CompiledUnitView>
CompiledDesignResolver::find_systemverilog_package(
    const std::string_view library, const std::string_view name) const
{
    const auto* package = design_ != nullptr
        ? systemverilog_package(*design_, library, name)
        : nullptr;
    return package != nullptr ? design_->find_unit(package->id)
                              : std::nullopt;
}

CompiledDeclarationResolution
CompiledDesignResolver::resolve_systemverilog_package_member(
    const std::string_view library, const std::string_view package_name,
    const std::string_view member,
    const CompiledDeclarationPredicate& predicate) const
{
    if (design_ == nullptr || package_name.empty() || member.empty()) {
        return { CompiledResolutionStatus::invalid, { } };
    }
    const auto* package = systemverilog_package(
        *design_, library, package_name);
    if (package == nullptr) {
        return { CompiledResolutionStatus::not_found, { } };
    }
    std::vector<DeclarationId> candidates;
    const auto append_direct = [&](const sv::Unit& current) {
        const auto initial_size = candidates.size();
        for (const auto id : current.declarations) {
            const auto view = find_declaration(*design_, effective_, id);
            if (view && view->systemverilog != nullptr
                && view->systemverilog->name == member
                && (!predicate || predicate(*view))) {
                candidates.push_back(id);
            }
        }
        return candidates.size() != initial_size;
    };
    std::unordered_set<std::uint32_t> visiting;
    collect_systemverilog_package_exports(*design_, *package, member,
        visiting, append_direct);
    return declaration_result(std::move(candidates));
}

CompiledSystemVerilogLetResolution
CompiledDesignResolver::resolve_systemverilog_package_let(
    const std::string_view library, const std::string_view package_name,
    const std::string_view member) const
{
    if (design_ == nullptr || package_name.empty() || member.empty()) {
        return { CompiledResolutionStatus::invalid, { } };
    }
    const auto* package = systemverilog_package(
        *design_, library, package_name);
    if (package == nullptr) {
        return { CompiledResolutionStatus::not_found, { } };
    }
    std::vector<const sv::LetDeclaration*> candidates;
    const auto append_direct = [&](const sv::Unit& current) {
        const auto initial_size = candidates.size();
        for (const auto& declaration : current.lets) {
            if (declaration.name == member) {
                candidates.push_back(&declaration);
            }
        }
        return candidates.size() != initial_size;
    };
    std::unordered_set<std::uint32_t> visiting;
    collect_systemverilog_package_exports(*design_, *package, member,
        visiting, append_direct);
    sort_unique(candidates);
    return { status_for(candidates.size()), std::move(candidates) };
}

CompiledSystemVerilogClassResolution
CompiledDesignResolver::resolve_systemverilog_package_class(
    const std::string_view library, const std::string_view package_name,
    const std::string_view member) const
{
    if (design_ == nullptr || package_name.empty() || member.empty()) {
        return { CompiledResolutionStatus::invalid, { } };
    }
    const auto* package = systemverilog_package(
        *design_, library, package_name);
    if (package == nullptr) {
        return { CompiledResolutionStatus::not_found, { } };
    }
    std::vector<const sv::ClassDeclaration*> candidates;
    const auto append_direct = [&](const sv::Unit& current) {
        const auto initial_size = candidates.size();
        for (const auto& declaration : design_->systemverilog_hir.classes()) {
            if (declaration.name == member
                && class_belongs_to_unit(*design_, declaration, current.id)) {
                candidates.push_back(&declaration);
            }
        }
        return candidates.size() != initial_size;
    };
    std::unordered_set<std::uint32_t> visiting;
    collect_systemverilog_package_exports(*design_, *package, member,
        visiting, append_direct);
    std::ranges::sort(candidates, {},
        &sv::ClassDeclaration::canonical_identity);
    const auto duplicate = std::ranges::unique(candidates, {},
        &sv::ClassDeclaration::canonical_identity);
    candidates.erase(duplicate.begin(), duplicate.end());
    return { status_for(candidates.size()), std::move(candidates) };
}

CompiledSystemVerilogClassResolution
CompiledDesignResolver::resolve_systemverilog_class(
    const std::string_view name, const ScopeId use_scope,
    const bool same_library_fallback,
    const std::uint64_t source_order) const
{
    const auto reference = design_ != nullptr
        ? systemverilog_scoped_reference(
              *design_, selected_unit_, name, use_scope)
        : std::nullopt;
    if (!reference) {
        return { CompiledResolutionStatus::invalid, { } };
    }
    const auto [member, package_name, owner] = *reference;
    const auto make_result = [](auto candidates) {
        std::ranges::sort(candidates, {},
            &sv::ClassDeclaration::canonical_identity);
        const auto duplicate = std::ranges::unique(candidates, {},
            &sv::ClassDeclaration::canonical_identity);
        candidates.erase(duplicate.begin(), duplicate.end());
        return CompiledSystemVerilogClassResolution {
            status_for(candidates.size()), std::move(candidates)
        };
    };
    if (package_name.empty()) {
        auto scope = use_scope;
        for (std::size_t depth { };
             scope.valid() && depth <= design_->semantics.scopes().size();
             ++depth) {
            std::vector<const sv::ClassDeclaration*> candidates;
            for (const auto& declaration :
                design_->systemverilog_hir.classes()) {
                if (declaration.scope == scope
                    && declaration.name == member) {
                    candidates.push_back(&declaration);
                }
            }
            auto result = make_result(std::move(candidates));
            if (result.status != CompiledResolutionStatus::not_found) {
                return result;
            }
            const auto* record = find_scope(*design_, scope);
            if (record == nullptr || !record->parent) {
                break;
            }
            scope = *record->parent;
        }
    }
    const auto package_tier = [&](const std::string_view requested) {
        return resolve_systemverilog_package_class(
            owner->systemverilog->library, requested, member);
    };
    if (!package_name.empty()) {
        return package_tier(package_name);
    }
    std::vector<const sv::ClassDeclaration*> explicit_candidates;
    for (const auto& import : owner->systemverilog->imports) {
        if (import_follows_reference(import.source_order, source_order)
            || !import.member || import.member->spelling != member) {
            continue;
        }
        auto tier = package_tier(import.package.spelling);
        explicit_candidates.insert(explicit_candidates.end(),
            tier.candidates.begin(), tier.candidates.end());
    }
    auto result = make_result(std::move(explicit_candidates));
    if (result.status != CompiledResolutionStatus::not_found) {
        return result;
    }
    std::vector<const sv::ClassDeclaration*> wildcard_candidates;
    for (const auto& import : owner->systemverilog->imports) {
        if (import_follows_reference(import.source_order, source_order)
            || !import.wildcard) {
            continue;
        }
        auto tier = package_tier(import.package.spelling);
        wildcard_candidates.insert(wildcard_candidates.end(),
            tier.candidates.begin(), tier.candidates.end());
    }
    result = make_result(std::move(wildcard_candidates));
    if (result.status != CompiledResolutionStatus::not_found
        || !same_library_fallback || source_order != 0U) {
        return result;
    }
    std::vector<const sv::ClassDeclaration*> fallback;
    for (const auto& package : design_->systemverilog_units()) {
        if (package.kind != sv::UnitKind::package
            || normalized_library(package.library)
                != normalized_library(owner->systemverilog->library)) {
            continue;
        }
        auto tier = resolve_systemverilog_package_class(
            package.library, package.name, member);
        fallback.insert(fallback.end(),
            tier.candidates.begin(), tier.candidates.end());
    }
    return make_result(std::move(fallback));
}

CompiledSystemVerilogClassPropertyResolution
CompiledDesignResolver::resolve_systemverilog_class_property(
    const std::string_view identity,
    const std::optional<bool> static_storage) const
{
    if (design_ == nullptr || identity.empty()) {
        return { CompiledResolutionStatus::invalid, { } };
    }
    std::vector<const sv::ClassProperty*> candidates;
    for (const auto& declaration : design_->systemverilog_hir.classes()) {
        for (const auto& property : declaration.properties) {
            if (property.canonical_identity == identity
                && (!static_storage
                    || property.static_storage == *static_storage)) {
                candidates.push_back(&property);
            }
        }
    }
    return { status_for(candidates.size()), std::move(candidates) };
}

CompiledSystemVerilogClassMethodResolution
CompiledDesignResolver::resolve_systemverilog_class_method(
    const std::string_view identity,
    const std::optional<bool> static_method,
    const std::optional<sv::ClassMethodKind> kind) const
{
    if (design_ == nullptr || identity.empty()) {
        return { CompiledResolutionStatus::invalid, { } };
    }
    std::vector<const sv::ClassMethod*> candidates;
    for (const auto& declaration : design_->systemverilog_hir.classes()) {
        for (const auto& method : declaration.methods) {
            if (method.canonical_identity == identity
                && (!static_method
                    || method.static_method == *static_method)
                && (!kind || method.kind == *kind)) {
                candidates.push_back(&method);
            }
        }
    }
    return { status_for(candidates.size()), std::move(candidates) };
}

std::vector<const sv::Statement*>
CompiledDesignResolver::systemverilog_import_statements(
    const ScopeId scope) const
{
    const auto& statements = design_->systemverilog_hir.statements();
    if (const auto positions
        = design_->systemverilog_import_statements_in_scope(scope)) {
        std::vector<const sv::Statement*> result;
        result.reserve(positions->size());
        for (const auto position : *positions) {
            result.push_back(&statements[position]);
        }
        return result;
    }
    if (!import_statements_) {
        auto& index = import_statements_.emplace();
        for (const auto& statement : statements) {
            if (!statement.imports.empty() && statement.nested_scope) {
                index[statement.nested_scope->value()].push_back(&statement);
            }
        }
    }
    const auto found = import_statements_->find(scope.value());
    return found == import_statements_->end()
        ? std::vector<const sv::Statement*> { }
        : found->second;
}

CompiledDeclarationResolution
CompiledDesignResolver::resolve_systemverilog_name(
    const sv::Name& name, const ScopeId use_scope,
    const CompiledDeclarationPredicate& predicate,
    const bool same_library_fallback) const
{
    if (design_ == nullptr || name.spelling.empty()) {
        return { CompiledResolutionStatus::invalid, { } };
    }
    const auto* use_scope_record = find_scope(*design_, use_scope);
    const auto use_unit = use_scope_record != nullptr
        ? design_->find_unit(use_scope_record->unit)
        : std::nullopt;
    const auto* generate_owner = use_unit
            && use_unit->systemverilog != nullptr
        ? use_unit->systemverilog
        : nullptr;
    const auto simple_name = name.spelling.find('.') == std::string::npos
        && name.spelling.find("::") == std::string::npos;
    // Whether an import in an enclosing scope or a compilation-unit
    // declaration takes the name from the declaration selected at parse
    // time; computed only when the name carries one.
    std::optional<bool> shadowed;
    const auto lexical_import_has_member = [&] {
        if (!simple_name) {
            return false;
        }
        const auto owner = generate_owner != nullptr
            ? use_unit
            : design_->find_unit(selected_unit_);
        if (!owner || owner->systemverilog == nullptr) {
            return false;
        }
        const auto provides = [&](const sv::Statement& statement) {
            for (const auto& import : statement.imports) {
                if (import_follows_reference(
                        import.source_order, name.source_order)) {
                    continue;
                }
                if (!import.wildcard
                    && (!import.member
                        || import.member->spelling != name.spelling)) {
                    continue;
                }
                const auto imported = resolve_systemverilog_package_member(
                    owner->systemverilog->library, import.package.spelling,
                    name.spelling, predicate);
                if (imported.status != CompiledResolutionStatus::not_found) {
                    return true;
                }
            }
            return false;
        };
        const auto& statements = design_->systemverilog_hir.statements();
        auto scope = std::optional { use_scope };
        while (scope) {
            if (const auto positions
                = design_->systemverilog_import_statements_in_scope(
                    *scope)) {
                for (const auto position : *positions) {
                    if (provides(statements[position])) {
                        return true;
                    }
                }
            } else {
                for (const auto* statement
                    : systemverilog_import_statements(*scope)) {
                    if (provides(*statement)) {
                        return true;
                    }
                }
            }
            const auto* record = find_scope(*design_, *scope);
            scope = record != nullptr ? record->parent : std::nullopt;
        }
        return false;
    };
    const auto compilation_unit_name_is_visible = [&] {
        if (!simple_name) {
            return false;
        }
        if (generate_owner == nullptr) {
            return false;
        }
        const auto* source_unit = source_package_for_compilation_unit(
            *design_, *generate_owner);
        if (source_unit == nullptr) {
            return false;
        }
        const auto declares = [&](const DeclarationId id) {
            const auto declaration = find_declaration(
                *design_, effective_, id);
            return declaration && declaration->systemverilog != nullptr
                && declaration->systemverilog->scope == source_unit->scope
                && declaration->systemverilog->name == name.spelling;
        };
        // A specialization that renames declarations is not in the index.
        if (effective_ == nullptr
            || !effective_->renames_systemverilog_declarations()) {
            if (const auto named
                = design_->systemverilog_declarations_named_in_scope(
                    source_unit->scope, name.spelling)) {
                return std::ranges::any_of(*named, declares);
            }
        }
        return std::ranges::any_of(source_unit->declarations, declares);
    };
    const auto name_is_shadowed = [&] {
        if (!shadowed) {
            shadowed = lexical_import_has_member()
                || compilation_unit_name_is_visible();
        }
        return *shadowed;
    };
    const auto retained_is_valid = [&](const DeclarationId id) {
        const auto declaration = find_declaration(
            *design_, effective_, id);
        if (!declaration || declaration->systemverilog == nullptr
            || (predicate && !predicate(*declaration))) {
            return false;
        }
        return !simple_name
            || declaration->systemverilog->name == name.spelling;
    };
    const auto generate_candidate = [&](const DeclarationId id) {
        return generate_owner != nullptr
            ? systemverilog_generate_candidate(*design_, *generate_owner, id)
            : std::nullopt;
    };
    const auto resolve_generated_candidates
        = [&](CompiledDeclarationResolution result) {
              if (result.candidates.empty() || generate_owner == nullptr) {
                  return result;
              }
              std::vector<SystemVerilogGenerateCandidate> generated;
              for (const auto candidate : result.candidates) {
                  auto path = generate_candidate(candidate);
                  if (!path) {
                      return result;
                  }
                  generated.push_back(std::move(*path));
              }
              if (result.candidates.size() > 1U
                  && !systemverilog_generate_candidates_are_exclusive(
                      generated)) {
                  return result;
              }
              if (effective_ == nullptr) {
                  return result.candidates.size() > 1U
                      ? CompiledDeclarationResolution {
                            CompiledResolutionStatus::not_found, { } }
                      : result;
              }
              if (effective_->unit() != generate_owner->id) {
                  return result;
              }
              const auto selected = effective_->selected_generates();
              std::vector<DeclarationId> active;
              for (std::size_t index = 0;
                   index < result.candidates.size(); ++index) {
                  const bool is_active = std::ranges::all_of(
                      generated[index].branches,
                      [&](const SystemVerilogGenerateBranch& branch) {
                          return std::ranges::find(
                                     selected, branch.branch)
                              != selected.end();
                      });
                  if (is_active) {
                      active.push_back(result.candidates[index]);
                  }
              }
              if (active.size() == 1U) {
                  return CompiledDeclarationResolution {
                      CompiledResolutionStatus::unique, std::move(active) };
              }
              if (result.candidates.size() == 1U && active.empty()) {
                  return CompiledDeclarationResolution {
                      CompiledResolutionStatus::not_found, { } };
              }
              return result;
          };
    if (name.selected && retained_is_valid(*name.selected)
        && !name_is_shadowed()) {
        const auto declaration = find_declaration(
            *design_, effective_, *name.selected);
        if (!generate_candidate(*name.selected)) {
            // Genvars name only generate blocks of the use unit.
            if (simple_name && declaration
                && (generate_owner == nullptr
                    || !generate_owner->generates.empty())
                && systemverilog_genvar_hides(*design_, use_scope,
                    declaration->systemverilog->scope, name.spelling)) {
                return { CompiledResolutionStatus::not_found, { } };
            }
            CompiledDeclarationResolution result {
                CompiledResolutionStatus::unique, { *name.selected }
            };
            return resolve_generated_candidates(std::move(result));
        }
    }
    const auto normalize_synthetic_duplicate
        = [&](CompiledDeclarationResolution result) {
              if (result.status != CompiledResolutionStatus::ambiguous
                  || !synthetic_enum_constant_duplicate(
                      *design_, effective_, result.candidates)) {
                  return result;
              }
              const auto ranked = rank_systemverilog_constants(
                  *design_, effective_, result.candidates);
              return ranked && ranked->size() == 1U
                  ? declaration_result(*ranked)
                  : result;
          };
    std::vector<DeclarationId> retained;
    if (!name.overloads.empty() && !name_is_shadowed()) {
        for (const auto candidate : name.overloads) {
            if (retained_is_valid(candidate)) {
                retained.push_back(candidate);
            }
        }
    }
    const bool retained_generated = std::ranges::any_of(
        retained, [&](const DeclarationId id) {
            return generate_candidate(id).has_value();
        });
    if (!retained.empty() && !retained_generated) {
        auto result = normalize_synthetic_duplicate(
            declaration_result(std::move(retained)));
        return resolve_generated_candidates(std::move(result));
    }
    auto result = normalize_synthetic_duplicate(resolve_systemverilog(
        name.spelling, use_scope, predicate,
        same_library_fallback && name.source_order == 0U,
        std::nullopt, name.source_order));
    return resolve_generated_candidates(std::move(result));
}

CompiledDeclarationResolution
CompiledDesignResolver::resolve_systemverilog(
    const std::string_view name, const ScopeId use_scope,
    const CompiledDeclarationPredicate& predicate,
    const bool same_library_fallback,
    const std::optional<ScopeId> stop_scope,
    const std::uint64_t source_order) const
{
    // `local::` names the scope of the randomize call in an inline
    // constraint (IEEE 1800-2017 18.7.1).
    constexpr std::string_view local_scope = "local::";
    if (name.starts_with(local_scope)) {
        return resolve_systemverilog(name.substr(local_scope.size()),
            use_scope, predicate, same_library_fallback, stop_scope,
            source_order);
    }
    const auto reference = design_ != nullptr
        ? systemverilog_scoped_reference(
              *design_, selected_unit_, name, use_scope)
        : std::nullopt;
    if (!reference) {
        return { CompiledResolutionStatus::invalid, { } };
    }
    const auto [member, package_name, owner] = *reference;
    const auto* compilation_unit_source = source_package_for_compilation_unit(
        *design_, *owner->systemverilog);

    const auto declarations_in_scope = [&](const ScopeId scope) {
        std::vector<DeclarationId> candidates;
        visit_systemverilog_declarations_named_in_scope(
            *design_, effective_, scope, member,
            [&](const CompiledDeclarationView& view) {
                if (!predicate || predicate(view)) {
                    candidates.push_back(view.systemverilog->id);
                }
            });
        auto result = declaration_result(std::move(candidates));
        if (result.status != CompiledResolutionStatus::not_found
            || compilation_unit_source == nullptr
            || scope != owner->systemverilog->scope) {
            return result;
        }
        std::vector<DeclarationId> compilation_unit_candidates;
        for (const auto id : compilation_unit_source->declarations) {
            const auto declaration = find_declaration(
                *design_, effective_, id);
            if (declaration && declaration->systemverilog != nullptr
                && declaration->systemverilog->scope
                    == compilation_unit_source->scope
                && declaration->systemverilog->name == member
                && (!predicate || predicate(*declaration))) {
                compilation_unit_candidates.push_back(id);
            }
        }
        auto cu_result = declaration_result(std::move(compilation_unit_candidates));
        return cu_result;
    };
    const auto package_tier = [&](const std::string_view requested) {
        auto result = resolve_systemverilog_package_member(
            owner->systemverilog->library, requested, member, predicate);
        return result;
    };
    const auto imports_in_scope = [&](const ScopeId scope)
        -> const std::vector<sv::Import>* {
        const auto statements = systemverilog_import_statements(scope);
        return statements.size() == 1U ? &statements.front()->imports
                                       : nullptr;
    };
    if (package_name.empty()) {
        auto scope = use_scope;
        for (std::size_t depth { };
             scope.valid() && depth <= design_->semantics.scopes().size();
             ++depth) {
            const auto tier = declarations_in_scope(scope);
            if (tier.status != CompiledResolutionStatus::not_found) {
                return tier;
            }
            if (const auto* imports = imports_in_scope(scope)) {
                std::vector<DeclarationId> explicit_candidates;
                for (const auto& import : *imports) {
                    if (import_follows_reference(
                            import.source_order, source_order)) {
                        continue;
                    }
                    if (!import.member || import.member->spelling != member) {
                        continue;
                    }
                    const auto imported = package_tier(
                        import.package.spelling);
                    explicit_candidates.insert(
                        explicit_candidates.end(), imported.candidates.begin(),
                        imported.candidates.end());
                }
                auto import_result = declaration_result(
                    std::move(explicit_candidates));
                if (import_result.status != CompiledResolutionStatus::not_found) {
                    return import_result;
                }
                std::vector<DeclarationId> wildcard_candidates;
                for (const auto& import : *imports) {
                    if (import_follows_reference(
                            import.source_order, source_order)
                        || !import.wildcard) {
                        continue;
                    }
                    const auto imported = package_tier(
                        import.package.spelling);
                    wildcard_candidates.insert(
                        wildcard_candidates.end(), imported.candidates.begin(),
                        imported.candidates.end());
                }
                import_result = declaration_result(
                    std::move(wildcard_candidates));
                if (import_result.status != CompiledResolutionStatus::not_found) {
                    return import_result;
                }
            }
            if (systemverilog_genvar_scope(*design_, scope, member)) {
                return { CompiledResolutionStatus::not_found, { } };
            }
            if (stop_scope && scope == *stop_scope) {
                break;
            }
            const auto* record = find_scope(*design_, scope);
            if (record == nullptr || !record->parent) {
                break;
            }
            scope = *record->parent;
        }
    }

    if (!package_name.empty()) {
        // `$unit::name` names the compilation-unit scope the unit imports
        // (IEEE 1800-2017 3.12.1, 26.3).
        std::string_view requested_package = package_name;
        if (package_name == "$unit") {
            for (const auto& import : owner->systemverilog->imports) {
                if (import.package.spelling.starts_with("fsim_unit_")) {
                    requested_package = import.package.spelling;
                    break;
                }
            }
        }
        auto result = package_tier(requested_package);
        if (result.status != CompiledResolutionStatus::not_found) {
            return result;
        }

        std::vector<DeclarationId> class_candidates;
        for (const auto& declaration :
            design_->systemverilog_hir.classes()) {
            if (declaration.name != package_name) {
                continue;
            }
            for (const auto id : declaration.type_aliases) {
                const auto view = find_declaration(
                    *design_, effective_, id);
                if (view && view->systemverilog != nullptr
                    && view->systemverilog->name == member
                    && (!predicate || predicate(*view))) {
                    class_candidates.push_back(id);
                }
            }
        }
        return declaration_result(std::move(class_candidates));
    }

    std::vector<DeclarationId> explicit_candidates;
    for (const auto& import : owner->systemverilog->imports) {
        if (!import.member || import.member->spelling != member) {
            continue;
        }
        const auto tier = package_tier(import.package.spelling);
        explicit_candidates.insert(explicit_candidates.end(),
            tier.candidates.begin(), tier.candidates.end());
    }
    auto result = declaration_result(std::move(explicit_candidates));
    if (result.status != CompiledResolutionStatus::not_found) {
        return result;
    }

    std::vector<DeclarationId> wildcard_candidates;
    for (const auto& import : owner->systemverilog->imports) {
        if (!import.wildcard) {
            continue;
        }
        const auto tier = package_tier(import.package.spelling);
        wildcard_candidates.insert(wildcard_candidates.end(),
            tier.candidates.begin(), tier.candidates.end());
    }
    result = declaration_result(std::move(wildcard_candidates));
    if (result.status != CompiledResolutionStatus::not_found
        || !same_library_fallback) {
        return result;
    }

    std::vector<DeclarationId> fallback;
    for (const auto& package : design_->systemverilog_units()) {
        if (package.kind != sv::UnitKind::package
            || normalized_library(package.library)
                != normalized_library(owner->systemverilog->library)) {
            continue;
        }
        for (const auto id : package.declarations) {
            const auto view = find_declaration(*design_, effective_, id);
            if (view && view->systemverilog != nullptr
                && view->systemverilog->name == member
                && (!predicate || predicate(*view))) {
                fallback.push_back(id);
            }
        }
    }
    return declaration_result(std::move(fallback));
}

CompiledDeclarationResolution
CompiledDesignResolver::resolve_systemverilog_interface_member(
    const std::string_view receiver, const std::string_view member,
    const ScopeId use_scope,
    const CompiledDeclarationPredicate& predicate,
    const std::string_view generic_interface) const
{
    if (design_ == nullptr || receiver.empty() || member.empty()) {
        return { CompiledResolutionStatus::invalid, { } };
    }
    const auto interface_object = [&](const CompiledDeclarationView& view) {
        if (view.systemverilog == nullptr) {
            return false;
        }
        const auto& declaration = *view.systemverilog;
        return !declaration.interface_type.empty()
            || (declaration.type
                && !declaration.type->interface_type.empty())
            || (!generic_interface.empty() && declaration.type
                && declaration.type->target.spelling == "interface");
    };
    auto receiver_id = resolve_systemverilog(
        receiver, use_scope, interface_object, false).unique();
    // `vif.sub.f`: steps through interface instances of the receiver's
    // interface (IEEE 1800-2017 25.3, 25.9).
    std::string_view steps;
    if (!receiver_id) {
        if (const auto dot = receiver.find('.');
            dot != std::string_view::npos && dot != 0U) {
            receiver_id = resolve_systemverilog(receiver.substr(0U, dot),
                use_scope, interface_object, false).unique();
            steps = receiver.substr(dot + 1U);
        }
    }
    const auto receiver_view = receiver_id
        ? find_declaration(*design_, effective_, *receiver_id)
        : std::nullopt;
    if (!receiver_view || receiver_view->systemverilog == nullptr) {
        return { CompiledResolutionStatus::not_found, { } };
    }
    const auto& declaration = *receiver_view->systemverilog;
    const auto interface_name = !declaration.interface_type.empty()
        ? std::string_view { declaration.interface_type }
        : declaration.type && !declaration.type->interface_type.empty()
        ? std::string_view { declaration.type->interface_type }
        : generic_interface;
    const auto modport = !declaration.modport.empty()
        ? std::string_view { declaration.modport }
        : declaration.type
            ? std::string_view { declaration.type->interface_modport }
            : std::string_view { };

    auto library = std::string_view { "work" };
    if (const auto* scope = find_scope(*design_, declaration.scope)) {
        if (const auto owner = design_->find_unit(scope->unit);
            owner && owner->systemverilog != nullptr) {
            library = normalized_library(owner->systemverilog->library);
        }
    }
    auto selected = design_->find_unit(
        UnitKind::systemverilog_interface, library, interface_name);
    if (!selected || selected->systemverilog == nullptr) {
        return { CompiledResolutionStatus::not_found, { } };
    }
    auto modport_name = modport;
    while (!steps.empty()) {
        const auto dot = steps.find('.');
        const auto step = steps.substr(0U, dot);
        // `bif.consumer.send`: a last step naming a modport of the
        // interface selects the member through that modport (25.5).
        if (dot == std::string_view::npos
            && std::ranges::find(selected->systemverilog->modports, step,
                   &sv::Modport::name)
                != selected->systemverilog->modports.end()) {
            modport_name = step;
            break;
        }
        std::optional<CompiledUnitView> next;
        for (const auto instance_id : selected->systemverilog->instances) {
            const auto instance = design_->find_instance(instance_id);
            if (!instance || instance->systemverilog == nullptr
                || instance->systemverilog->name != step) {
                continue;
            }
            next = design_->find_unit(UnitKind::systemverilog_interface,
                library, instance->systemverilog->target.spelling);
            break;
        }
        if (!next || next->systemverilog == nullptr) {
            return { CompiledResolutionStatus::not_found, { } };
        }
        selected = next;
        // A modport restricts only the receiver's own interface (25.5).
        modport_name = { };
        steps = dot == std::string_view::npos ? std::string_view { }
                                              : steps.substr(dot + 1U);
    }
    const auto& interface = *selected->systemverilog;
    if (!modport_name.empty()) {
        const auto view = std::ranges::find(
            interface.modports, modport_name, &sv::Modport::name);
        if (view == interface.modports.end()
            || std::ranges::none_of(
                view->members,
                [&](const sv::ModportMember& candidate) {
                    return candidate.name.spelling == member
                        && (candidate.kind
                                == sv::ModportMemberKind::signal
                            || candidate.kind
                                == sv::ModportMemberKind::function_import
                            || candidate.kind
                                == sv::ModportMemberKind::task_import);
                })) {
            return { CompiledResolutionStatus::not_found, { } };
        }
    }
    std::vector<DeclarationId> candidates;
    for (const auto candidate : interface.declarations) {
        const auto view = find_declaration(
            *design_, effective_, candidate);
        if (view && view->systemverilog != nullptr
            && view->systemverilog->name == member
            && (!predicate || predicate(*view))) {
            candidates.push_back(candidate);
        }
    }
    return declaration_result(std::move(candidates));
}

CompiledSystemVerilogLetResolution
CompiledDesignResolver::resolve_systemverilog_let(
    const std::string_view name, const ScopeId use_scope) const
{
    const auto reference = design_ != nullptr
        ? systemverilog_scoped_reference(
              *design_, selected_unit_, name, use_scope)
        : std::nullopt;
    if (!reference) {
        return { CompiledResolutionStatus::invalid, { } };
    }
    const auto [member, package_name, owner] = *reference;
    const auto package_tier = [&](const std::string_view requested) {
        return resolve_systemverilog_package_let(
            owner->systemverilog->library, requested, member);
    };
    if (!package_name.empty()) {
        return package_tier(package_name);
    }

    std::vector<const sv::LetDeclaration*> lexical;
    std::size_t lexical_depth { };
    const auto& selected = effective_ != nullptr
        ? effective_->selected_generates()
        : std::span<const DeclarationId> { };
    const auto visit = [&](const auto& self,
                           const sv::GenerateRegion& region) -> void {
        if (std::ranges::find(selected, region.declaration)
                == selected.end()
            || !scope_contains(*design_, region.scope, use_scope)) {
            return;
        }
        const auto depth = scope_depth(*design_, region.scope);
        for (const auto& declaration : region.lets) {
            if (declaration.name != member) {
                continue;
            }
            if (depth > lexical_depth) {
                lexical.clear();
                lexical_depth = depth;
            }
            if (depth == lexical_depth) {
                lexical.push_back(&declaration);
            }
        }
        for (const auto& child : region.nested) {
            self(self, child);
        }
    };
    for (const auto& region : owner->systemverilog->generates) {
        visit(visit, region);
    }
    if (lexical.empty()) {
        for (const auto& declaration : owner->systemverilog->lets) {
            if (declaration.name == member) {
                lexical.push_back(&declaration);
            }
        }
    }
    if (!lexical.empty()) {
        return { status_for(lexical.size()), std::move(lexical) };
    }

    std::vector<const sv::LetDeclaration*> explicit_candidates;
    for (const auto& import : owner->systemverilog->imports) {
        if (!import.member || import.member->spelling != member) {
            continue;
        }
        const auto tier = package_tier(import.package.spelling);
        explicit_candidates.insert(explicit_candidates.end(),
            tier.candidates.begin(), tier.candidates.end());
    }
    if (!explicit_candidates.empty()) {
        sort_unique(explicit_candidates);
        return { status_for(explicit_candidates.size()),
            std::move(explicit_candidates) };
    }

    std::vector<const sv::LetDeclaration*> wildcard_candidates;
    for (const auto& import : owner->systemverilog->imports) {
        if (!import.wildcard) {
            continue;
        }
        const auto tier = package_tier(import.package.spelling);
        wildcard_candidates.insert(wildcard_candidates.end(),
            tier.candidates.begin(), tier.candidates.end());
    }
    sort_unique(wildcard_candidates);
    return { status_for(wildcard_candidates.size()),
        std::move(wildcard_candidates) };
}

CompiledDeclarationResolution
CompiledDesignResolver::resolve_systemverilog_constant(
    const std::string_view name, const ScopeId use_scope,
    const bool same_library_fallback,
    const std::uint64_t source_order) const
{
    using Form = sv::DeclarationForm;
    const CompiledDeclarationPredicate constant = [](const auto& view) {
        return view.systemverilog != nullptr
            && (view.systemverilog->form == Form::parameter
                || view.systemverilog->form == Form::local_parameter
                || view.systemverilog->form == Form::enumeration_literal);
    };
    auto result = resolve_systemverilog(
        name, use_scope, constant,
        same_library_fallback && source_order == 0U,
        std::nullopt, source_order);
    if (result.candidates.empty()) {
        return result;
    }
    if (const auto ranked = rank_systemverilog_constants(
            *design_, effective_, result.candidates)) {
        return declaration_result(*ranked);
    }
    return result;
}

CompiledDeclarationResolution
CompiledDesignResolver::resolve_systemverilog_named_type(
    const std::string_view name, const ScopeId use_scope,
    const bool same_library_fallback,
    const std::uint64_t source_order) const
{
    // A built-in type keyword is a reserved word and never the name of a
    // declaration (IEEE 1800-2017 5.6.2, Annex B); a declaration that names
    // a type needs the 1800 keywords, where all of these are reserved.
    static constexpr std::array<std::string_view, 27> builtin_types {
        "bit", "byte", "chandle", "event", "int", "integer", "logic",
        "longint", "real", "realtime", "reg", "shortint", "shortreal",
        "string", "supply0", "supply1", "time", "tri", "tri0", "tri1",
        "triand", "trior", "trireg", "uwire", "wand", "wire", "wor",
    };
    if (std::ranges::binary_search(builtin_types, name)) {
        return { CompiledResolutionStatus::not_found, { } };
    }
    using Form = sv::DeclarationForm;
    const CompiledDeclarationPredicate named_type = [&](const auto& view) {
        return view.systemverilog != nullptr
            && (view.systemverilog->form == Form::type_parameter
                || view.systemverilog->form == Form::typedef_declaration
                || view.systemverilog->form
                    == Form::nettype_declaration
                || systemverilog_declared_type(
                    *design_, effective_, *view.systemverilog));
    };
    return resolve_systemverilog(
        name, use_scope, named_type,
        same_library_fallback && source_order == 0U, std::nullopt,
        source_order);
}

std::optional<TypeId> CompiledDesignResolver::resolve_vhdl_named_type(
    const std::string_view spelling,
    const std::optional<ScopeId> owner_scope) const
{
    if (design_ == nullptr) {
        return std::nullopt;
    }
    // `work.pkg.t` is the expanded name of `t` in the working library's
    // package `pkg` (IEEE 1076-2008 8.3, 13.2): resolve it as `pkg.t`.
    if (spelling.size() > 5U
        && same_vhdl_identifier(spelling.substr(0U, 4U), "work")
        && spelling[4] == '.'
        && spelling.find('.', 5U) != std::string_view::npos) {
        if (const auto unqualified
            = resolve_vhdl_named_type(spelling.substr(5U), owner_scope)) {
            return unqualified;
        }
    }
    const auto simple_name = simple_vhdl_name(spelling);
    if (simple_name.empty()) {
        return std::nullopt;
    }
    const auto find_type = [&](const TypeId id) {
        return semantic::find_type(*design_, effective_, id);
    };
    // The stored types that can carry `simple_name`, in HIR order: the
    // name index when it is current and no specialization replacement
    // can rename a type, else every type.
    const auto named_type_ids = [&] {
        std::vector<TypeId> ids;
        const auto& types = design_->vhdl_hir.types();
        const auto positions
            = effective_ == nullptr || !effective_->replaces_vhdl_types()
            ? design_->vhdl_type_positions_named(simple_name)
            : std::nullopt;
        if (positions) {
            ids.reserve(positions->size());
            for (const auto position : *positions) {
                ids.push_back(types[position].id);
            }
        } else {
            ids.reserve(types.size());
            for (const auto& stored : types) {
                ids.push_back(stored.id);
            }
        }
        return ids;
    }();
    if (const auto identity = parse_vhdl_declaration_identity(spelling)) {
        std::vector<const vhdl::TypeDefinition*> named;
        for (const auto stored_id : named_type_ids) {
            const auto type = find_type(stored_id);
            if (type && type->vhdl != nullptr
                && same_vhdl_identifier(type->vhdl->name, simple_name)) {
                named.push_back(type->vhdl);
            }
        }
        const auto selection
            = select_vhdl_identity_type(*design_, named, *identity);
        using Status = VhdlIdentityTypeSelection::Status;
        if (selection.status == Status::unique) {
            return selection.type;
        }
        if (selection.status == Status::ambiguous) {
            return std::nullopt;
        }
    }

    const auto separator = spelling.find_last_of(".:");
    auto qualifier = separator == std::string_view::npos
        ? std::string_view { }
        : spelling.substr(0U, separator);
    if (spelling.starts_with("vhdl-hir-type:")
        || spelling.starts_with("@fsim-vital:")) {
        qualifier = { };
    }

    const Scope* owner = owner_scope
        ? find_scope(*design_, *owner_scope)
        : nullptr;
    const auto owner_unit = owner != nullptr
        ? design_->find_unit(owner->unit)
        : std::nullopt;
    const auto* owner_vhdl = owner_unit && owner_unit->vhdl != nullptr
        ? owner_unit->vhdl
        : nullptr;
    std::optional<std::string> exported_package_template;
    if (!qualifier.empty() && owner_scope) {
        const auto qualifier_name = simple_vhdl_name(qualifier);
        const auto declaration_visible = [&](const ScopeId declaration_scope) {
            auto active = owner_scope;
            while (active) {
                if (*active == declaration_scope) {
                    return true;
                }
                const auto* active_scope = find_scope(*design_, *active);
                active = active_scope != nullptr
                    ? active_scope->parent
                    : std::nullopt;
            }
            const auto* semantic_scope = find_scope(
                *design_, declaration_scope);
            const auto declaration_unit = semantic_scope != nullptr
                ? design_->find_unit(semantic_scope->unit)
                : std::nullopt;
            const auto* declaration_vhdl
                = declaration_unit && declaration_unit->vhdl != nullptr
                ? declaration_unit->vhdl
                : nullptr;
            // An instance declared in a package that a use clause makes
            // visible (IEEE 1076-2008 4.9, 12.4).
            if (owner_vhdl != nullptr && declaration_vhdl != nullptr
                && declaration_vhdl->kind == vhdl::UnitKind::package
                && declaration_vhdl->primary_name.empty()
                && semantic_scope != nullptr
                && declaration_unit->identity != nullptr
                && declaration_unit->identity->scope == declaration_scope) {
                if (vhdl_package_member_visible(
                        *owner_vhdl, *declaration_vhdl, qualifier_name)) {
                    return true;
                }
                if (owner_vhdl->kind == vhdl::UnitKind::architecture) {
                    if (const auto primary
                        = design_->vhdl_primary_unit(*owner_vhdl);
                        primary && *primary != nullptr
                        && vhdl_package_member_visible(
                            **primary, *declaration_vhdl, qualifier_name)) {
                        return true;
                    }
                }
            }
            return owner_vhdl != nullptr && declaration_vhdl != nullptr
                && owner_vhdl->kind == vhdl::UnitKind::architecture
                && declaration_vhdl->kind == vhdl::UnitKind::entity
                && same_vhdl_identifier(
                    owner_vhdl->library, declaration_vhdl->library)
                && same_vhdl_identifier(
                    owner_vhdl->primary_name, declaration_vhdl->name);
        };
        for (const auto& stored : design_->vhdl_hir.declarations()) {
            const auto declaration = find_declaration(
                *design_, effective_, stored.id);
            if (!declaration || declaration->vhdl == nullptr
                || !declaration->vhdl->package
                || (declaration->vhdl->form
                        != vhdl::DeclarationForm::package_instance
                    && declaration->vhdl->form
                        != vhdl::DeclarationForm::generic_package)
                || !same_vhdl_identifier(
                    declaration->vhdl->name, qualifier_name)
                || !declaration_visible(declaration->vhdl->scope)) {
                continue;
            }
            const auto& template_name
                = declaration->vhdl->package->template_name;
            const auto candidate = std::string { simple_vhdl_name(
                template_name.text()) };
            if (exported_package_template
                && !same_vhdl_identifier(
                    *exported_package_template, candidate)) {
                return std::nullopt;
            }
            exported_package_template = candidate;
        }
    }

    std::optional<TypeId> selected;
    int selected_score = std::numeric_limits<int>::min();
    bool ambiguous { };
    std::optional<std::size_t> selected_lexical_distance;
    const auto lexical_distance_for = [&](const ScopeId declaration_scope)
        -> std::optional<std::size_t> {
        if (!owner_scope) {
            return std::nullopt;
        }
        auto active = *owner_scope;
        std::size_t distance { };
        for (std::size_t depth { };
             active.valid()
                 && depth <= design_->semantics.scopes().size();
             ++depth) {
            if (active == declaration_scope) {
                return distance;
            }
            const auto* active_scope = find_scope(*design_, active);
            if (active_scope == nullptr || !active_scope->parent) {
                break;
            }
            active = *active_scope->parent;
            ++distance;
        }
        return std::nullopt;
    };
    // An alias of a type (6.6.3), projected as a subtype that only renames
    // its type mark, denotes that type; homographs that denote the same
    // named entity are both made directly visible (IEEE 1076-2008 12.4 c).
    const auto denoted_type = [&](TypeId id) {
        for (std::size_t depth { }; depth < 16U; ++depth) {
            const auto renamed = find_type(id);
            if (!renamed || renamed->vhdl == nullptr) {
                break;
            }
            const auto& definition = *renamed->vhdl;
            const bool renaming = definition.form == vhdl::TypeForm::alias
                || (definition.form == vhdl::TypeForm::subtype
                    && definition.base.constraints.empty()
                    && definition.base.element_constraints.empty()
                    && definition.base.resolution_function.spelling.empty());
            if (!renaming || !definition.base.type_mark.target.valid()) {
                break;
            }
            id = definition.base.type_mark.target;
        }
        return id;
    };
    const auto same_denotation = [&](const std::optional<TypeId> left,
                                     const TypeId right) {
        return left && denoted_type(*left) == denoted_type(right);
    };
    for (const auto stored_id : named_type_ids) {
        const auto type = find_type(stored_id);
        if (!type || type->vhdl == nullptr
            || !same_vhdl_identifier(type->vhdl->name, simple_name)) {
            continue;
        }
        const auto& definition = *type->vhdl;
        const auto declaration = find_declaration(
            *design_, effective_, definition.declaration);
        const auto* scope = declaration && declaration->vhdl != nullptr
            ? find_scope(*design_, declaration->vhdl->scope)
            : nullptr;
        const auto unit = scope != nullptr
            ? design_->find_unit(scope->unit)
            : std::nullopt;
        const auto* candidate_vhdl = unit && unit->vhdl != nullptr
            ? unit->vhdl
            : nullptr;
        if (!qualifier.empty() && candidate_vhdl != nullptr
            && candidate_vhdl->kind == vhdl::UnitKind::package
            && !candidate_vhdl->primary_name.empty()) {
            continue;
        }
        const auto same_unit = owner != nullptr && scope != nullptr
            && owner->unit == scope->unit;
        std::optional<std::size_t> ancestor_distance;
        if (owner_scope && same_unit) {
            ancestor_distance = lexical_distance_for(
                declaration->vhdl->scope);
            // Declarations in a nested sibling or descendant region are not
            // visible at this use site, even when they belong to the same
            // design unit.
            if (!ancestor_distance) {
                continue;
            }
        }
        const auto lexical_rank_distance
            = owner_scope && qualifier.empty() && same_unit
            ? ancestor_distance
            : std::optional<std::size_t> { };
        const auto visible_package = qualifier.empty()
            && owner_vhdl != nullptr && candidate_vhdl != nullptr
            && vhdl_package_member_visible(
                *owner_vhdl, *candidate_vhdl, simple_name);
        const auto associated_entity = qualifier.empty()
            && owner_vhdl != nullptr && candidate_vhdl != nullptr
            && owner_vhdl->kind == vhdl::UnitKind::architecture
            && candidate_vhdl->kind == vhdl::UnitKind::entity
            && same_vhdl_identifier(
                owner_vhdl->library, candidate_vhdl->library)
            && same_vhdl_identifier(
                owner_vhdl->primary_name, candidate_vhdl->name);
        if (owner_scope && qualifier.empty() && !same_unit
            && !visible_package && !associated_entity) {
            continue;
        }

        int score { };
        if (!qualifier.empty()) {
            if (candidate_vhdl == nullptr) {
                continue;
            }
            const auto unit_identity = candidate_vhdl->library.empty()
                ? std::string { "work." } + candidate_vhdl->name
                : candidate_vhdl->library + "." + candidate_vhdl->name;
            const auto qualified_match
                = same_vhdl_identifier(qualifier, candidate_vhdl->name)
                || (exported_package_template
                    && same_vhdl_identifier(
                        *exported_package_template,
                        candidate_vhdl->name))
                || same_vhdl_identifier(qualifier, unit_identity)
                || (qualifier.size() > candidate_vhdl->name.size()
                    && (qualifier[qualifier.size()
                            - candidate_vhdl->name.size() - 1U]
                            == '.'
                        || qualifier[qualifier.size()
                               - candidate_vhdl->name.size() - 1U]
                            == ':')
                    && same_vhdl_identifier(
                        qualifier.substr(
                            qualifier.size() - candidate_vhdl->name.size()),
                        candidate_vhdl->name));
            if (!qualified_match) {
                continue;
            }
            score += 2'000;
        }
        if (owner_scope && declaration && declaration->vhdl != nullptr
            && declaration->vhdl->scope == *owner_scope) {
            score += 1'000;
        }
        if (same_unit) {
            score += 800;
        }
        if (owner_vhdl != nullptr && candidate_vhdl != nullptr
            && owner_vhdl->id == candidate_vhdl->id) {
            score += 100;
        }
        if (visible_package) {
            score += 500;
        }
        if (definition.form != vhdl::TypeForm::protected_body) {
            score += 10;
        }
        if (lexical_rank_distance) {
            if (!selected_lexical_distance
                || *lexical_rank_distance < *selected_lexical_distance) {
                selected = definition.id;
                selected_score = score;
                selected_lexical_distance = lexical_rank_distance;
                ambiguous = false;
            } else if (*lexical_rank_distance
                    == *selected_lexical_distance) {
                if (score > selected_score) {
                    selected = definition.id;
                    selected_score = score;
                    ambiguous = false;
                } else if (score == selected_score
                    && selected != definition.id
                    && !same_denotation(selected, definition.id)) {
                    ambiguous = true;
                }
            }
        } else if (!selected_lexical_distance) {
            if (score > selected_score) {
                selected = definition.id;
                selected_score = score;
                ambiguous = false;
            } else if (score == selected_score
                && selected != definition.id
                && !same_denotation(selected, definition.id)) {
                ambiguous = true;
            }
        }
    }
    return ambiguous ? std::nullopt : selected;
}

bool CompiledDesignResolver::resolutions_memoizable() const noexcept
{
    return design_ != nullptr && effective_ != nullptr
        && &effective_->design() == design_
        && selected_unit_ == effective_->unit()
        && (boundary_fallback_ == nullptr || !*boundary_fallback_)
        // A name resolution in progress hides its own expression.
        && resolving_expression_names_.empty();
}

CompiledVhdlTypeIdentityResolution
CompiledDesignResolver::resolve_vhdl_type_identity(
    std::string_view spelling,
    const std::optional<ScopeId> owner_scope) const
{
    if (!owner_scope || !resolutions_memoizable()) {
        return compute_vhdl_type_identity(spelling, owner_scope);
    }
    spelling = vhdl::without_builtin_prefix(spelling);
    auto& by_spelling = effective_->resolution_memo()
                            .type_identities[owner_scope->value()];
    if (const auto found = by_spelling.find(spelling);
        found != by_spelling.end()) {
        for (const auto& entry : found->second) {
            if (std::ranges::equal(entry.binding_frames, binding_frames_)) {
                return entry.result;
            }
        }
    }
    auto result = compute_vhdl_type_identity(spelling, owner_scope);
    // The computation can resolve other names and rehash the table.
    auto& entries = by_spelling.try_emplace(std::string { spelling })
                        .first->second;
    if (entries.size()
        < detail::ResolutionMemo::maximum_type_identity_contexts) {
        entries.push_back({
            std::vector<CompiledBindingFrame> {
                binding_frames_.begin(), binding_frames_.end()
            },
            result,
        });
    }
    return result;
}

CompiledVhdlTypeIdentityResolution
CompiledDesignResolver::compute_vhdl_type_identity(
    std::string_view spelling,
    const std::optional<ScopeId> owner_scope) const
{
    if (design_ == nullptr) {
        return { CompiledResolutionStatus::invalid, std::nullopt,
            vhdl::BuiltinTypeIdentity::none,
            CompiledVhdlStandardScalarIdentity::none };
    }
    spelling = vhdl::without_builtin_prefix(spelling);
    if (const auto type = resolve_vhdl_named_type(spelling, owner_scope)) {
        return { CompiledResolutionStatus::unique, type,
            vhdl::BuiltinTypeIdentity::none,
            CompiledVhdlStandardScalarIdentity::none };
    }

    auto lookup_scope = owner_scope;
    if (!lookup_scope) {
        const auto selected = design_->find_unit(selected_unit_);
        if (selected && selected->identity != nullptr) {
            lookup_scope = selected->identity->scope;
        }
    }
    if (!lookup_scope) {
        return { };
    }

    vhdl::Name source_name;
    source_name.spelling = spelling;
    source_name.canonical = spelling;
    const CompiledDeclarationPredicate named_type = [](const auto& view) {
        if (view.vhdl == nullptr) {
            return false;
        }
        using Form = vhdl::DeclarationForm;
        return view.vhdl->form == Form::type
            || view.vhdl->form == Form::subtype
            || view.vhdl->form == Form::generic_type
            || view.vhdl->form == Form::alias;
    };
    const auto visible_source = resolve_vhdl(
        source_name, *lookup_scope, named_type);
    if (visible_source.status != CompiledResolutionStatus::not_found) {
        CompiledVhdlTypeIdentityResolution result;
        result.status = visible_source.status;
        if (const auto declaration_id = visible_source.unique()) {
            const auto declaration = find_declaration(
                *design_, effective_, *declaration_id);
            if (declaration && declaration->vhdl != nullptr) {
                result.type = declaration->vhdl->declared_type;
                if (!result.type && declaration->vhdl->subtype
                    && declaration->vhdl->subtype->type_mark.target.valid()) {
                    result.type
                        = declaration->vhdl->subtype->type_mark.target;
                }
            }
            if (!result.type) {
                for (const auto& stored : design_->vhdl_hir.types()) {
                    const auto type
                        = find_type(*design_, effective_, stored.id);
                    if (!type || type->vhdl == nullptr
                        || type->vhdl->declaration != *declaration_id) {
                        continue;
                    }
                    if (result.type
                        && *result.type != type->vhdl->id) {
                        result.status
                            = CompiledResolutionStatus::ambiguous;
                        result.type.reset();
                        return result;
                    }
                    result.type = type->vhdl->id;
                }
            }
        }
        return result;
    }

    const auto* owner_scope_record
        = find_scope(*design_, *lookup_scope);
    const auto owner_unit = owner_scope_record != nullptr
        ? design_->find_unit(owner_scope_record->unit)
        : std::nullopt;
    const auto* owner_vhdl
        = owner_unit && owner_unit->vhdl != nullptr
        ? owner_unit->vhdl
        : nullptr;
    if (owner_vhdl == nullptr) {
        return { };
    }
    // SIGNED and UNSIGNED imported by use clauses from both IEEE numeric
    // packages, neither retained, are homographs: neither is directly
    // visible (IEEE 1076-2019 12.4).
    if ((same_vhdl_identifier(spelling, "signed")
            || same_vhdl_identifier(spelling, "unsigned"))
        && vhdl_builtin_package_member_imported(
            "ieee", "numeric_std", spelling, *lookup_scope)
        && vhdl_builtin_package_member_imported(
            "ieee", "numeric_bit", spelling, *lookup_scope)) {
        return { CompiledResolutionStatus::ambiguous, std::nullopt,
            vhdl::BuiltinTypeIdentity::none,
            CompiledVhdlStandardScalarIdentity::none };
    }
    // Do not turn a same-spelled source declaration of another class into a
    // compiler-owned type. This check intentionally uses the unfiltered
    // scoped resolver: the filtered lookup above has already preserved source
    // type identities and ambiguity.
    if (resolve_vhdl(source_name, *lookup_scope).status
        == CompiledResolutionStatus::not_found) {
        // IEEE 1076-2019 13.2 omits the implicit context clause for context
        // declarations and package STANDARD. The compiler currently stamps
        // the same predefined-environment record on every unit, so apply
        // those owner-kind exceptions before accepting an unqualified name.
        const bool standard_package_owner
            = owner_vhdl->kind == vhdl::UnitKind::package
            && same_vhdl_identifier(
                normalized_library(owner_vhdl->library), "std")
            && same_vhdl_identifier(owner_vhdl->name, "standard");
        const bool allow_unqualified_standard
            = owner_vhdl->kind != vhdl::UnitKind::context
            && !standard_package_owner;
        const auto scalar = builtin_standard_scalar_type_identity(
            spelling, owner_vhdl->predefined_environment,
            allow_unqualified_standard);
        if (scalar != CompiledVhdlStandardScalarIdentity::none) {
            return { CompiledResolutionStatus::unique, std::nullopt,
                vhdl::BuiltinTypeIdentity::none, scalar };
        }
    }
    const auto builtin = vhdl::standard_array_builtin_type_identity(
        spelling, owner_vhdl->predefined_environment);
    if (builtin == vhdl::BuiltinTypeIdentity::none) {
        return { };
    }
    return { CompiledResolutionStatus::unique, std::nullopt, builtin,
        CompiledVhdlStandardScalarIdentity::none };
}

std::optional<sv::TypeReference>
CompiledDesignResolver::effective_systemverilog_type(
    const sv::TypeReference& input, ScopeId use_scope) const
{
    if (design_ == nullptr) {
        return std::nullopt;
    }
    // `type(expression)` as a data type denotes the expression's type in
    // this specialization (IEEE 1800-2017 6.23).
    if (input.type_of && effective_ != nullptr) {
        const auto described = describe_systemverilog_expression_type(
            *effective_, *input.type_of);
        auto result = described ? systemverilog_type_reference(*described)
                                : std::nullopt;
        if (result) {
            result->unpacked_dimensions.insert(
                result->unpacked_dimensions.begin(),
                input.unpacked_dimensions.begin(),
                input.unpacked_dimensions.end());
        }
        return result;
    }

    const auto declaration_type = [&](const DeclarationId id)
        -> std::optional<sv::TypeReference> {
        const auto declaration = find_declaration(
            *design_, effective_, id);
        if (!declaration || declaration->systemverilog == nullptr) {
            return std::nullopt;
        }
        const auto& record = *declaration->systemverilog;
        auto result = record.form == sv::DeclarationForm::type_parameter
                && record.default_type
            ? record.default_type
            : record.type ? record.type : record.default_type;
        if (!result) {
            return std::nullopt;
        }
        const auto nominal_type = systemverilog_declared_type(
            *design_, effective_, record);
        if (record.form != sv::DeclarationForm::type_parameter
            && nominal_type) {
            result->target.target = *nominal_type;
            if (result->target.spelling.empty()) {
                result->target.spelling = record.name;
            }
        }
        return result;
    };
    const auto bound_type = [&](const DeclarationId formal)
        -> std::optional<sv::TypeReference> {
        for (auto frame = binding_frames_.rbegin();
             frame != binding_frames_.rend(); ++frame) {
            const auto binding = std::ranges::find(
                *frame, formal, &CompiledActualBinding::formal);
            if (binding == frame->end()) {
                continue;
            }
            if (binding->actual_declaration) {
                if (const auto result = declaration_type(
                        *binding->actual_declaration)) {
                    return result;
                }
            }
            if (binding->expression) {
                const auto expression = find_expression(
                    *design_, effective_, *binding->expression);
                if (expression
                    && expression->systemverilog != nullptr
                    && expression->systemverilog->referenced_name
                    && expression->systemverilog
                           ->referenced_name->selected) {
                    if (const auto result = declaration_type(
                            *expression->systemverilog
                                 ->referenced_name->selected)) {
                        return result;
                    }
                }
            }
            return binding->systemverilog_type;
        }
        if (effective_ == nullptr) {
            return std::nullopt;
        }
        const auto& actuals
            = effective_->specialization().actual_identities;
        const auto actual = std::ranges::find(
            actuals, formal,
            &SpecializedHirActualIdentity::declaration);
        if (actual == actuals.end()) {
            return std::nullopt;
        }
        if (actual->actual_declaration) {
            if (const auto result = declaration_type(
                    *actual->actual_declaration)) {
                return result;
            }
        }
        if (actual->systemverilog_type) {
            return actual->systemverilog_type;
        }
        if (actual->actual_expression) {
            const auto expression = effective_->find_expression(
                *actual->actual_expression);
            if (expression
                && expression->systemverilog != nullptr
                && expression->systemverilog->referenced_name
                && expression->systemverilog
                       ->referenced_name->selected) {
                return declaration_type(
                    *expression->systemverilog
                         ->referenced_name->selected);
            }
        }
        return std::nullopt;
    };
    const auto type_declaration = [&](const sv::TypeReference& reference,
                                      const ScopeId scope)
        -> std::optional<DeclarationId> {
        if (reference.target.target.valid()) {
            const auto fallback
                = find_type(*design_, effective_, reference.target.target);
            if (fallback && fallback->systemverilog != nullptr
                && fallback->systemverilog->declaration.valid()) {
                return fallback->systemverilog->declaration;
            }
            std::optional<DeclarationId> selected;
            for (const auto& declaration :
                design_->systemverilog_hir.declarations()) {
                if (declaration.declared_type
                    && *declaration.declared_type
                        == reference.target.target) {
                    if (selected && *selected != declaration.id) {
                        return std::nullopt;
                    }
                    selected = declaration.id;
                }
            }
            return selected;
        }
        if (reference.target.spelling.empty()) {
            return std::nullopt;
        }
        if (const auto named = resolve_systemverilog_named_type(
                reference.target.spelling, scope, false,
                reference.source_order)
                .unique()) {
            return named;
        }
        // A type parameter of a base class is visible in the derived class
        // (IEEE 1800-2017 8.13, 8.25); a class specialization's frame binds
        // the parameters of its whole base chain, derived first.
        for (auto frame = binding_frames_.rbegin();
             frame != binding_frames_.rend(); ++frame) {
            for (const auto& binding : *frame) {
                const auto declaration = find_declaration(
                    *design_, effective_, binding.formal);
                if (declaration && declaration->systemverilog != nullptr
                    && declaration->systemverilog->form
                        == sv::DeclarationForm::type_parameter
                    && declaration->systemverilog->name
                        == reference.target.spelling) {
                    return binding.formal;
                }
            }
        }
        return std::nullopt;
    };
    std::set<DeclarationId> visiting;
    const auto materialize = [&](const auto& self,
                                 sv::TypeReference candidate,
                                 const ScopeId scope)
        -> sv::TypeReference {
        auto selected = type_declaration(candidate, scope);
        if (const auto scoped = systemverilog_type_parameter_scoped_typedef(
                candidate, scope)) {
            std::vector<CompiledBindingFrame> frames {
                binding_frames_.begin(), binding_frames_.end()
            };
            frames.push_back(scoped->frame);
            const CompiledDesignResolver nested {
                *design_, selected_unit_, effective_, frames
            };
            if (auto resolved = nested.effective_systemverilog_type(
                    scoped->type, scoped->scope)) {
                candidate = retain_systemverilog_declarator(
                    std::move(*resolved), candidate);
                selected.reset();
            }
        }
        if (selected && visiting.insert(*selected).second) {
            const auto declaration = find_declaration(
                *design_, effective_, *selected);
            if (declaration
                && declaration->systemverilog != nullptr) {
                const auto& record = *declaration->systemverilog;
                if (record.form
                    == sv::DeclarationForm::type_parameter) {
                    const auto bound = bound_type(record.id);
                    auto replacement = bound
                        ? bound
                        : record.default_type;
                    if (replacement) {
                        auto resolved = self(self, *replacement,
                            bound ? scope : record.scope);
                        candidate = retain_systemverilog_declarator(
                            std::move(resolved), candidate, true);
                    }
                } else {
                    const auto nominal_target = candidate.target;
                    const auto declared = record.type
                        ? record.type
                        : record.default_type;
                    if (declared) {
                        auto resolved = self(
                            self, *declared, record.scope);
                        candidate = retain_systemverilog_declarator(
                            std::move(resolved), candidate);
                    }
                    const auto nominal_type = systemverilog_declared_type(
                        *design_, effective_, record);
                    if (nominal_type) {
                        candidate.target.target
                            = *nominal_type;
                        candidate.target.source = nominal_target.source;
                        candidate.target.spelling
                            = nominal_target.spelling.empty()
                            ? record.name
                            : nominal_target.spelling;
                        const auto fallback
                            = find_type(*design_, effective_, *nominal_type);
                        if (fallback
                            && fallback->systemverilog != nullptr
                            && (!candidate.value_form
                                || *candidate.value_form
                                    == sv::TypeForm::unresolved)) {
                            candidate.value_form
                                = fallback->systemverilog->form;
                        }
                    }
                }
            }
            visiting.erase(*selected);
        }

        for (auto& element : candidate.container_element_types) {
            element = self(self, std::move(element), scope);
        }
        return candidate;
    };

    return materialize(materialize, input, use_scope);
}

std::optional<CompiledClassScopedTypedef>
CompiledDesignResolver::systemverilog_type_parameter_scoped_typedef(
    const sv::TypeReference& input, const ScopeId use_scope) const
{
    const auto& spelling = input.target.spelling;
    const auto separator = spelling.find("::");
    if (design_ == nullptr || separator == std::string::npos
        || separator == 0U
        || spelling.find("::", separator + 2U) != std::string::npos) {
        return std::nullopt;
    }
    sv::TypeReference qualifier;
    qualifier.target.spelling = spelling.substr(0U, separator);
    qualifier.source_order = input.source_order;
    const auto formal = resolve_systemverilog_named_type(
        qualifier.target.spelling, use_scope, false, input.source_order)
                            .unique();
    const auto formal_declaration = formal
        ? find_declaration(*design_, effective_, *formal)
        : std::nullopt;
    if (!formal_declaration || formal_declaration->systemverilog == nullptr
        || formal_declaration->systemverilog->form
            != sv::DeclarationForm::type_parameter) {
        return std::nullopt;
    }
    const auto owner = effective_systemverilog_type(qualifier, use_scope);
    if (!owner
        || (owner->class_identity.empty()
            && owner->target.spelling.empty())) {
        return std::nullopt;
    }
    // A class type actual bound before class resolution keeps only its
    // spelling; it names the one class so called.
    const auto& classes = design_->systemverilog_hir.classes();
    auto selected_class = classes.end();
    for (auto candidate = classes.begin(); candidate != classes.end();
         ++candidate) {
        const bool matches = owner->class_identity.empty()
            ? !candidate->forward_declaration
                && candidate->name == owner->target.spelling
            : sv::class_declaration_identity(*candidate)
                == owner->class_identity;
        if (!matches) {
            continue;
        }
        if (selected_class != classes.end()) {
            return std::nullopt;
        }
        selected_class = candidate;
    }
    if (selected_class == classes.end()) {
        return std::nullopt;
    }
    const auto name = std::string_view { spelling }.substr(separator + 2U);
    const sv::Declaration* alias { };
    for (const auto id : selected_class->type_aliases) {
        const auto declaration = find_declaration(*design_, effective_, id);
        if (declaration && declaration->systemverilog != nullptr
            && declaration->systemverilog->name == name
            && declaration->systemverilog->type) {
            alias = declaration->systemverilog;
            break;
        }
    }
    if (alias == nullptr) {
        return std::nullopt;
    }
    CompiledClassScopedTypedef result {
        *alias->type, alias->scope, { }
    };
    const auto& actuals = owner->interface_parameter_actuals;
    for (std::size_t index { }; index < selected_class->parameters.size();
         ++index) {
        const auto& parameter = selected_class->parameters[index];
        const auto named = std::ranges::find_if(actuals,
            [&](const sv::ActualAssociation& association) {
                return association.formal
                    && *association.formal == parameter.name;
            });
        const auto* association = named != actuals.end() ? &*named
            : index < actuals.size() && !actuals[index].formal
            ? &actuals[index]
            : nullptr;
        if (association == nullptr) {
            continue;
        }
        CompiledActualBinding binding;
        binding.formal = parameter.declaration;
        if (association->kind == sv::ActualKind::type && association->type) {
            binding.systemverilog_type = association->type;
        } else if (association->kind == sv::ActualKind::expression
            && association->expression) {
            binding.expression = association->expression;
        } else {
            continue;
        }
        result.frame.push_back(std::move(binding));
    }
    return result;
}

std::optional<sv::TypeReference>
CompiledDesignResolver::underlying_systemverilog_type(
    const sv::TypeReference& input, const ScopeId use_scope) const
{
    if (design_ == nullptr) {
        return std::nullopt;
    }

    auto current = effective_systemverilog_type(input, use_scope)
                       .value_or(input);
    std::set<TypeId> visiting;
    while (current.target.target.valid()
        && visiting.insert(current.target.target).second) {
        const auto definition
            = find_type(*design_, effective_, current.target.target);
        if (!definition || definition->systemverilog == nullptr) {
            break;
        }
        const auto form = definition->systemverilog->form;
        if (form == sv::TypeForm::packed_structure
            || form == sv::TypeForm::packed_union
            || form == sv::TypeForm::tagged_union
            || form == sv::TypeForm::unpacked_structure
            || form == sv::TypeForm::unpacked_union) {
            break;
        }
        // The typedef's base names a type in the typedef's own scope
        // (IEEE 1800-2017 6.18).
        const auto alias = find_declaration(
            *design_, effective_, definition->systemverilog->declaration);
        auto replacement = effective_systemverilog_type(
                               definition->systemverilog->base,
                               alias && alias->systemverilog != nullptr
                                   ? alias->systemverilog->scope
                                   : use_scope)
                               .value_or(
                                   definition->systemverilog->base);
        current = retain_systemverilog_declarator(
            std::move(replacement), current);
    }
    return current;
}

std::optional<std::int64_t>
CompiledDesignResolver::vhdl_enumeration_literal_ordinal(
    const TypeId expected_type, const ExpressionId expression) const
{
    if (design_ == nullptr || !expected_type.valid()) {
        return std::nullopt;
    }
    const auto candidate = find_expression(*design_, effective_, expression);
    // TRUE and FALSE may be literals of a user enumeration too; the
    // expected type selects the overload (IEEE 1076-2008 5.2.2.1, 12.5).
    if (!candidate || candidate->vhdl == nullptr
        || (candidate->vhdl->kind != vhdl::ExpressionKind::name
            && candidate->vhdl->kind != vhdl::ExpressionKind::logic_literal
            && candidate->vhdl->kind
                != vhdl::ExpressionKind::boolean_literal)) {
        return std::nullopt;
    }
    // A selected name (`pkg.literal`) denotes the literal by its simple
    // name.
    const auto spelling = std::string_view { candidate->vhdl->text };
    const auto matches = [&](const std::string_view literal) {
        return spelling.starts_with('\'')
            ? spelling == literal
            : same_vhdl_identifier(simple_vhdl_name(spelling), literal);
    };
    std::set<TypeId> visiting;
    const auto resolve = [&](const auto& self, const TypeId type_id)
        -> std::optional<std::int64_t> {
        if (!type_id.valid() || !visiting.insert(type_id).second) {
            return std::nullopt;
        }
        const auto fallback = find_type(*design_, effective_, type_id);
        if (!fallback || fallback->vhdl == nullptr) {
            return std::nullopt;
        }
        const auto literal = std::ranges::find_if(
            fallback->vhdl->enumeration_literals,
            [&](const vhdl::EnumerationLiteral& value) {
                return matches(value.spelling);
            });
        if (literal != fallback->vhdl->enumeration_literals.end()) {
            return static_cast<std::int64_t>(literal->ordinal);
        }
        return fallback->vhdl->base.type_mark.target.valid()
            ? self(self, fallback->vhdl->base.type_mark.target)
            : std::nullopt;
    };
    return resolve(resolve, expected_type);
}

std::optional<vhdl::SubtypeIndication>
CompiledDesignResolver::effective_vhdl_subtype(
    const vhdl::SubtypeIndication& input, const ScopeId use_scope) const
{
    if (!binding_frames_.empty() || !resolutions_memoizable()) {
        return compute_effective_vhdl_subtype(input, use_scope);
    }
    auto key = std::hash<std::string_view> { }(input.type_mark.spelling);
    key = key * 31U + use_scope.value();
    key = key * 31U + input.type_mark.target.value();
    auto& memo = effective_->resolution_memo();
    if (const auto found = memo.effective_subtypes.find(key);
        found != memo.effective_subtypes.end()) {
        for (const auto& entry : found->second) {
            if (entry.scope == use_scope && entry.input == input) {
                return entry.result;
            }
        }
    }
    auto result = compute_effective_vhdl_subtype(input, use_scope);
    if (memo.effective_subtype_count
        < detail::ResolutionMemo::maximum_effective_subtypes) {
        // The computation can resolve other subtypes and rehash the table.
        memo.effective_subtypes[key].push_back({ use_scope, input, result });
        ++memo.effective_subtype_count;
    }
    return result;
}

std::optional<vhdl::SubtypeIndication>
CompiledDesignResolver::compute_effective_vhdl_subtype(
    const vhdl::SubtypeIndication& input, const ScopeId use_scope) const
{
    if (design_ == nullptr) {
        return std::nullopt;
    }
    // A type mark that analysis could not resolve (a selected name such as
    // `work.pkg.t`, or an instance-qualified declaration identity) is bound
    // now that every unit is known; the effective subtype keeps a binding.
    const auto relink = [&](const TypeId resolved)
        -> std::optional<vhdl::SubtypeIndication> {
        auto relinked = input;
        relinked.type_mark.target = resolved;
        auto effective = effective_vhdl_subtype(relinked, use_scope);
        if (!effective) {
            return relinked;
        }
        if (!effective->type_mark.target.valid()
            && effective->builtin_type == vhdl::BuiltinTypeIdentity::none
            && !effective->type_mark.spelling.empty()) {
            effective->type_mark.target = resolve_vhdl_named_type(
                effective->type_mark.spelling, use_scope)
                                              .value_or(resolved);
        }
        return effective;
    };
    if (input.type_mark.target.valid() && !input.type_mark.spelling.empty()) {
        const auto marked = find_type(*design_, effective_,
            input.type_mark.target);
        if (marked && marked->vhdl != nullptr
            && marked->vhdl->form == vhdl::TypeForm::unresolved) {
            if (const auto resolved = resolve_vhdl_named_type(
                    input.type_mark.spelling, use_scope);
                resolved && *resolved != input.type_mark.target) {
                return relink(*resolved);
            }
        }
    } else if (!input.type_mark.target.valid()
        && input.builtin_type == vhdl::BuiltinTypeIdentity::none
        && parse_vhdl_declaration_identity(input.type_mark.spelling)) {
        if (const auto resolved = resolve_vhdl_named_type(
                input.type_mark.spelling, use_scope)) {
            return relink(*resolved);
        }
    }

    const auto evaluate_boundary = [&](
        const std::optional<std::int64_t> folded,
        const std::optional<ExpressionId> residual,
        const std::span<const CompiledBindingFrame> active) {
        if (folded) {
            return folded;
        }
        auto value = residual && effective_ != nullptr
            ? effective_->evaluate_integral_expression(
                  *residual, vhdl_frames_integral_binding(active))
            : std::nullopt;
        if (!value && residual && boundary_fallback_ != nullptr
            && *boundary_fallback_) {
            value = (*boundary_fallback_)(*residual);
        }
        return value;
    };
    // A generic integral fold is not a substitute for an enum ordinal. Keep
    // unresolved IDs only within this resolver call and exact active binding
    // context, so one specialization cannot poison another.
    struct EnumBoundaryMaterialization {
        std::vector<CompiledBindingFrame> active_frames;
        std::set<ExpressionId> unresolved;
    };
    std::vector<EnumBoundaryMaterialization> enum_boundary_materializations;
    const auto unresolved_enum_boundaries_for = [&](
        const std::span<const CompiledBindingFrame> active)
        -> std::set<ExpressionId>& {
        const auto found = std::ranges::find_if(
            enum_boundary_materializations,
            [&](const EnumBoundaryMaterialization& entry) {
                return std::ranges::equal(entry.active_frames, active);
            });
        if (found != enum_boundary_materializations.end()) {
            return found->unresolved;
        }
        enum_boundary_materializations.push_back({
            std::vector<CompiledBindingFrame> {
                active.begin(), active.end()
            },
            { }
        });
        return enum_boundary_materializations.back().unresolved;
    };
    std::function<void(vhdl::SubtypeIndication&,
        std::span<const CompiledBindingFrame>)> materialize_boundaries;
    const auto standard_width = [&]() -> std::uint8_t {
        const auto selected = design_->find_unit(selected_unit_);
        return selected && selected->vhdl != nullptr
                && selected->vhdl->standard == "2019"
            ? 64U
            : 32U;
    };
    const auto concrete_array_range_length = [](
        const vhdl::RangeConstraint& range,
        const std::int64_t left,
        const std::int64_t right)
        -> std::optional<std::uint64_t> {
        if (range.direction_from_bounds) {
            // Equal bounds are non-null for either direction. Unequal bounds
            // still need the referenced range's direction; endpoint order
            // cannot distinguish a null range from its non-null counterpart.
            if (left != right) {
                return std::nullopt;
            }
            return std::uint64_t { 1U };
        }
        if (range.null) {
            return std::uint64_t { 0U };
        }
        const bool null_range = range.descending
            ? left < right
            : left > right;
        if (null_range) {
            return std::uint64_t { 0U };
        }
        const auto distance = support::index_distance(left, right);
        if (distance == std::numeric_limits<std::uint64_t>::max()) {
            return std::nullopt;
        }
        return distance + 1U;
    };
    const auto complete_predefined = [&](
        vhdl::SubtypeIndication& result,
        const std::span<const CompiledBindingFrame> active,
        const ScopeId subtype_scope) {
        materialize_boundaries(result, active);
        const auto name = simple_vhdl_name(result.type_mark.spelling);
        if (result.builtin_type == vhdl::BuiltinTypeIdentity::none
            && !result.type_mark.target.valid()) {
            auto spelling = std::string_view {
                result.type_mark.spelling
            };
            spelling = vhdl::without_builtin_prefix(spelling);
            const auto resolved = CompiledDesignResolver {
                *design_, selected_unit_, effective_, active
            }.resolve_vhdl_type_identity(spelling, subtype_scope);
            if (resolved.status == CompiledResolutionStatus::unique
                && resolved.builtin_type
                    != vhdl::BuiltinTypeIdentity::none) {
                result.builtin_type = resolved.builtin_type;
            }
        }
        const auto stamp_builtin_vector_identity = [&]() {
            if (result.builtin_type != vhdl::BuiltinTypeIdentity::none
                || result.type_mark.target.valid()
                || result.domain != vhdl::ValueDomain::logic9) {
                return;
            }
            auto type_name = std::string_view {
                result.type_mark.spelling
            };
            type_name = vhdl::without_builtin_prefix(type_name);
            if (type_name.empty()
                || type_name.find_first_of(".:") != std::string_view::npos) {
                return;
            }
            const auto declarations
                = design_->vhdl_type_declarations_named(type_name);
            if (!declarations || !declarations->empty()
                || !vhdl_builtin_package_member_imported("ieee",
                    "std_logic_1164", type_name, subtype_scope)) {
                return;
            }
            if (same_vhdl_identifier(type_name, "std_logic_vector")) {
                result.builtin_type
                    = vhdl::BuiltinTypeIdentity::ieee_std_logic_1164_std_logic_vector;
            } else if (same_vhdl_identifier(
                           type_name, "std_ulogic_vector")) {
                result.builtin_type
                    = vhdl::BuiltinTypeIdentity::ieee_std_logic_1164_std_ulogic_vector;
            }
        };
        if (same_vhdl_identifier(name, "integer")
            || same_vhdl_identifier(name, "natural")
            || same_vhdl_identifier(name, "positive")
            || same_vhdl_identifier(name, "universal_integer")
            || same_vhdl_identifier(name, "time")) {
            // A compiled subtype can deliberately retain a storage width
            // that differs from the language's predefined INTEGER width.
            // TIME and compiler-owned VITAL aliases are the important case:
            // their HIR layout is 64 bits even in a VHDL-2008 unit.  Keep
            // that compiled layout authoritative while completing only the
            // missing predefined properties.
            const auto width = result.executable_width
                    && *result.executable_width != 0U
                ? static_cast<std::uint8_t>(*result.executable_width)
                : result.integer_storage_width != 0U
                ? result.integer_storage_width
                : same_vhdl_identifier(name, "time")
                ? std::uint8_t { 64U }
                : standard_width();
            result.domain = vhdl::ValueDomain::integer;
            result.executable_width = width;
            result.integer_storage_width = width;
            result.signed_value = true;
            return true;
        } else if (same_vhdl_identifier(name, "boolean")) {
            result.domain = vhdl::ValueDomain::boolean;
            result.executable_width = 1U;
            return true;
        } else if (same_vhdl_identifier(name, "real")) {
            // STD.STANDARD.REAL is held as a 64-bit IEEE double.
            result.domain = vhdl::ValueDomain::bit2;
            result.executable_width = 64U;
            result.signed_value = true;
            return true;
        } else if (same_vhdl_identifier(name, "bit")) {
            result.domain = vhdl::ValueDomain::bit2;
            result.executable_width = 1U;
            return true;
        } else if (same_vhdl_identifier(name, "std_logic")
            || same_vhdl_identifier(name, "std_ulogic")) {
            result.domain = vhdl::ValueDomain::logic9;
            result.executable_width = 1U;
            return true;
        } else if (same_vhdl_identifier(name, "character")) {
            result.domain = vhdl::ValueDomain::bit2;
            result.executable_width = 8U;
            return true;
        } else if (same_vhdl_identifier(name, "time_record")) {
            result.domain = vhdl::ValueDomain::bit2;
            result.executable_width = 515U;
            return true;
        } else if (same_vhdl_identifier(name, "dayofweek")) {
            result.domain = vhdl::ValueDomain::bit2;
            result.executable_width = 3U;
            return true;
        } else if (const auto* enumeration
                   = support::find_vhdl_standard_enumeration(name)) {
            // Predefined STD.STANDARD enumerations (IEEE 1076-2019 16.3).
            result.domain = vhdl::ValueDomain::bit2;
            result.executable_width
                = support::vhdl_standard_enumeration_width(*enumeration);
            return true;
        } else if (same_vhdl_identifier(name, "type_class")
            || same_vhdl_identifier(name, "value_class")) {
            result.domain = vhdl::ValueDomain::bit2;
            result.executable_width = 4U;
            return true;
        } else if (name.size() >= std::string_view { "_mirror" }.size()
            && same_vhdl_identifier(
                name.substr(name.size()
                    - std::string_view { "_mirror" }.size()),
                "_mirror")) {
            result.domain = vhdl::ValueDomain::bit2;
            result.executable_width = 32U;
            return true;
        } else {
            std::uint64_t element_width { };
            auto element_domain = vhdl::ValueDomain::unknown;
            switch (result.builtin_type) {
            case vhdl::BuiltinTypeIdentity::std_standard_bit_vector:
                element_width = 1U;
                element_domain = vhdl::ValueDomain::bit2;
                break;
            case vhdl::BuiltinTypeIdentity::std_standard_boolean_vector:
                element_width = 1U;
                element_domain = vhdl::ValueDomain::boolean;
                break;
            case vhdl::BuiltinTypeIdentity::std_standard_string:
                element_width = 8U;
                element_domain = vhdl::ValueDomain::string;
                break;
            case vhdl::BuiltinTypeIdentity::std_standard_integer_vector:
                element_width = standard_width();
                element_domain = vhdl::ValueDomain::integer;
                break;
            case vhdl::BuiltinTypeIdentity::std_standard_time_vector:
                element_width = 64U;
                element_domain = vhdl::ValueDomain::integer;
                break;
            case vhdl::BuiltinTypeIdentity::std_standard_real_vector:
                // REAL elements are IEEE doubles (IEEE 1076-2008 16.3).
                element_width = 64U;
                element_domain = vhdl::ValueDomain::bit2;
                break;
            default:
                break;
            }
            if (element_width == 0U
                && (same_vhdl_identifier(name, "std_logic_vector")
                || same_vhdl_identifier(name, "std_ulogic_vector")
                || same_vhdl_identifier(name, "signed")
                || same_vhdl_identifier(name, "unsigned")
                || same_vhdl_identifier(name, "ufixed")
                || same_vhdl_identifier(name, "sfixed")
                || same_vhdl_identifier(name, "unresolved_ufixed")
                || same_vhdl_identifier(name, "unresolved_sfixed")
                || same_vhdl_identifier(name, "float")
                || same_vhdl_identifier(name, "unresolved_float")
                || same_vhdl_identifier(name, "u_float"))) {
                element_width = 1U;
                element_domain = vhdl::ValueDomain::logic9;
            }
            if (element_width == 0U
                && result.builtin_type == vhdl::BuiltinTypeIdentity::none
                && same_vhdl_identifier(name, "string")
                && result.type_mark.target.valid()) {
                const auto shadow
                    = find_type(*design_, effective_, result.type_mark.target);
                if (shadow && shadow->vhdl != nullptr
                    && shadow->vhdl->form == vhdl::TypeForm::array
                    && same_vhdl_identifier(shadow->vhdl->name, "string")) {
                    // Preserve the legacy representation fallback for a
                    // resolved source array named STRING without assigning
                    // the predefined STD.STANDARD.STRING identity.
                    element_width = 8U;
                    element_domain = vhdl::ValueDomain::string;
                }
            }
            if (element_width == 0U) {
                return false;
            }
            if (result.domain == vhdl::ValueDomain::unknown) {
                result.domain = element_domain;
            }
            stamp_builtin_vector_identity();
            // The IEEE fixed_pkg and float_pkg array types are compiler
            // owned as well, with no retained declaration.
            const bool compiler_owned_fixed_or_float
                = result.builtin_type == vhdl::BuiltinTypeIdentity::none
                && (same_vhdl_identifier(name, "ufixed")
                    || same_vhdl_identifier(name, "sfixed")
                    || same_vhdl_identifier(name, "unresolved_ufixed")
                    || same_vhdl_identifier(name, "unresolved_sfixed")
                    || same_vhdl_identifier(name, "float")
                    || same_vhdl_identifier(name, "unresolved_float")
                    || same_vhdl_identifier(name, "u_float"));
            const bool compiler_owned_unconstrained_logic_vector
                = !result.type_mark.target.valid()
                && (result.builtin_type
                        == vhdl::BuiltinTypeIdentity::ieee_std_logic_1164_std_logic_vector
                    || result.builtin_type
                        == vhdl::BuiltinTypeIdentity::ieee_std_logic_1164_std_ulogic_vector
                    || compiler_owned_fixed_or_float)
                && result.constraints.empty();
            if (compiler_owned_unconstrained_logic_vector
                && result.executable_width
                && *result.executable_width == element_width) {
                // The parser represents a bare Logic9 type mark with scalar
                // width one. For these imported IEEE array types that value
                // is only the element-width placeholder, not a constrained
                // one-bit array subtype.
                result.unconstrained = true;
                result.executable_width.reset();
            }
            if (result.constraints.empty()) {
                if (!result.executable_width
                    || *result.executable_width == 0U) {
                    result.unconstrained = true;
                    result.executable_width.reset();
                }
                return true;
            }
            std::uint64_t count { 1U };
            for (const auto& constraint : result.constraints) {
                const auto left = evaluate_boundary(
                    constraint.left, constraint.left_expression,
                    active);
                const auto right = evaluate_boundary(
                    constraint.right, constraint.right_expression,
                    active);
                if (!left || !right) {
                    result.executable_width.reset();
                    return true;
                }
                const auto dimension = concrete_array_range_length(
                    constraint, *left, *right);
                if (!dimension) {
                    result.executable_width.reset();
                    return true;
                }
                if (*dimension != 0U
                    && count
                        > std::numeric_limits<std::uint64_t>::max()
                            / *dimension) {
                    result.executable_width.reset();
                    return true;
                }
                count *= *dimension;
            }
            if (element_width != 0U
                && count > std::numeric_limits<std::uint64_t>::max()
                        / element_width) {
                result.executable_width.reset();
                return true;
            }
            result.unconstrained = false;
            result.executable_width = count * element_width;
            return true;
        }
    };
    auto frames = std::vector<CompiledBindingFrame> {
        binding_frames_.begin(), binding_frames_.end()
    };
    std::set<TypeId> active_types;
    std::set<DeclarationId> active_generics;
    const auto merge_inherited = [](vhdl::SubtypeIndication& result,
                                    const vhdl::SubtypeIndication& inherited) {
        const bool owns_constraints = !result.constraints.empty();
        const bool owns_width = result.executable_width
            && *result.executable_width != 0U;
        if (result.domain == vhdl::ValueDomain::unknown) {
            result.domain = inherited.domain;
        }
        if (!result.executable_width || *result.executable_width == 0U) {
            result.executable_width = inherited.executable_width;
        }
        if (result.integer_storage_width == 0U) {
            result.integer_storage_width = inherited.integer_storage_width;
        }
        if (result.constraints.empty()) {
            result.constraints = inherited.constraints;
        }
        if (result.resolution_function.spelling.empty()
            && result.resolution_function.canonical.empty()) {
            result.resolution_function = inherited.resolution_function;
        }
        if (result.builtin_type == vhdl::BuiltinTypeIdentity::none) {
            result.builtin_type = inherited.builtin_type;
        }
        if (!inherited.element_constraints.empty()) {
            result.element_constraints = inherited.element_constraints;
        }
        result.signed_value = result.signed_value || inherited.signed_value;
        // `unconstrained` is part of the effective array shape, not a
        // nominal property of the outer subtype mark.  Generic-type and
        // named-subtype substitution can supply the first concrete shape;
        // retaining the outer placeholder's `true` value then causes
        // aggregates and callable defaults to be rejected despite complete
        // inherited bounds.
        result.unconstrained = owns_constraints || owns_width
            ? false
            : inherited.unconstrained;
    };
    const auto bound_type = [&](const DeclarationId formal,
                                const std::span<
                                    const CompiledBindingFrame> active,
                                const bool prefer_actual_subtype)
        -> std::optional<vhdl::SubtypeIndication> {
        const auto declaration_type = [&](const DeclarationId id)
            -> std::optional<vhdl::SubtypeIndication> {
            const auto declaration = find_declaration(
                *design_, effective_, id);
            if (!declaration || declaration->vhdl == nullptr) {
                return std::nullopt;
            }
            if (declaration->vhdl->subtype) {
                return declaration->vhdl->subtype;
            }
            if (declaration->vhdl->declared_type) {
                vhdl::SubtypeIndication result;
                result.type_mark.target = *declaration->vhdl->declared_type;
                result.type_mark.spelling = declaration->vhdl->name;
                return result;
            }
            return declaration->vhdl->default_type;
        };
        // A generic type of an enclosing instance as the actual (`t => t`
        // in a generic package body) denotes that generic, which an outer
        // frame binds (IEEE 1076-2008 4.9, 6.5.3).
        auto current = formal;
        for (std::size_t hop = 0U; hop < 8U; ++hop) {
            std::optional<DeclarationId> outer;
            for (const auto& frame : active) {
                const auto binding = std::ranges::find(
                    frame, current, &CompiledActualBinding::formal);
                if (binding == frame.end() || !binding->actual_declaration
                    || *binding->actual_declaration == current
                    || (binding->vhdl_type
                        && binding->vhdl_type->type_mark.target.valid())) {
                    continue;
                }
                const auto actual = find_declaration(
                    *design_, effective_, *binding->actual_declaration);
                if (actual && actual->vhdl != nullptr
                    && actual->vhdl->form
                        == vhdl::DeclarationForm::generic_type) {
                    outer = *binding->actual_declaration;
                }
            }
            const bool outer_bound = outer
                && std::ranges::any_of(active, [&](const auto& frame) {
                       return std::ranges::find(frame, *outer,
                                  &CompiledActualBinding::formal)
                           != frame.end();
                   });
            if (!outer_bound) {
                break;
            }
            current = *outer;
        }
        for (auto frame = active.rbegin(); frame != active.rend(); ++frame) {
            const auto binding = std::ranges::find(
                *frame, current, &CompiledActualBinding::formal);
            if (binding == frame->end()) {
                continue;
            }
            if (!prefer_actual_subtype && binding->vhdl_type) {
                return binding->vhdl_type;
            }
            if (binding->actual_declaration) {
                return declaration_type(*binding->actual_declaration);
            }
            // An actual expression's own subtype, when the binding carries
            // it, is the shape a name of it would only approximate.
            if (binding->expression && !binding->vhdl_type) {
                const auto resolution = CompiledDesignResolver {
                    *design_, selected_unit_, effective_, active
                }.resolve_expression_name(*binding->expression);
                if (const auto selected = resolution.unique()) {
                    return declaration_type(*selected);
                }
            }
            if (prefer_actual_subtype) {
                return binding->vhdl_type;
            }
            return std::nullopt;
        }
        if (effective_ == nullptr) {
            return std::nullopt;
        }
        const auto& actuals = effective_->specialization().actual_identities;
        const auto actual = std::ranges::find(
            actuals, current, &SpecializedHirActualIdentity::declaration);
        if (actual == actuals.end()) {
            return std::nullopt;
        }
        if (!prefer_actual_subtype && actual->vhdl_type) {
            return actual->vhdl_type;
        }
        if (actual->actual_declaration) {
            return declaration_type(*actual->actual_declaration);
        }
        if (actual->actual_expression) {
            const auto resolution = CompiledDesignResolver {
                *design_, selected_unit_, effective_, active
            }.resolve_expression_name(*actual->actual_expression);
            if (const auto selected = resolution.unique()) {
                return declaration_type(*selected);
            }
        }
        if (prefer_actual_subtype) {
            return actual->vhdl_type;
        }
        return std::nullopt;
    };

    std::function<std::optional<vhdl::SubtypeIndication>(
        vhdl::SubtypeIndication, ScopeId,
        std::vector<CompiledBindingFrame>&)> resolve;
    std::function<void(vhdl::SubtypeIndication&,
        const vhdl::SubtypeIndication&, std::vector<std::string>&,
        std::size_t, std::vector<CompiledBindingFrame>&)>
        close_instance_elements;
    using RangeDirectionPath
        = std::set<std::pair<std::uint32_t, std::uint32_t>>;
    std::function<std::optional<bool>(
        const vhdl::RangeConstraint&,
        std::span<const CompiledBindingFrame>, RangeDirectionPath&)>
        range_source_direction;
    range_source_direction = [&](
        const vhdl::RangeConstraint& constraint,
        const std::span<const CompiledBindingFrame> active,
        RangeDirectionPath& path) -> std::optional<bool> {
        if (!constraint.direction_from_bounds) {
            return constraint.descending;
        }
        if (!constraint.left_expression || !constraint.right_expression) {
            return std::nullopt;
        }
        constexpr std::size_t maximum_range_direction_depth = 32U;
        if (path.size() >= maximum_range_direction_depth) {
            return std::nullopt;
        }
        const auto path_key = std::pair {
            constraint.left_expression->value(),
            constraint.right_expression->value()
        };
        if (!path.insert(path_key).second) {
            return std::nullopt;
        }
        const auto finish = [&](const std::optional<bool> result) {
            path.erase(path_key);
            return result;
        };
        const auto left_expression = find_expression(
            *design_, effective_, *constraint.left_expression);
        const auto right_expression = find_expression(
            *design_, effective_, *constraint.right_expression);
        if (!left_expression || left_expression->vhdl == nullptr
            || !right_expression || right_expression->vhdl == nullptr
            || left_expression->vhdl->kind != vhdl::ExpressionKind::call
            || right_expression->vhdl->kind != vhdl::ExpressionKind::call) {
            return finish(std::nullopt);
        }
        const auto& left_call = *left_expression->vhdl;
        const auto& right_call = *right_expression->vhdl;
        const bool reverse_range = left_call.text == "'right"
            && right_call.text == "'left";
        if ((!reverse_range
                && (left_call.text != "'left"
                    || right_call.text != "'right"))
            || left_call.operands.size() != 1U
            || right_call.operands.size() != 1U) {
            return finish(std::nullopt);
        }
        // `obj'element.e'left`: the selected element's range (IEEE
        // 1076-2008 16.2.3, 8.3) under the object's element constraints.
        // An indexed name `obj(i)'left` denotes an element of the array
        // object in the same way (IEEE 1076-2008 8.4).
        const auto selection_steps = [&](ExpressionId id) {
            std::vector<std::string> steps;
            for (std::size_t depth { }; depth < 16U; ++depth) {
                const auto selected = find_expression(*design_, effective_, id);
                if (selected && selected->vhdl != nullptr
                    && selected->vhdl->kind == vhdl::ExpressionKind::index
                    && selected->vhdl->operands.size() >= 2U) {
                    steps.insert(steps.begin(), "'element");
                    id = selected->vhdl->operands.front();
                    continue;
                }
                if (!selected || selected->vhdl == nullptr
                    || selected->vhdl->kind != vhdl::ExpressionKind::call
                    || selected->vhdl->operands.size() != 1U
                    || (selected->vhdl->text != "'element"
                        && !selected->vhdl->text.starts_with(
                            "@vhdl-member:"))) {
                    break;
                }
                steps.insert(steps.begin(), selected->vhdl->text);
                id = selected->vhdl->operands.front();
            }
            return std::pair { id, std::move(steps) };
        };
        const auto [prefix_id, prefix_steps]
            = selection_steps(left_call.operands.front());
        const auto [paired_prefix_id, paired_steps]
            = selection_steps(right_call.operands.front());
        if (prefix_steps != paired_steps) {
            return finish(std::nullopt);
        }
        const auto prefix = find_expression(*design_, effective_, prefix_id);
        const auto paired_prefix
            = find_expression(*design_, effective_, paired_prefix_id);
        if (!prefix || prefix->vhdl == nullptr
            || prefix->vhdl->kind != vhdl::ExpressionKind::name
            || !paired_prefix || paired_prefix->vhdl == nullptr
            || paired_prefix->vhdl->kind != vhdl::ExpressionKind::name) {
            return finish(std::nullopt);
        }
        const auto resolver = CompiledDesignResolver {
            *design_, selected_unit_, effective_, active
        };
        // `obj.e'left`: an element of a record object written as a selected
        // name (IEEE 1076-2008 8.3) is the object's member.
        auto steps = prefix_steps;
        const auto object_member = [&](const vhdl::Expression& name)
            -> std::optional<std::pair<DeclarationId,
                std::vector<std::string>>> {
            const auto parts = vhdl_name_parts(name.text);
            if (parts.size() < 2U) {
                return std::nullopt;
            }
            vhdl::Name head;
            head.spelling = std::string { parts[0] };
            head.canonical = head.spelling;
            const auto object
                = resolver.resolve_vhdl(head, name.scope).unique();
            const auto declaration = object
                ? find_declaration(*design_, effective_, *object)
                : std::nullopt;
            if (!declaration || declaration->vhdl == nullptr) {
                return std::nullopt;
            }
            switch (declaration->vhdl->form) {
            case vhdl::DeclarationForm::variable:
            case vhdl::DeclarationForm::constant:
            case vhdl::DeclarationForm::signal:
            case vhdl::DeclarationForm::port:
            case vhdl::DeclarationForm::generic_constant:
                break;
            default:
                return std::nullopt;
            }
            std::vector<std::string> members;
            for (std::size_t index = 1U; index < parts.size(); ++index) {
                members.push_back(
                    "@vhdl-member:" + std::string { parts[index] });
            }
            return std::pair { *object, std::move(members) };
        };
        std::optional<DeclarationId> member_object;
        if (prefix->vhdl->text == paired_prefix->vhdl->text) {
            if (auto selected = object_member(*prefix->vhdl)) {
                member_object = selected->first;
                steps.insert(steps.begin(), selected->second.begin(),
                    selected->second.end());
            }
        }
        const auto prefix_resolution = member_object
            ? CompiledDeclarationResolution {
                  CompiledResolutionStatus::unique, { *member_object } }
            : resolver.resolve_expression_name(prefix_id);
        const auto paired_prefix_resolution = member_object
            ? prefix_resolution
            : resolver.resolve_expression_name(paired_prefix_id);
        const auto declaration_id = prefix_resolution.unique();
        const auto paired_declaration_id
            = paired_prefix_resolution.unique();
        if (!declaration_id || !paired_declaration_id
            || *declaration_id != *paired_declaration_id) {
            const auto builtin_spelling = [](const vhdl::Name& name) {
                auto spelling = vhdl_spelling(name);
                spelling = vhdl::without_builtin_prefix(spelling);
                return spelling;
            };
            const auto is_builtin_std_ulogic = [&](const vhdl::Name& name) {
                const auto spelling = builtin_spelling(name);
                return vhdl_name_parts(vhdl_spelling(name)).size() == 1U
                    && same_vhdl_identifier(spelling, "std_ulogic");
            };
            const auto is_implicit_standard_discrete_type = [&](
                const vhdl::Name& name) {
                const auto spelling = builtin_spelling(name);
                return spelling.find_first_of(".:") == std::string_view::npos
                    && vhdl_name_parts(spelling).size() == 1U
                    && builtin_standard_discrete_type(spelling);
            };
            const auto builtin_std_ulogic_visible = [&](
                const vhdl::Name& name, const ScopeId scope) {
                const auto spelling = builtin_spelling(name);
                if (same_vhdl_identifier(spelling, "std_ulogic")) {
                    // An unqualified name needs an exact IEEE import; a
                    // same-spelled declaration from another package is not
                    // sufficient evidence for compiler-owned enum order.
                    return resolver.vhdl_builtin_package_member_imported(
                            "ieee", "std_logic_1164", "std_ulogic", scope)
                        && resolver.vhdl_builtin_type_visible(
                            spelling, scope);
                }
                return false;
            };
            const auto& left_name = prefix->vhdl->referenced_name;
            const auto& right_name = paired_prefix->vhdl->referenced_name;
            const bool no_retained_identity = left_name && right_name
                && !left_name->selected && left_name->overloads.empty()
                && !right_name->selected && right_name->overloads.empty();
            const bool same_prefix_spelling = left_name && right_name
                && same_vhdl_identifier(
                    builtin_spelling(*left_name),
                    builtin_spelling(*right_name));
            const bool same_ieee_std_ulogic = same_prefix_spelling
                && is_builtin_std_ulogic(*left_name)
                && is_builtin_std_ulogic(*right_name);
            const bool same_standard_discrete_type
                = same_prefix_spelling
                && is_implicit_standard_discrete_type(*left_name)
                && is_implicit_standard_discrete_type(*right_name);
            const auto prefix_scope = prefix->vhdl->scope;
            const bool recorded_prefix_scope = prefix_scope.valid()
                && find_scope(*design_, prefix_scope) != nullptr;
            const bool implicit_standard_type = recorded_prefix_scope
                && same_standard_discrete_type;
            const bool imported_ieee_type = same_ieee_std_ulogic
                && builtin_std_ulogic_visible(
                    *left_name, prefix->vhdl->scope)
                && builtin_std_ulogic_visible(
                    *right_name, paired_prefix->vhdl->scope);
            if (constraint.kind == vhdl::RangeKind::array_index
                && recorded_prefix_scope
                && prefix_scope == paired_prefix->vhdl->scope
                && prefix_resolution.status
                    == CompiledResolutionStatus::not_found
                && prefix_resolution.candidates.empty()
                && paired_prefix_resolution.status
                    == CompiledResolutionStatus::not_found
                && paired_prefix_resolution.candidates.empty()
                && no_retained_identity
                && (implicit_standard_type || imported_ieee_type)) {
                // STD.STANDARD is implicitly visible. Its predefined
                // discrete ranges use ascending declaration/range order;
                // IEEE std_ulogic uses ascending enumeration declaration
                // order. Preserve attribute order directly so reverse,
                // singleton, and null ranges do not depend on endpoint
                // comparisons.
                return finish(reverse_range);
            }
            return finish(std::nullopt);
        }
        const auto declaration = find_declaration(
            *design_, effective_, *declaration_id);
        if (!declaration || declaration->vhdl == nullptr) {
            return finish(std::nullopt);
        }
        const auto form = declaration->vhdl->form;
        const bool type_prefix = form == vhdl::DeclarationForm::type
            || form == vhdl::DeclarationForm::subtype
            || form == vhdl::DeclarationForm::generic_type;
        auto source_subtype = declaration->vhdl->subtype;
        // Every element of a record object has the actual's shape: a record
        // formal's element constraints come from its actual (IEEE 1076-2008
        // 4.2.2.2), and a constrained record leaves nothing to choose.
        const auto record_subtype = [&] {
            std::set<TypeId> visited;
            for (auto type_id = source_subtype
                     ? source_subtype->type_mark.target
                     : TypeId { };
                 type_id.valid() && visited.insert(type_id).second;) {
                const auto type = find_type(*design_, effective_, type_id);
                if (!type || type->vhdl == nullptr) {
                    return false;
                }
                if (type->vhdl->form == vhdl::TypeForm::record) {
                    return true;
                }
                type_id = type->vhdl->base.type_mark.target;
            }
            return false;
        };
        const bool actual_shape_is_authoritative
            = declaration->vhdl->form
                    == vhdl::DeclarationForm::generic_constant
            || (source_subtype && source_subtype->unconstrained)
            || (!steps.empty() && record_subtype());
        if (actual_shape_is_authoritative) {
            if (auto actual_subtype = bound_type(
                    *declaration_id, active, true)) {
                // A generic constant's actual has the formal's type (IEEE
                // 1076-2008 6.5.6.2), so an actual subtype that names its
                // mark without an identity denotes the formal's type.
                if (!actual_subtype->type_mark.target.valid()
                    && source_subtype
                    && source_subtype->type_mark.target.valid()) {
                    actual_subtype->type_mark.target
                        = source_subtype->type_mark.target;
                }
                source_subtype = std::move(actual_subtype);
            }
        }
        if (form == vhdl::DeclarationForm::generic_type) {
            if (auto actual_type = bound_type(
                    *declaration_id, active, true)) {
                source_subtype = std::move(actual_type);
            }
        } else if (type_prefix && declaration->vhdl->declared_type) {
            if (!source_subtype) {
                source_subtype.emplace();
            }
            // A type mark denotes the declared type. Retain its nominal
            // identity while following the type record for source bounds.
            source_subtype->type_mark.target
                = *declaration->vhdl->declared_type;
            if (source_subtype->type_mark.spelling.empty()) {
                source_subtype->type_mark.spelling = declaration->vhdl->name;
            }
        } else if (!source_subtype && declaration->vhdl->declared_type) {
            source_subtype.emplace();
            source_subtype->type_mark.target
                = *declaration->vhdl->declared_type;
            source_subtype->type_mark.spelling = declaration->vhdl->name;
        }
        if (!source_subtype) {
            return finish(std::nullopt);
        }
        for (const auto& step : steps) {
            const vhdl::TypeDefinition* composite { };
            std::set<TypeId> visited;
            for (auto type_id = source_subtype->type_mark.target;
                 type_id.valid() && visited.insert(type_id).second;) {
                const auto type = find_type(*design_, effective_, type_id);
                if (!type || type->vhdl == nullptr) {
                    break;
                }
                if (type->vhdl->form == vhdl::TypeForm::array
                    || type->vhdl->form == vhdl::TypeForm::record) {
                    composite = type->vhdl;
                    break;
                }
                type_id = type->vhdl->base.type_mark.target;
            }
            if (composite == nullptr) {
                return finish(std::nullopt);
            }
            if (step == "'element") {
                if (composite->form != vhdl::TypeForm::array
                    || !composite->element_subtype) {
                    return finish(std::nullopt);
                }
                source_subtype = vhdl::constrained_element_subtype(
                    *source_subtype, "", *composite->element_subtype);
                continue;
            }
            const auto name = std::string_view { step }.substr(
                std::string_view { "@vhdl-member:" }.size());
            const auto member = std::ranges::find_if(
                composite->record_elements,
                [&](const vhdl::RecordElement& candidate) {
                    return same_vhdl_identifier(candidate.name, name);
                });
            if (composite->form != vhdl::TypeForm::record
                || member == composite->record_elements.end()) {
                return finish(std::nullopt);
            }
            source_subtype = vhdl::constrained_element_subtype(
                *source_subtype, member->name, member->subtype);
        }

        const auto scalar_range_kind = [](const vhdl::RangeKind kind) {
            return kind == vhdl::RangeKind::integer
                || kind == vhdl::RangeKind::enumeration
                || kind == vhdl::RangeKind::discrete;
        };
        const auto source_type_id = source_subtype->type_mark.target;
        enum class SourceTypeShape : std::uint8_t {
            unknown,
            scalar,
            array,
        };
        const auto source_type_shape = [&] {
            auto type_id = source_type_id;
            std::set<TypeId> visited;
            while (type_id.valid() && visited.insert(type_id).second) {
                const auto fallback = find_type(*design_, effective_, type_id);
                if (!fallback || fallback->vhdl == nullptr) {
                    return SourceTypeShape::unknown;
                }
                const auto& definition = *fallback->vhdl;
                if (definition.form == vhdl::TypeForm::array) {
                    return SourceTypeShape::array;
                }
                if (definition.form == vhdl::TypeForm::scalar
                    || definition.form == vhdl::TypeForm::enumeration
                    || definition.form == vhdl::TypeForm::physical) {
                    return SourceTypeShape::scalar;
                }
                if (definition.form != vhdl::TypeForm::subtype
                    && definition.form != vhdl::TypeForm::alias) {
                    return SourceTypeShape::unknown;
                }
                const auto has_scalar_constraint =
                    [&](const vhdl::RangeConstraint& candidate) {
                        return scalar_range_kind(candidate.kind);
                    };
                if ((definition.scalar_range
                        && has_scalar_constraint(
                            *definition.scalar_range))
                    || std::ranges::any_of(
                        definition.base.constraints,
                        has_scalar_constraint)) {
                    // Explicit scalar range metadata also identifies a
                    // scalar subtype when its predefined base has no TypeId.
                    return SourceTypeShape::scalar;
                }
                type_id = definition.base.type_mark.target;
            }
            return SourceTypeShape::unknown;
        }();
        const bool declared_scalar_constraint = std::ranges::any_of(
            source_subtype->constraints,
            [&](const vhdl::RangeConstraint& candidate) {
                return scalar_range_kind(candidate.kind);
            });
        const bool allow_scalar_index_source
            = constraint.kind == vhdl::RangeKind::array_index
            && (source_type_shape == SourceTypeShape::scalar
                || declared_scalar_constraint);
        const bool scalar_constraint_source
            = scalar_range_kind(constraint.kind)
            || allow_scalar_index_source;
        const auto find_source_constraint = [&](
            const std::vector<vhdl::RangeConstraint>& constraints)
            -> std::pair<std::optional<vhdl::RangeConstraint>, bool> {
            std::optional<vhdl::RangeConstraint> found;
            if (constraint.kind == vhdl::RangeKind::array_index) {
                for (const auto& candidate : constraints) {
                    if (candidate.kind
                        != vhdl::RangeKind::array_index) {
                        continue;
                    }
                    if (found) {
                        return { std::nullopt, true };
                    }
                    found = candidate;
                }
                if (found || !allow_scalar_index_source) {
                    return { found, false };
                }
            }
            for (const auto& candidate : constraints) {
                if (!scalar_range_kind(candidate.kind)) {
                    continue;
                }
                if (found) {
                    return { std::nullopt, true };
                }
                found = candidate;
            }
            return { found, false };
        };
        auto source_match
            = find_source_constraint(source_subtype->constraints);
        if (source_match.second) {
            return finish(std::nullopt);
        }
        auto source_range = std::move(source_match.first);
        auto type_id = source_type_id;
        std::set<TypeId> visited;
        while (!source_range && type_id.valid()
            && visited.insert(type_id).second) {
            const auto fallback = find_type(*design_, effective_, type_id);
            if (!fallback || fallback->vhdl == nullptr) {
                break;
            }
            const auto& definition = *fallback->vhdl;
            if (definition.form == vhdl::TypeForm::array) {
                if (constraint.kind != vhdl::RangeKind::array_index
                    || definition.array_dimensions.size() != 1U
                    || !definition.array_dimensions.front().constraint) {
                    return finish(std::nullopt);
                }
                source_range = *definition.array_dimensions.front().constraint;
                break;
            }
            if (definition.scalar_range
                && (definition.scalar_range->kind == constraint.kind
                    || (scalar_constraint_source
                        && scalar_range_kind(
                            definition.scalar_range->kind)))) {
                source_range = *definition.scalar_range;
                break;
            }
            if (definition.form == vhdl::TypeForm::enumeration
                && scalar_constraint_source) {
                // An enumeration type's implicit base range follows literal
                // declaration order. Do not infer this from endpoint values.
                const auto direction = reverse_range;
                return finish(direction);
            }
            if (definition.form != vhdl::TypeForm::subtype
                && definition.form != vhdl::TypeForm::alias) {
                break;
            }
            source_match
                = find_source_constraint(definition.base.constraints);
            if (source_match.second) {
                return finish(std::nullopt);
            }
            source_range = std::move(source_match.first);
            type_id = definition.base.type_mark.target;
        }
        if (!source_range) {
            return finish(std::nullopt);
        }
        const auto direction = range_source_direction(
            *source_range, active, path);
        if (!direction) {
            return finish(std::nullopt);
        }
        return finish(reverse_range
            ? std::optional<bool> { !*direction }
            : direction);
    };
    materialize_boundaries = [&](
        vhdl::SubtypeIndication& subtype,
        const std::span<const CompiledBindingFrame> active) {
        const auto& unresolved = unresolved_enum_boundaries_for(active);
        for (auto& constraint : subtype.constraints) {
            if (!constraint.left && constraint.left_expression
                && !unresolved.contains(
                    *constraint.left_expression)) {
                constraint.left = evaluate_boundary(
                    constraint.left, constraint.left_expression, active);
            }
            if (!constraint.right && constraint.right_expression
                && !unresolved.contains(
                    *constraint.right_expression)) {
                constraint.right = evaluate_boundary(
                    constraint.right, constraint.right_expression, active);
            }
            if (constraint.direction_from_bounds && constraint.left
                && constraint.right) {
                RangeDirectionPath path;
                const auto source_direction = range_source_direction(
                    constraint, active, path);
                if (source_direction) {
                    constraint.descending = *source_direction;
                    constraint.null = constraint.descending
                        ? *constraint.left < *constraint.right
                        : *constraint.left > *constraint.right;
                    constraint.direction_from_bounds = false;
                }
            }
        }
    };
    const auto concrete_predefined = [&]() {
        if (effective_ == nullptr || !binding_frames_.empty()
            || !effective_->specialization().actual_identities.empty()
            || !effective_->specialization().hierarchy_identities.empty()
            || input.type_mark.target.valid()
            || input.type_mark.spelling.empty()
            || input.type_mark.spelling.find('.') != std::string::npos
            || input.predefined_attribute
            || input.predefined_attribute_dimension
            || !input.resolution_function.spelling.empty()
            || !input.resolution_function.canonical.empty()
            || input.unconstrained
            || input.has_unspecified_parts()
            || std::ranges::any_of(input.constraints,
                [](const vhdl::RangeConstraint& constraint) {
                    return !constraint.left || !constraint.right
                        || constraint.left_expression
                        || constraint.right_expression;
                })) {
            return std::optional<vhdl::SubtypeIndication> { };
        }
        const auto named_types = design_->vhdl_type_declarations_named(
            simple_vhdl_name(input.type_mark.spelling));
        if (!named_types || !named_types->empty()) {
            return std::optional<vhdl::SubtypeIndication> { };
        }
        auto result = input;
        return complete_predefined(result, binding_frames_, use_scope)
            ? std::optional<vhdl::SubtypeIndication> { std::move(result) }
            : std::optional<vhdl::SubtypeIndication> { };
    }();
    if (concrete_predefined) {
        return concrete_predefined;
    }
    // Lookups below accept only type, subtype and generic-type
    // declarations, all of which the design indexes by identifier: a name
    // with none (a predefined or IEEE type) cannot resolve.
    const auto may_name_type = [&](const std::string_view spelling) {
        const auto last = spelling.substr(spelling.rfind('.') + 1U);
        if (last.empty() || last.find_first_of("@:") != std::string_view::npos) {
            return true;
        }
        const auto declarations = design_->vhdl_type_declarations_named(last);
        return !declarations || !declarations->empty();
    };
    // `T'element`, `T'index(N)` and `T'designated_subtype` denote a subtype
    // of the prefix's type (IEEE 1076-2019 16.2.2, 16.2.3): resolve the
    // prefix, then select from its type definition.
    const auto attribute_subtype = [&](const vhdl::SubtypeIndication& input,
                                       const ScopeId scope,
                                       std::vector<CompiledBindingFrame>&
                                           active_frames)
        -> std::optional<vhdl::SubtypeIndication> {
        const auto attribute = *input.predefined_attribute;
        std::int64_t dimension { 1 };
        if (input.predefined_attribute_dimension) {
            const auto value = evaluate_boundary(std::nullopt,
                input.predefined_attribute_dimension, active_frames);
            if (!value || *value < 1) {
                return std::nullopt;
            }
            dimension = *value;
        }
        auto prefix = input;
        prefix.predefined_attribute.reset();
        prefix.predefined_attribute_dimension.reset();
        const auto effective = resolve(prefix, scope, active_frames)
                                   .value_or(prefix);
        // An unbound generic array type (`type T is array (type is range
        // <>) of type is private`, IEEE 1076-2019 6.5.3) yields its open
        // index or element class.
        std::optional<vhdl::SubtypeIndication> open_prefix;
        if (effective.has_unspecified_parts()) {
            open_prefix = effective;
        }
        for (const auto mark :
            { effective.type_mark.target, input.type_mark.target }) {
            if (open_prefix || !mark.valid()) {
                break;
            }
            const auto type = find_type(*design_, effective_, mark);
            const auto declaration = type && type->vhdl != nullptr
                ? find_declaration(*design_, effective_,
                      type->vhdl->declaration)
                : std::nullopt;
            if (declaration && declaration->vhdl != nullptr
                && declaration->vhdl->form
                    == vhdl::DeclarationForm::generic_type
                && declaration->vhdl->subtype
                && declaration->vhdl->subtype->has_unspecified_parts()) {
                open_prefix = *declaration->vhdl->subtype;
            }
        }
        if (open_prefix
            && open_prefix->unspecified_class
                == vhdl::UnspecifiedTypeClass::array
            && !open_prefix->unspecified_component_classes.empty()
            && (attribute == vhdl::PredefinedAttribute::element
                || attribute == vhdl::PredefinedAttribute::index)) {
            const auto& classes = open_prefix->unspecified_component_classes;
            const auto position = attribute
                    == vhdl::PredefinedAttribute::element
                ? classes.size() - 1U
                : static_cast<std::size_t>(dimension - 1);
            if (attribute == vhdl::PredefinedAttribute::index
                && position + 1U >= classes.size()) {
                return std::nullopt;
            }
            const auto& marks = open_prefix->unspecified_component_type_marks;
            if (classes[position] == vhdl::UnspecifiedTypeClass::none
                && position < marks.size() && !marks[position].empty()) {
                vhdl::SubtypeIndication component;
                component.type_mark.spelling = marks[position];
                return resolve(component, scope, active_frames);
            }
            vhdl::SubtypeIndication open;
            open.unspecified_class
                = classes[position] == vhdl::UnspecifiedTypeClass::none
                ? vhdl::UnspecifiedTypeClass::private_type
                : classes[position];
            open.unspecified_inference_identity
                = (open_prefix->unspecified_inference_identity.empty()
                          ? "type#"
                              + std::to_string(
                                  input.type_mark.target.value())
                          : open_prefix->unspecified_inference_identity)
                + (attribute == vhdl::PredefinedAttribute::element
                        ? std::string { "'element" }
                        : "'index(" + std::to_string(dimension) + ")");
            return open;
        }
        const vhdl::TypeDefinition* definition { };
        auto type_id = effective.type_mark.target;
        std::set<TypeId> visited;
        while (type_id.valid() && visited.insert(type_id).second) {
            const auto type = find_type(*design_, effective_, type_id);
            if (!type || type->vhdl == nullptr) {
                break;
            }
            if (type->vhdl->form != vhdl::TypeForm::subtype
                && type->vhdl->form != vhdl::TypeForm::alias) {
                definition = type->vhdl;
                break;
            }
            type_id = type->vhdl->base.type_mark.target;
        }
        auto definition_scope = scope;
        if (definition != nullptr) {
            const auto declaration = find_declaration(
                *design_, effective_, definition->declaration);
            if (declaration && declaration->vhdl != nullptr) {
                definition_scope = declaration->vhdl->scope;
            }
        }
        const auto named = [](const std::string_view spelling) {
            vhdl::SubtypeIndication named_subtype;
            named_subtype.type_mark.spelling = spelling;
            return named_subtype;
        };
        std::optional<vhdl::SubtypeIndication> selected;
        const auto index_constraints = [&] {
            std::vector<vhdl::RangeConstraint> ranges;
            for (const auto& range : effective.constraints) {
                if (range.kind == vhdl::RangeKind::array_index) {
                    ranges.push_back(range);
                }
            }
            return ranges;
        }();
        const auto builtin_array = effective.builtin_type
                != vhdl::BuiltinTypeIdentity::none
            ? vhdl::builtin_type_name(effective.builtin_type)
            : simple_vhdl_name(
                  vhdl::without_builtin_prefix(effective.type_mark.spelling));
        const auto builtin_element = [&]() -> std::string_view {
            for (const auto& [array, element] :
                std::initializer_list<std::pair<std::string_view,
                    std::string_view>> {
                    { "string", "character" }, { "bit_vector", "bit" },
                    { "boolean_vector", "boolean" },
                    { "integer_vector", "integer" },
                    { "real_vector", "real" }, { "time_vector", "time" } }) {
                if (same_vhdl_identifier(builtin_array, array)) {
                    return element;
                }
            }
            return { };
        }();
        switch (attribute) {
        case vhdl::PredefinedAttribute::element:
            if (definition != nullptr
                && definition->form == vhdl::TypeForm::array
                && definition->element_subtype) {
                selected = vhdl::constrained_element_subtype(
                    effective, "", *definition->element_subtype);
            } else if (definition == nullptr && !builtin_element.empty()) {
                selected = named(builtin_element);
            }
            break;
        case vhdl::PredefinedAttribute::index:
            if (definition != nullptr
                && definition->form == vhdl::TypeForm::array
                && static_cast<std::uint64_t>(dimension)
                    <= definition->array_dimensions.size()) {
                const auto& index = definition->array_dimensions
                    [static_cast<std::size_t>(dimension - 1)];
                selected = named(index.index_subtype.text());
                if (index.index_subtype.selected) {
                    const auto declaration = find_declaration(*design_,
                        effective_, *index.index_subtype.selected);
                    if (declaration && declaration->vhdl != nullptr
                        && declaration->vhdl->declared_type) {
                        selected->type_mark.target
                            = *declaration->vhdl->declared_type;
                    }
                }
            } else if (definition == nullptr && dimension == 1
                && (!builtin_element.empty()
                    || effective.builtin_type
                        == vhdl::BuiltinTypeIdentity::
                            ieee_std_logic_1164_std_logic_vector
                    || effective.builtin_type
                        == vhdl::BuiltinTypeIdentity::
                            ieee_std_logic_1164_std_ulogic_vector
                    || same_vhdl_identifier(builtin_array, "std_logic_vector")
                    || same_vhdl_identifier(
                        builtin_array, "std_ulogic_vector"))) {
                // STD_ULOGIC_VECTOR and its subtype STD_LOGIC_VECTOR are
                // indexed by NATURAL (IEEE 1076-2008 16.7).
                selected = named(same_vhdl_identifier(builtin_array, "string")
                        ? "positive" : "natural");
            }
            // A constrained prefix's index subtype is its index range.
            if (selected
                && static_cast<std::uint64_t>(dimension)
                    <= index_constraints.size()) {
                auto range = index_constraints
                    [static_cast<std::size_t>(dimension - 1)];
                range.kind = vhdl::RangeKind::discrete;
                selected->constraints = { range };
            }
            break;
        case vhdl::PredefinedAttribute::designated_subtype:
            if (definition != nullptr
                && (definition->form == vhdl::TypeForm::access
                    || definition->form == vhdl::TypeForm::file)
                && definition->designated_subtype) {
                selected = *definition->designated_subtype;
            }
            break;
        default:
            break;
        }
        if (!selected) {
            return std::nullopt;
        }
        auto resolved = resolve(*selected, definition_scope, active_frames);
        // The subtype of an index named by the array type's declaration (a
        // predefined `natural`) is visible at the use as well, where a
        // compiled library's scope may not resolve it (IEEE 1076-2019
        // 16.2.3).
        if (!resolved && definition_scope != scope) {
            resolved = resolve(*selected, scope, active_frames);
        }
        return resolved;
    };
    // Whether a subtype (or the subtype its mark names) has a bound that
    // only a generic environment can fold.
    const auto residual_bounds = [&](const auto& self,
                                     const vhdl::SubtypeIndication& subtype,
                                     const std::size_t depth) -> bool {
        const auto residual = [](const vhdl::RangeConstraint& range) {
            return (range.left_expression && !range.left)
                || (range.right_expression && !range.right);
        };
        if (depth > 8U || std::ranges::any_of(subtype.constraints, residual)
            || std::ranges::any_of(subtype.element_constraints,
                [&](const vhdl::ElementConstraint& element) {
                    return std::ranges::any_of(element.constraints, residual);
                })) {
            return depth <= 8U;
        }
        const auto type = subtype.type_mark.target.valid()
            ? find_type(*design_, effective_, subtype.type_mark.target)
            : std::nullopt;
        if (!type || type->vhdl == nullptr) {
            return false;
        }
        if (type->vhdl->form == vhdl::TypeForm::subtype
            || type->vhdl->form == vhdl::TypeForm::alias) {
            return self(self, type->vhdl->base, depth + 1U);
        }
        return type->vhdl->form == vhdl::TypeForm::array
            && std::ranges::any_of(type->vhdl->array_dimensions,
                [&](const vhdl::ArrayDimension& dimension) {
                    return dimension.constraint
                        && residual(*dimension.constraint);
                });
    };
    close_instance_elements = [&](vhdl::SubtypeIndication& root,
                                  const vhdl::SubtypeIndication& level,
                                  std::vector<std::string>& path,
                                  const std::size_t depth,
                                  std::vector<CompiledBindingFrame>& active_frames) {
        if (depth > 8U) {
            return;
        }
        const vhdl::TypeDefinition* definition { };
        auto id = level.type_mark.target;
        std::set<std::uint32_t> seen;
        while (id.valid() && seen.insert(id.value()).second) {
            const auto type = find_type(*design_, effective_, id);
            if (!type || type->vhdl == nullptr) {
                break;
            }
            if (type->vhdl->form == vhdl::TypeForm::subtype
                || type->vhdl->form == vhdl::TypeForm::alias) {
                id = type->vhdl->base.type_mark.target;
                continue;
            }
            definition = type->vhdl;
            break;
        }
        if (definition == nullptr) {
            return;
        }
        const auto definition_declaration = definition->declaration.valid()
            ? find_declaration(*design_, effective_, definition->declaration)
            : std::nullopt;
        const auto definition_scope = definition_declaration
                && definition_declaration->vhdl != nullptr
            ? definition_declaration->vhdl->scope
            : ScopeId { };
        const auto visit = [&](const std::string& step,
                               const vhdl::SubtypeIndication& declared) {
            path.push_back(step);
            const auto owned = std::ranges::find_if(root.element_constraints,
                [&](const vhdl::ElementConstraint& constraint) {
                    return constraint.path == path;
                });
            const auto owned_index = owned == root.element_constraints.end()
                ? std::optional<std::size_t> { }
                : std::optional { static_cast<std::size_t>(
                      owned - root.element_constraints.begin()) };
            // A constraint written on the selected subtype is evaluated in
            // this frame too: its bounds may name the instance's generics.
            const auto element = resolve(vhdl::constrained_element_subtype(
                                             level, step, declared),
                definition_scope, active_frames);
            if (element
                && (owned_index
                    || residual_bounds(residual_bounds, declared, 0U))) {
                std::vector<vhdl::RangeConstraint> ranges;
                const bool array_element = std::ranges::any_of(
                    element->constraints, [](const auto& range) {
                        return range.kind == vhdl::RangeKind::array_index;
                    });
                for (auto range : element->constraints) {
                    // A scalar element's range is the instance's as well
                    // (IEEE 1076-2008 4.9, 5.3.3.1).
                    const bool kept = array_element
                        ? range.kind == vhdl::RangeKind::array_index
                        : ranges.empty()
                            && (range.kind == vhdl::RangeKind::integer
                                || range.kind
                                    == vhdl::RangeKind::enumeration
                                || range.kind == vhdl::RangeKind::discrete);
                    if (!kept) {
                        continue;
                    }
                    if (!range.left || !range.right
                        || range.direction_from_bounds) {
                        ranges.clear();
                        break;
                    }
                    range.left_expression.reset();
                    range.right_expression.reset();
                    ranges.push_back(range);
                }
                if (!ranges.empty() && owned_index) {
                    root.element_constraints[*owned_index].constraints
                        = std::move(ranges);
                } else if (!ranges.empty()) {
                    root.element_constraints.push_back(
                        { path, std::move(ranges) });
                }
            }
            if (element) {
                close_instance_elements(
                    root, *element, path, depth + 1U, active_frames);
            }
            path.pop_back();
        };
        if (definition->form == vhdl::TypeForm::array
            && definition->element_subtype) {
            visit("", *definition->element_subtype);
        } else if (definition->form == vhdl::TypeForm::record) {
            for (const auto& element : definition->record_elements) {
                visit(element.name, element.subtype);
            }
        }
    };
    resolve = [&](vhdl::SubtypeIndication result, ScopeId scope,
                  std::vector<CompiledBindingFrame>& active_frames)
        -> std::optional<vhdl::SubtypeIndication> {
        materialize_boundaries(result, active_frames);
        if (result.predefined_attribute) {
            return attribute_subtype(result, scope, active_frames);
        }
        const auto resolver = CompiledDesignResolver {
            *design_, selected_unit_, effective_, active_frames
        };
        // A type of a package instance's template, resolved in the
        // instance's generic environment (IEEE 1076-2008 4.9).
        const auto package_member_subtype = [&](
            const CompiledVhdlPackageMember& package)
            -> std::optional<std::optional<vhdl::SubtypeIndication>> {
            const auto declaration = find_declaration(
                *design_, effective_, package.member);
            if (declaration && declaration->vhdl != nullptr) {
                auto selected = declaration->vhdl->subtype;
                // A generic type of the instance denotes its actual (IEEE
                // 1076-2008 6.5.3, 4.9).
                if (declaration->vhdl->form
                    == vhdl::DeclarationForm::generic_type) {
                    const auto bound = std::ranges::find(
                        package.generic_bindings, package.member,
                        &CompiledActualBinding::formal);
                    if (bound != package.generic_bindings.end()
                        && bound->vhdl_type) {
                        selected = bound->vhdl_type;
                    }
                }
                if (!selected && declaration->vhdl->declared_type) {
                    selected.emplace();
                    selected->type_mark.target
                        = *declaration->vhdl->declared_type;
                    selected->type_mark.spelling
                        = declaration->vhdl->name;
                }
                if (selected) {
                    if (!result.constraints.empty()) {
                        selected->constraints = result.constraints;
                        selected->unconstrained = false;
                        selected->executable_width.reset();
                    }
                    // A record constraint on the selected mark
                    // (IEEE 1076-2008 5.3.3.1) applies as written.
                    if (!result.element_constraints.empty()) {
                        selected->element_constraints
                            = result.element_constraints;
                        selected->executable_width.reset();
                    }
                    const bool append = !package.generic_bindings.empty()
                        && std::ranges::none_of(active_frames,
                            [&](const auto& frame) {
                                return frame
                                    == package.generic_bindings;
                            });
                    if (append) {
                        active_frames.push_back(
                            package.generic_bindings);
                    }
                    auto effective = resolve(
                        *selected, declaration->vhdl->scope,
                        active_frames);
                    // Element subtypes of the instance's composite type are
                    // the instance's (4.9); keep their frame-dependent bounds
                    // as element constraints (5.3.2.1, 5.3.3.1), so a walk of
                    // the template outside this frame sees the instance's.
                    if (append && effective) {
                        const auto snapshot = *effective;
                        std::vector<std::string> path;
                        close_instance_elements(
                            *effective, snapshot, path, 0U, active_frames);
                    }
                    if (append) {
                        active_frames.pop_back();
                    }
                    return std::optional<std::optional<vhdl::SubtypeIndication>> {
                        std::move(effective) };
                }
            }
            return std::nullopt;
        };
        if (result.type_mark.spelling.find('.') != std::string::npos
            && may_name_type(result.type_mark.spelling)) {
            vhdl::Name name;
            name.spelling = result.type_mark.spelling;
            name.canonical = result.type_mark.spelling;
            name.source = result.type_mark.source;
            const auto package = resolver.resolve_vhdl_package_members(
                name, scope, [](const CompiledDeclarationView& candidate) {
                    if (candidate.vhdl == nullptr) {
                        return false;
                    }
                    const auto form = candidate.vhdl->form;
                    return form == vhdl::DeclarationForm::type
                        || form == vhdl::DeclarationForm::subtype
                        || form == vhdl::DeclarationForm::generic_type;
                }).unique();
            if (package) {
                if (auto effective = package_member_subtype(*package)) {
                    return *effective;
                }
            }
        }

        std::optional<DeclarationId> declaration_id;
        if (result.type_mark.target.valid()) {
            const auto type
                = find_type(*design_, effective_, result.type_mark.target);
            if (type && type->vhdl != nullptr) {
                declaration_id = type->vhdl->declaration;
            }
        }
        // A declaration identity (`source:offset[@instance]:name`) names the
        // type declared there.
        if (!declaration_id
            && parse_vhdl_declaration_identity(result.type_mark.spelling)) {
            if (const auto identified = resolver.resolve_vhdl_named_type(
                    result.type_mark.spelling, scope)) {
                if (const auto type
                    = find_type(*design_, effective_, *identified);
                    type && type->vhdl != nullptr) {
                    result.type_mark.target = *identified;
                    declaration_id = type->vhdl->declaration;
                }
            }
        }
        if (!declaration_id && !result.type_mark.spelling.empty()) {
            vhdl::Name name;
            name.spelling = result.type_mark.spelling;
            name.canonical = result.type_mark.spelling;
            name.source = result.type_mark.source;
            constexpr auto synthetic_vital_prefix
                = std::string_view { "@fsim-vital:" };
            if (name.spelling.starts_with(synthetic_vital_prefix)) {
                // Synthetic VITAL declarations retain their marker as the
                // nominal identity.  Only remove it for lexical lookup so the
                // canonical resolver can attach the visible declaration's
                // TypeId without leaking a hierarchy-local resolver.
                name.spelling.erase(0, synthetic_vital_prefix.size());
                name.canonical = name.spelling;
            }
            if (may_name_type(name.spelling)) {
                declaration_id = resolver.resolve_vhdl(
                    name, scope, [](const CompiledDeclarationView& candidate) {
                        if (candidate.vhdl == nullptr) {
                            return false;
                        }
                        const auto form = candidate.vhdl->form;
                        return form == vhdl::DeclarationForm::type
                            || form == vhdl::DeclarationForm::subtype
                            || form == vhdl::DeclarationForm::generic_type;
                    }).unique();
            }
            // A type of a generic package made visible by `use inst.all`
            // denotes the instance's copy (IEEE 1076-2008 4.9, 12.4).
            const auto generic_package_member = [&] {
                const auto declaration = declaration_id
                    ? find_declaration(*design_, effective_, *declaration_id)
                    : std::nullopt;
                const auto* declaration_scope
                    = declaration && declaration->vhdl != nullptr
                    ? find_scope(*design_, declaration->vhdl->scope)
                    : nullptr;
                const auto owner = declaration_scope != nullptr
                    ? design_->find_unit(declaration_scope->unit)
                    : std::nullopt;
                return owner && owner->vhdl != nullptr
                    && owner->vhdl->kind == vhdl::UnitKind::package
                    && std::ranges::any_of(owner->vhdl->declarations,
                        [&](const DeclarationId id) {
                            const auto member
                                = find_declaration(*design_, effective_, id);
                            return member && member->vhdl != nullptr
                                && vhdl::is_generic_formal(member->vhdl->form);
                        });
            };
            if (generic_package_member()) {
                const auto package = resolver.resolve_vhdl_package_members(
                    name, scope, [](const CompiledDeclarationView& candidate) {
                        return candidate.vhdl != nullptr
                            && (candidate.vhdl->form
                                    == vhdl::DeclarationForm::type
                                || candidate.vhdl->form
                                    == vhdl::DeclarationForm::subtype
                                || candidate.vhdl->form
                                    == vhdl::DeclarationForm::generic_type);
                    }).unique();
                if (package && package->member == *declaration_id
                    && !package->generic_bindings.empty()) {
                    if (auto effective = package_member_subtype(*package)) {
                        return *effective;
                    }
                }
            }
        }
        if (declaration_id) {
            const auto declaration = find_declaration(
                *design_, effective_, *declaration_id);
            if (declaration && declaration->vhdl != nullptr
                && declaration->vhdl->form
                    == vhdl::DeclarationForm::generic_type
                && active_generics.insert(*declaration_id).second) {
                auto replacement = bound_type(
                    *declaration_id, active_frames, false);
                if (!replacement) {
                    replacement = declaration->vhdl->default_type;
                }
                if (replacement) {
                    if (!result.constraints.empty()) {
                        replacement->constraints = result.constraints;
                        replacement->unconstrained = false;
                        replacement->executable_width.reset();
                    }
                    auto effective = resolve(
                        *replacement, declaration->vhdl->scope,
                        active_frames);
                    active_generics.erase(*declaration_id);
                    return effective;
                }
                active_generics.erase(*declaration_id);
            }
            if (declaration && declaration->vhdl != nullptr
                && declaration->vhdl->declared_type) {
                result.type_mark.target
                    = *declaration->vhdl->declared_type;
                if (result.type_mark.spelling.empty()) {
                    result.type_mark.spelling = declaration->vhdl->name;
                }
            }
        }

        if (!result.type_mark.target.valid()) {
            complete_predefined(result, active_frames, scope);
            return result;
        }
        if (!active_types.insert(result.type_mark.target).second) {
            return result;
        }
        const auto type
            = find_type(*design_, effective_, result.type_mark.target);
        if (!type || type->vhdl == nullptr) {
            active_types.erase(result.type_mark.target);
            complete_predefined(result, active_frames, scope);
            return result;
        }
        const auto& definition = *type->vhdl;
        const auto definition_declaration = find_declaration(
            *design_, effective_, definition.declaration);
        const auto definition_scope
            = definition_declaration
                && definition_declaration->vhdl != nullptr
            ? definition_declaration->vhdl->scope
            : scope;
        std::optional<decltype(result.type_mark)> compiler_owned_mark;
        if (definition.form == vhdl::TypeForm::subtype
            || definition.form == vhdl::TypeForm::alias
            || definition.form == vhdl::TypeForm::scalar) {
            auto inherited = definition.base;
            if (!result.element_constraints.empty()) {
                // The indication's element constraints refine those of
                // the named subtype (IEEE 1076-2008 6.3).
                inherited.element_constraints.insert(
                    inherited.element_constraints.end(),
                    result.element_constraints.begin(),
                    result.element_constraints.end());
                result.executable_width.reset();
            }
            if (!result.constraints.empty()) {
                inherited.constraints = result.constraints;
                inherited.unconstrained = false;
                result.executable_width.reset();
            } else if (definition.scalar_range) {
                inherited.constraints = { *definition.scalar_range };
                inherited.unconstrained = false;
            }
            const auto effective = resolve(
                inherited, definition_scope, active_frames);
            if (effective) {
                merge_inherited(result, *effective);
                // The compiler-owned fixed_pkg and float_pkg types have no
                // retained declaration: their name is their identity, which
                // a subtype of one shares (IEEE 1076-2008 6.3).
                if (!effective->type_mark.target.valid()
                    && std::ranges::any_of(
                        std::array { std::string_view { "ufixed" },
                            std::string_view { "sfixed" },
                            std::string_view { "unresolved_ufixed" },
                            std::string_view { "unresolved_sfixed" },
                            std::string_view { "u_ufixed" },
                            std::string_view { "u_sfixed" },
                            std::string_view { "float" },
                            std::string_view { "unresolved_float" },
                            std::string_view { "u_float" } },
                        [&](const std::string_view name) {
                            return same_vhdl_identifier(
                                vhdl::without_builtin_prefix(
                                    effective->type_mark.spelling),
                                name);
                        })) {
                    compiler_owned_mark = effective->type_mark;
                }
            }
        }
        if (definition.form == vhdl::TypeForm::enumeration
            && !definition.enumeration_literals.empty()) {
            // A range-constrained subtype keeps its base type's
            // representation, so its values convert freely (6.3).
            auto maximum = definition.enumeration_literals.size() - 1U;
            std::uint64_t width { 1U };
            while (maximum > 1U) {
                ++width;
                maximum >>= 1U;
            }
            result.domain = result.domain == vhdl::ValueDomain::unknown
                ? vhdl::ValueDomain::bit2
                : result.domain;
            result.executable_width = width;
        } else if (definition.form == vhdl::TypeForm::physical) {
            const auto width = standard_width();
            result.domain = vhdl::ValueDomain::integer;
            result.executable_width = width;
            result.integer_storage_width = width;
            result.signed_value = true;
        } else if (definition.form == vhdl::TypeForm::access) {
            // Access subtypes retain designated-object constraints, but their
            // runtime representation is always one 32-bit handle. Never treat
            // an array index constraint's element count as handle width.
            result.domain = vhdl::ValueDomain::bit2;
            result.executable_width = 32U;
        } else if (definition.form == vhdl::TypeForm::record) {
            std::uint64_t width { };
            auto domain = vhdl::ValueDomain::unknown;
            bool concrete = !definition.record_elements.empty();
            for (const auto& element : definition.record_elements) {
                const auto effective = resolve(
                    vhdl::constrained_element_subtype(
                        result, element.name, element.subtype),
                    definition_scope, active_frames);
                // A null array element has no scalar subelements and adds
                // no bits (IEEE 1076-2008 5.3.2.1).
                const bool null_array = effective
                    && std::ranges::any_of(effective->constraints,
                        [](const vhdl::RangeConstraint& range) {
                            return range.kind == vhdl::RangeKind::array_index
                                && (range.null
                                    || (range.left && range.right
                                        && (range.descending
                                                ? *range.left < *range.right
                                                : *range.left
                                                    > *range.right)));
                        });
                if (null_array) {
                    continue;
                }
                // An element left unconstrained has no layout of its own
                // (5.3.3); its nominal width is not its size.
                const bool open_element = effective
                    && effective->unconstrained
                    && std::ranges::none_of(effective->constraints,
                        [](const vhdl::RangeConstraint& range) {
                            return range.kind == vhdl::RangeKind::array_index;
                        });
                if (!effective || open_element || !effective->executable_width
                    || *effective->executable_width == 0U
                    || width > std::numeric_limits<std::uint64_t>::max()
                            - *effective->executable_width) {
                    concrete = false;
                    break;
                }
                width += *effective->executable_width;
                // A record is a composite of its elements' bits, never a
                // string value itself, even when its only element is one.
                const auto element_domain
                    = effective->domain == vhdl::ValueDomain::string
                    ? vhdl::ValueDomain::bit2
                    : effective->domain;
                if (domain == vhdl::ValueDomain::unknown) {
                    domain = element_domain;
                } else if (domain != element_domain) {
                    const auto rank = [](const vhdl::ValueDomain value) {
                        return value == vhdl::ValueDomain::logic9 ? 3
                            : value == vhdl::ValueDomain::logic4 ? 2
                            : value == vhdl::ValueDomain::unknown
                                || value == vhdl::ValueDomain::string
                            ? 0
                            : 1;
                    };
                    const auto merged = std::max(
                        rank(domain), rank(element_domain));
                    domain = merged == 3 ? vhdl::ValueDomain::logic9
                        : merged == 2 ? vhdl::ValueDomain::logic4
                        : merged == 1 ? vhdl::ValueDomain::bit2
                                      : vhdl::ValueDomain::unknown;
                }
            }
            if (concrete && width != 0U) {
                result.executable_width = width;
                if (result.domain == vhdl::ValueDomain::unknown) {
                    result.domain = domain;
                }
            }
        } else if (definition.form == vhdl::TypeForm::array
            && definition.element_subtype) {
            const auto element = resolve(
                vhdl::constrained_element_subtype(
                    result, "", *definition.element_subtype),
                definition_scope, active_frames);
            if (definition.array_dimensions.size() == 1U
                && definition_declaration
                && definition_declaration->vhdl != nullptr
                && definition.element_subtype->domain
                    == vhdl::ValueDomain::logic9) {
                const auto* owner = design_->vhdl_scope_unit(
                    definition_declaration->vhdl->scope);
                if (vhdl::is_package(owner, "ieee", "std_logic_1164")
                    && owner->primary_name.empty()) {
                    if (same_vhdl_identifier(
                            definition.name, "std_logic_vector")) {
                        result.builtin_type = vhdl::BuiltinTypeIdentity::
                            ieee_std_logic_1164_std_logic_vector;
                    } else if (same_vhdl_identifier(
                                   definition.name,
                                   "std_ulogic_vector")) {
                        result.builtin_type = vhdl::BuiltinTypeIdentity::
                            ieee_std_logic_1164_std_ulogic_vector;
                    }
                }
            }
            const auto unresolved_predefined_character = [&](
                std::string_view spelling, const ScopeId scope) {
                spelling = vhdl::without_builtin_prefix(spelling);
                if (spelling.empty()
                    || !same_vhdl_identifier(
                        simple_vhdl_name(spelling), "character")) {
                    return false;
                }

                vhdl::Name name;
                name.spelling = spelling;
                name.canonical = spelling;
                const auto resolver = CompiledDesignResolver {
                    *design_, selected_unit_, effective_, active_frames
                };
                const auto visible_type = resolver.resolve_vhdl(
                    name, scope, [](const CompiledDeclarationView& view) {
                        if (view.vhdl == nullptr) {
                            return false;
                        }
                        const auto form = view.vhdl->form;
                        return form == vhdl::DeclarationForm::type
                            || form == vhdl::DeclarationForm::subtype
                            || form == vhdl::DeclarationForm::generic_type;
                    });
                if (visible_type.status
                    != CompiledResolutionStatus::not_found) {
                    return false;
                }

                // STD.STANDARD declarations are implicitly visible. Some
                // compiled bundles omit their TypeDefinition, so a targetless
                // CHARACTER can be identified only after scoped lookup rules
                // have ruled out a visible user declaration with that name.
                return same_vhdl_identifier(spelling, "character")
                    || same_vhdl_identifier(
                        spelling, "standard.character")
                    || same_vhdl_identifier(
                        spelling, "std.standard.character");
            };
            const auto predefined_character_index = [&](
                const vhdl::ArrayDimension& dimension) {
                const auto& index_name = dimension.index_subtype;
                auto index_scope = definition_scope;
                std::optional<TypeId> index_type;
                if (index_name.selected) {
                    const auto declaration = find_declaration(
                        *design_, effective_, *index_name.selected);
                    if (!declaration || declaration->vhdl == nullptr) {
                        return false;
                    }
                    index_scope = declaration->vhdl->scope;
                    if (declaration->vhdl->declared_type) {
                        index_type = *declaration->vhdl->declared_type;
                    } else if (declaration->vhdl->subtype) {
                        const auto& subtype
                            = *declaration->vhdl->subtype;
                        index_type = subtype.type_mark.target.valid()
                            ? std::optional { subtype.type_mark.target }
                            : resolve_vhdl_named_type(
                                  subtype.type_mark.spelling, index_scope);
                        if (!index_type) {
                            return unresolved_predefined_character(
                                subtype.type_mark.spelling, index_scope);
                        }
                    }
                    if (!index_type) {
                        return false;
                    }
                } else {
                    index_type = resolve_vhdl_named_type(
                        index_name.spelling, index_scope);
                    if (!index_type) {
                        return unresolved_predefined_character(
                            index_name.spelling, index_scope);
                    }
                }

                std::unordered_set<std::uint32_t> visited;
                while (index_type
                    && visited.insert(index_type->value()).second) {
                    const auto type
                        = find_type(*design_, effective_, *index_type);
                    if (!type || type->vhdl == nullptr) {
                        return false;
                    }
                    const auto& index_definition = *type->vhdl;
                    if (index_definition.form
                        == vhdl::TypeForm::enumeration) {
                        const auto declaration = find_declaration(
                            *design_, effective_,
                            index_definition.declaration);
                        if (!declaration || declaration->vhdl == nullptr) {
                            return false;
                        }
                        const auto* owner_scope = find_scope(
                            *design_, declaration->vhdl->scope);
                        const auto owner = owner_scope != nullptr
                            ? design_->find_unit(owner_scope->unit)
                            : std::nullopt;
                        return same_vhdl_identifier(
                                   index_definition.name, "character")
                            && owner && owner->vhdl != nullptr
                            && owner->vhdl->kind == vhdl::UnitKind::package
                            && same_vhdl_identifier(
                                owner->vhdl->library, "std")
                            && same_vhdl_identifier(
                                owner->vhdl->name, "standard");
                    }
                    if (index_definition.form != vhdl::TypeForm::subtype
                        && index_definition.form != vhdl::TypeForm::alias
                        && index_definition.form != vhdl::TypeForm::scalar) {
                        return false;
                    }
                    const auto declaration = find_declaration(
                        *design_, effective_,
                        index_definition.declaration);
                    if (declaration && declaration->vhdl != nullptr) {
                        index_scope = declaration->vhdl->scope;
                    }
                    index_type
                        = index_definition.base.type_mark.target.valid()
                        ? std::optional {
                              index_definition.base.type_mark.target }
                        : resolve_vhdl_named_type(
                              index_definition.base.type_mark.spelling,
                              index_scope);
                    if (!index_type) {
                        return unresolved_predefined_character(
                            index_definition.base.type_mark.spelling,
                            index_scope);
                    }
                }
                return false;
            };
            const auto enum_root_for_type = [&](TypeId type_id,
                                                ScopeId type_scope)
                -> std::optional<TypeId> {
                std::set<TypeId> visited;
                while (type_id.valid() && visited.insert(type_id).second) {
                    const auto type = find_type(*design_, effective_, type_id);
                    if (!type || type->vhdl == nullptr) {
                        return std::nullopt;
                    }
                    const auto& definition = *type->vhdl;
                    if (definition.form == vhdl::TypeForm::enumeration) {
                        return definition.enumeration_literals.empty()
                            ? std::nullopt
                            : std::optional { type_id };
                    }
                    if (definition.form != vhdl::TypeForm::subtype
                        && definition.form != vhdl::TypeForm::alias
                        && definition.form != vhdl::TypeForm::scalar) {
                        return std::nullopt;
                    }
                    const auto declaration = find_declaration(
                        *design_, effective_, definition.declaration);
                    if (declaration && declaration->vhdl != nullptr) {
                        type_scope = declaration->vhdl->scope;
                    }
                    type_id = definition.base.type_mark.target.valid()
                        ? definition.base.type_mark.target
                        : resolve_vhdl_named_type(
                              definition.base.type_mark.spelling, type_scope)
                              .value_or(TypeId { });
                }
                return std::nullopt;
            };
            const auto enum_root_for_declaration = [&](
                const DeclarationId id) -> std::optional<TypeId> {
                const auto declaration = find_declaration(
                    *design_, effective_, id);
                if (!declaration || declaration->vhdl == nullptr) {
                    return std::nullopt;
                }
                const auto& source = *declaration->vhdl;
                if (source.declared_type) {
                    return enum_root_for_type(
                        *source.declared_type, source.scope);
                }
                if (source.subtype) {
                    const auto& subtype = *source.subtype;
                    const auto type = subtype.type_mark.target.valid()
                        ? std::optional { subtype.type_mark.target }
                        : resolve_vhdl_named_type(
                              subtype.type_mark.spelling, source.scope);
                    return type
                        ? enum_root_for_type(*type, source.scope)
                        : std::nullopt;
                }
                return std::nullopt;
            };
            const auto enumeration_index_root = [&](
                const vhdl::ArrayDimension& dimension)
                -> std::optional<TypeId> {
                const auto& name = dimension.index_subtype;
                auto scope = definition_scope;
                std::optional<TypeId> type;
                if (name.selected) {
                    const auto declaration = find_declaration(
                        *design_, effective_, *name.selected);
                    if (!declaration || declaration->vhdl == nullptr) {
                        return std::nullopt;
                    }
                    scope = declaration->vhdl->scope;
                    if (declaration->vhdl->declared_type) {
                        type = *declaration->vhdl->declared_type;
                    } else if (declaration->vhdl->subtype) {
                        const auto& subtype = *declaration->vhdl->subtype;
                        type = subtype.type_mark.target.valid()
                            ? std::optional { subtype.type_mark.target }
                            : resolve_vhdl_named_type(
                                  subtype.type_mark.spelling, scope);
                    }
                } else {
                    type = resolve_vhdl_named_type(name.spelling, scope);
                }
                return type ? enum_root_for_type(*type, scope)
                            : std::nullopt;
            };
            struct EnumerationBoundaryValue {
                std::optional<std::int64_t> ordinal;
                bool uses_enum_context { };
            };
            struct EnumerationActualBinding {
                bool found { };
                std::optional<ExpressionId> expression;
                std::optional<DeclarationId> declaration;
            };
            constexpr auto maximum_enum_boundary_depth = std::size_t { 64U };
            std::function<EnumerationBoundaryValue(TypeId, ExpressionId,
                std::set<ExpressionId>&, std::set<DeclarationId>&,
                std::size_t)>
                evaluate_enum_expression;
            std::function<EnumerationBoundaryValue(TypeId, DeclarationId,
                std::set<ExpressionId>&, std::set<DeclarationId>&,
                std::size_t)>
                evaluate_enum_declaration;
            const auto enum_literal_member = [&](const TypeId root,
                                                 const DeclarationId id) {
                const auto type = find_type(*design_, effective_, root);
                return type && type->vhdl != nullptr
                    && std::ranges::any_of(
                        type->vhdl->enumeration_literals,
                        [&](const vhdl::EnumerationLiteral& literal) {
                            return literal.declaration == id;
                        });
            };
            const auto enum_literal_ordinal = [&](const TypeId root,
                                                  const ExpressionId id) {
                const auto expression
                    = find_expression(*design_, effective_, id);
                if (!expression || expression->vhdl == nullptr) {
                    return std::optional<std::int64_t> { };
                }
                const auto& source = *expression->vhdl;
                if (source.referenced_name) {
                    const auto& name = *source.referenced_name;
                    if (name.selected
                        && !enum_literal_member(root, *name.selected)) {
                        return std::optional<std::int64_t> { };
                    }
                    if (!name.selected && !name.overloads.empty()
                        && std::ranges::none_of(name.overloads,
                            [&](const DeclarationId candidate) {
                                return enum_literal_member(root, candidate);
                            })) {
                        return std::optional<std::int64_t> { };
                    }
                }
                const auto type = find_type(*design_, effective_, root);
                if (!type || type->vhdl == nullptr) {
                    return std::optional<std::int64_t> { };
                }
                const auto spelling = std::string_view { source.text };
                const auto literal = std::ranges::find_if(
                    type->vhdl->enumeration_literals,
                    [&](const vhdl::EnumerationLiteral& value) {
                        return spelling.starts_with('\'')
                            ? spelling == value.spelling
                            : same_vhdl_identifier(
                                  spelling, value.spelling);
                    });
                return literal
                        != type->vhdl->enumeration_literals.end()
                    ? std::optional<std::int64_t> {
                          static_cast<std::int64_t>(literal->ordinal) }
                    : std::optional<std::int64_t> { };
            };
            const auto enum_actual_binding = [&](const DeclarationId formal) {
                for (auto frame = active_frames.rbegin();
                     frame != active_frames.rend(); ++frame) {
                    const auto binding = std::ranges::find(
                        *frame, formal, &CompiledActualBinding::formal);
                    if (binding != frame->end()) {
                        return EnumerationActualBinding {
                            true, binding->expression,
                            binding->actual_declaration };
                    }
                }
                if (effective_ != nullptr) {
                    const auto& actuals
                        = effective_->specialization().actual_identities;
                    const auto actual = std::ranges::find(
                        actuals, formal,
                        &SpecializedHirActualIdentity::declaration);
                    if (actual != actuals.end()) {
                        return EnumerationActualBinding {
                            true, actual->actual_expression,
                            actual->actual_declaration };
                    }
                }
                return EnumerationActualBinding { };
            };
            evaluate_enum_declaration = [&](const TypeId root,
                const DeclarationId id,
                std::set<ExpressionId>& active_expressions,
                std::set<DeclarationId>& active_declarations,
                const std::size_t depth)
                -> EnumerationBoundaryValue {
                if (depth >= maximum_enum_boundary_depth
                    || !active_declarations.insert(id).second) {
                    return { std::nullopt, true };
                }
                struct DeclarationGuard {
                    std::set<DeclarationId>& active;
                    DeclarationId id;
                    ~DeclarationGuard() { active.erase(id); }
                } guard { active_declarations, id };
                const auto declaration = find_declaration(
                    *design_, effective_, id);
                if (!declaration || declaration->vhdl == nullptr) {
                    return { std::nullopt, true };
                }
                const auto form = declaration->vhdl->form;
                if (form != vhdl::DeclarationForm::constant
                    && form != vhdl::DeclarationForm::generic_constant) {
                    return { std::nullopt, true };
                }
                const auto declared_root = enum_root_for_declaration(id);
                if (!declared_root || *declared_root != root) {
                    return { std::nullopt, true };
                }
                if (form == vhdl::DeclarationForm::generic_constant) {
                    const auto binding = enum_actual_binding(id);
                    if (binding.found) {
                        if (binding.expression) {
                            return evaluate_enum_expression(
                                root, *binding.expression, active_expressions,
                                active_declarations, depth + 1U);
                        }
                        if (binding.declaration) {
                            return evaluate_enum_declaration(
                                root, *binding.declaration,
                                active_expressions, active_declarations,
                                depth + 1U);
                        }
                        // An explicit binding owns the generic even when it
                        // has no expression. Do not substitute its default.
                        return { std::nullopt, true };
                    }
                }
                if (!declaration->vhdl->initializer) {
                    return { std::nullopt, true };
                }
                return evaluate_enum_expression(root,
                    *declaration->vhdl->initializer, active_expressions,
                    active_declarations, depth + 1U);
            };
            evaluate_enum_expression = [&](const TypeId root,
                const ExpressionId id,
                std::set<ExpressionId>& active_expressions,
                std::set<DeclarationId>& active_declarations,
                const std::size_t depth)
                -> EnumerationBoundaryValue {
                if (depth >= maximum_enum_boundary_depth
                    || !active_expressions.insert(id).second) {
                    return { std::nullopt, true };
                }
                struct ExpressionGuard {
                    std::set<ExpressionId>& active;
                    ExpressionId id;
                    ~ExpressionGuard() { active.erase(id); }
                } guard { active_expressions, id };
                const auto expression
                    = find_expression(*design_, effective_, id);
                if (!expression || expression->vhdl == nullptr) {
                    return { std::nullopt, true };
                }
                const auto& source = *expression->vhdl;
                if (source.kind == vhdl::ExpressionKind::logic_literal) {
                    return { enum_literal_ordinal(root, id), true };
                }
                if (source.kind == vhdl::ExpressionKind::integer_literal
                    || source.kind == vhdl::ExpressionKind::real_literal
                    || source.kind == vhdl::ExpressionKind::string_literal) {
                    return { std::nullopt, true };
                }
                if (source.kind == vhdl::ExpressionKind::boolean_literal) {
                    return { enum_literal_ordinal(root, id), true };
                }
                if (source.kind == vhdl::ExpressionKind::name) {
                    const auto resolver = CompiledDesignResolver {
                        *design_, selected_unit_, effective_, active_frames
                    };
                    const auto selected = source.referenced_name
                            && source.referenced_name->selected
                        ? source.referenced_name->selected
                        : source.referenced_name
                            && source.referenced_name->overloads.size() == 1U
                        ? std::optional {
                              source.referenced_name->overloads.front() }
                        : resolver.resolve_expression_name(id).unique();
                    if (selected) {
                        const auto declaration = find_declaration(
                            *design_, effective_, *selected);
                        if (declaration && declaration->vhdl != nullptr
                            && declaration->vhdl->form
                                == vhdl::DeclarationForm::enumeration_literal) {
                            return enum_literal_member(root, *selected)
                                ? EnumerationBoundaryValue {
                                      enum_literal_ordinal(root, id), true }
                                : EnumerationBoundaryValue {
                                      std::nullopt, true };
                        }
                        return evaluate_enum_declaration(root, *selected,
                            active_expressions, active_declarations,
                            depth + 1U);
                    }
                    return { enum_literal_ordinal(root, id), true };
                }
                constexpr auto qualified_prefix
                    = vhdl::qualified_expression_prefix;
                if (source.kind == vhdl::ExpressionKind::call
                    && source.text.starts_with(qualified_prefix)) {
                    if (source.operands.size() != 1U
                        || !source.referenced_name) {
                        return { std::nullopt, true };
                    }
                    const auto& name = *source.referenced_name;
                    const auto selected_type = name.selected
                        ? name.selected
                        : name.overloads.size() == 1U
                        ? std::optional { name.overloads.front() }
                        : std::nullopt;
                    if (!selected_type) {
                        return { std::nullopt, true };
                    }
                    const auto qualified_root
                        = enum_root_for_declaration(*selected_type);
                    if (!qualified_root || *qualified_root != root) {
                        return { std::nullopt, true };
                    }
                    const auto value = evaluate_enum_expression(root,
                        source.operands.front(), active_expressions,
                        active_declarations, depth + 1U);
                    return value.uses_enum_context
                        ? value
                        : EnumerationBoundaryValue {
                              std::nullopt, false };
                }
                // Preserve the existing evaluator for range attributes and
                // other supported non-qualification expressions.
                return { std::nullopt, false };
            };
            const auto enum_boundary_context = [&](
                const std::size_t dimension,
                const std::optional<ExpressionId> residual) {
                if (!residual
                    || dimension >= definition.array_dimensions.size()) {
                    return EnumerationBoundaryValue { };
                }
                const auto enum_root = enumeration_index_root(
                    definition.array_dimensions[dimension]);
                if (!enum_root) {
                    return EnumerationBoundaryValue { };
                }
                std::set<ExpressionId> active_expressions;
                std::set<DeclarationId> active_declarations;
                return evaluate_enum_expression(
                    *enum_root, *residual, active_expressions,
                    active_declarations, 0U);
            };
            // A bound of a CHARACTER-indexed dimension written as a
            // character literal is its CHARACTER position (`'0' to '9'`).
            const auto dimension_boundary = [&](const std::size_t dimension,
                                                const std::optional<std::int64_t> folded,
                                                const std::optional<ExpressionId> residual,
                                                const std::span<const CompiledBindingFrame> active)
                -> std::optional<std::int64_t> {
                // (An earlier fold may have taken '0' as a BIT value.)
                if (residual && dimension < definition.array_dimensions.size()) {
                    const auto expression
                        = find_expression(*design_, effective_, *residual);
                    if (predefined_character_index(
                            definition.array_dimensions[dimension])
                        && expression
                        && expression->vhdl != nullptr
                        && expression->vhdl->kind == vhdl::ExpressionKind::logic_literal
                        && expression->vhdl->text.size() == 3U
                        && expression->vhdl->text.front() == '\'') {
                        return static_cast<std::int64_t>(
                            static_cast<unsigned char>(expression->vhdl->text[1]));
                    }
                    const auto contextual = enum_boundary_context(
                        dimension, residual);
                    if (contextual.uses_enum_context) {
                        if (!contextual.ordinal) {
                            unresolved_enum_boundaries_for(active)
                                .insert(*residual);
                        }
                        return contextual.ordinal;
                    }
                }
                return evaluate_boundary(folded, residual, active);
            };
            const auto materialized_array_range = [&](
                const vhdl::RangeConstraint& source,
                const std::int64_t left, const std::int64_t right,
                const std::span<const CompiledBindingFrame> active)
                -> std::optional<vhdl::RangeConstraint> {
                auto range = source;
                range.left = left;
                range.right = right;
                if (!range.direction_from_bounds) {
                    return range;
                }
                RangeDirectionPath path;
                const auto direction = range_source_direction(
                    range, active, path);
                if (!direction) {
                    return std::nullopt;
                }
                range.descending = *direction;
                range.null = range.descending
                    ? left < right
                    : left > right;
                range.direction_from_bounds = false;
                return range;
            };
            std::uint64_t count { 1U };
            // An element left unconstrained has no layout of its own
            // (IEEE 1076-2008 5.3.2.1); its nominal width is not its size.
            const bool open_element = element && element->unconstrained
                && std::ranges::none_of(element->constraints,
                    [](const vhdl::RangeConstraint& range) {
                        return range.kind == vhdl::RangeKind::array_index;
                    });
            bool concrete = element && !open_element
                && element->executable_width
                && *element->executable_width != 0U;
            const bool constrained_shape = !result.constraints.empty()
                || (!definition.array_dimensions.empty()
                    && std::ranges::all_of(
                        definition.array_dimensions,
                        [](const vhdl::ArrayDimension& dimension) {
                            return dimension.constraint.has_value();
                        }));
            result.unconstrained = !constrained_shape;
            const auto& constraints = result.constraints.empty()
                ? definition.array_dimensions
                : std::vector<vhdl::ArrayDimension> { };
            const bool collect_fixed_array_ranges
                = result.constraints.empty()
                && !definition.array_dimensions.empty()
                && std::ranges::all_of(
                    definition.array_dimensions,
                    [](const vhdl::ArrayDimension& dimension) {
                        return !dimension.unconstrained
                            && dimension.constraint
                            && dimension.constraint->kind
                                == vhdl::RangeKind::array_index;
                    });
            std::vector<vhdl::RangeConstraint> materialized_dimensions;
            if (collect_fixed_array_ranges) {
                materialized_dimensions.reserve(
                    definition.array_dimensions.size());
            }
            if (!result.constraints.empty()) {
                // Normalize only proven enum-context bounds before the
                // ordinary width loop. This clears stale generic folds from
                // all dimensions without evaluating later attributes or
                // functions after an earlier ordinary failure.
                for (std::size_t dimension_index { };
                     dimension_index < result.constraints.size(); ++dimension_index) {
                    auto& constraint = result.constraints[dimension_index];
                    const auto clear_or_set = [&](
                        const std::optional<ExpressionId> expression,
                        std::optional<std::int64_t>& endpoint) {
                        if (!expression) {
                            return;
                        }
                        const auto contextual = enum_boundary_context(
                            dimension_index, expression);
                        if (!contextual.uses_enum_context) {
                            return;
                        }
                        if (contextual.ordinal) {
                            endpoint = contextual.ordinal;
                        } else {
                            endpoint.reset();
                            unresolved_enum_boundaries_for(active_frames)
                                .insert(*expression);
                        }
                    };
                    clear_or_set(constraint.left_expression,
                        constraint.left);
                    clear_or_set(constraint.right_expression,
                        constraint.right);
                }
                for (std::size_t dimension_index { };
                     dimension_index < result.constraints.size(); ++dimension_index) {
                    auto& constraint = result.constraints[dimension_index];
                    const auto left = dimension_boundary(dimension_index,
                        constraint.left, constraint.left_expression,
                        active_frames);
                    const auto right = dimension_boundary(dimension_index,
                        constraint.right, constraint.right_expression,
                        active_frames);
                    if (!left || !right) {
                        concrete = false;
                        break;
                    }
                    constraint.left = left;
                    constraint.right = right;
                    const auto range = materialized_array_range(
                        constraint, *left, *right, active_frames);
                    if (!range) {
                        concrete = false;
                        break;
                    }
                    constraint = *range;
                    const auto dimension = concrete_array_range_length(
                        *range, *left, *right);
                    if (!dimension) {
                        concrete = false;
                        break;
                    }
                    if ((*dimension != 0U && count
                            > std::numeric_limits<std::uint64_t>::max()
                                / *dimension)) {
                        concrete = false;
                        break;
                    }
                    count *= *dimension;
                }
            } else {
                for (std::size_t dimension_index { };
                     dimension_index < constraints.size(); ++dimension_index) {
                    const auto& dimension = constraints[dimension_index];
                    if (!dimension.constraint) {
                        concrete = false;
                        break;
                    }
                    const auto left = dimension_boundary(dimension_index,
                        dimension.constraint->left,
                        dimension.constraint->left_expression,
                        active_frames);
                    const auto right = dimension_boundary(dimension_index,
                        dimension.constraint->right,
                        dimension.constraint->right_expression,
                        active_frames);
                    if (!left || !right) {
                        concrete = false;
                        break;
                    }
                    const auto range = materialized_array_range(
                        *dimension.constraint, *left, *right,
                        active_frames);
                    if (!range) {
                        concrete = false;
                        break;
                    }
                    if (collect_fixed_array_ranges) {
                        materialized_dimensions.push_back(*range);
                    }
                    const auto size = concrete_array_range_length(
                        *range, *left, *right);
                    if (!size) {
                        concrete = false;
                        break;
                    }
                    if ((*size != 0U && count
                            > std::numeric_limits<std::uint64_t>::max()
                                / *size)) {
                        concrete = false;
                        break;
                    }
                    count *= *size;
                }
            }
            if (collect_fixed_array_ranges
                && materialized_dimensions.size()
                    == definition.array_dimensions.size()) {
                // Keep the context-specific, source-resolved declared shape
                // on the returned subtype. The vector is committed only
                // after every fixed dimension materialized successfully.
                // The source TypeDefinition remains untouched.
                result.constraints = std::move(materialized_dimensions);
            }
            // A statically constrained null array is still a complete
            // executable layout. Preserve its zero width so callers can
            // distinguish it from an unresolved or unconstrained array.
            if (concrete
                && count <= std::numeric_limits<std::uint64_t>::max()
                    / *element->executable_width) {
                result.executable_width
                    = count * *element->executable_width;
                if (result.domain == vhdl::ValueDomain::unknown) {
                    result.domain = element->domain;
                }
                if (result.domain == vhdl::ValueDomain::integer
                    || result.domain == vhdl::ValueDomain::boolean) {
                    result.domain = vhdl::ValueDomain::bit2;
                }
            } else if (constrained_shape) {
                // Do not retain a width from an older, incomplete bound
                // evaluation when any declared or supplied dimension failed.
                result.executable_width.reset();
            }
        }
        active_types.erase(result.type_mark.target);
        if (compiler_owned_mark) {
            result.type_mark = *compiler_owned_mark;
        }
        complete_predefined(result, active_frames, scope);
        return result;
    };
    return resolve(input, use_scope, frames);
}

std::optional<vhdl::SubtypeIndication>
CompiledDesignResolver::effective_vhdl_constant_object_subtype(
    const DeclarationId declaration_id, const ScopeId use_scope) const
{
    if (design_ == nullptr) {
        return std::nullopt;
    }
    const auto declaration = find_declaration(
        *design_, effective_, declaration_id);
    if (!declaration || declaration->vhdl == nullptr
        || declaration->vhdl->form != vhdl::DeclarationForm::constant
        || !declaration->vhdl->subtype) {
        return std::nullopt;
    }
    auto subtype = effective_vhdl_subtype(
        *declaration->vhdl->subtype, use_scope);
    if (!subtype || !declaration->vhdl->initializer
        || !subtype->constraints.empty()) {
        return subtype;
    }

    auto type_id = subtype->type_mark.target;
    if (!type_id.valid()) {
        type_id = resolve_vhdl_named_type(
                      subtype->type_mark.spelling,
                      declaration->vhdl->scope)
                      .value_or(TypeId { });
    }
    std::unordered_set<std::uint32_t> visited_types;
    const vhdl::TypeDefinition* array { };
    while (type_id.valid()
        && visited_types.insert(type_id.value()).second) {
        const auto type = find_type(*design_, effective_, type_id);
        if (!type || type->vhdl == nullptr) {
            break;
        }
        if (type->vhdl->form == vhdl::TypeForm::array) {
            array = type->vhdl;
            break;
        }
        if ((type->vhdl->form != vhdl::TypeForm::subtype
                && type->vhdl->form != vhdl::TypeForm::alias)
            || !type->vhdl->base.type_mark.target.valid()) {
            break;
        }
        type_id = type->vhdl->base.type_mark.target;
    }
    if (array == nullptr || array->array_dimensions.size() != 2U
        || std::ranges::any_of(
            array->array_dimensions, [](const vhdl::ArrayDimension& dimension) {
                return !dimension.unconstrained || dimension.constraint;
            })) {
        return subtype;
    }

    const auto array_declaration = find_declaration(
        *design_, effective_, array->declaration);
    const auto array_scope = array_declaration
            && array_declaration->vhdl != nullptr
        ? array_declaration->vhdl->scope
        : declaration->vhdl->scope;
    const auto standard_natural = [&](const vhdl::ArrayDimension& dimension) {
        const auto retained_declaration = dimension.index_subtype.selected;
        auto index_declaration = retained_declaration;
        bool implicit_standard_natural { };
        if (!index_declaration) {
            auto spelling = std::string_view {
                dimension.index_subtype.spelling
            };
            spelling = vhdl::without_builtin_prefix(spelling);
            const auto resolved = resolve_vhdl_named_type(
                spelling, array_scope);
            if (resolved) {
                const auto type = find_type(*design_, effective_, *resolved);
                if (type && type->vhdl != nullptr) {
                    index_declaration = type->vhdl->declaration;
                }
            }
            if (!index_declaration) {
                vhdl::Name index_name;
                index_name.spelling = spelling;
                index_name.canonical = spelling;
                const auto visible_type = resolve_vhdl(
                    index_name, array_scope,
                    [](const CompiledDeclarationView&) { return true; });
                const bool standard_spelling
                    = same_vhdl_identifier(spelling, "natural")
                    || same_vhdl_identifier(
                        spelling, "standard.natural")
                    || same_vhdl_identifier(
                        spelling, "std.standard.natural");
                if (visible_type.status
                    == CompiledResolutionStatus::unique) {
                    index_declaration = visible_type.unique();
                } else if (visible_type.status
                        == CompiledResolutionStatus::not_found
                    && standard_spelling) {
                    // STD.STANDARD.NATURAL is implicitly visible. Some
                    // compiled bundles omit its declaration and TypeDefinition;
                    // accept that compiler-owned name only after scoped lookup
                    // has ruled out a local or imported declaration.
                    implicit_standard_natural = true;
                }
            }
        }
        const auto index = index_declaration
            ? find_declaration(*design_, effective_, *index_declaration)
            : std::nullopt;
        const auto* scope = index && index->vhdl != nullptr
            ? find_scope(*design_, index->vhdl->scope)
            : nullptr;
        const auto owner = scope != nullptr
            ? design_->find_unit(scope->unit)
            : std::nullopt;
        const bool named_natural = implicit_standard_natural
            || (index && index->vhdl != nullptr
            && index->vhdl->form == vhdl::DeclarationForm::subtype
            && same_vhdl_identifier(index->vhdl->name, "natural"));
        const bool standard_owner = implicit_standard_natural
            || (owner && owner->vhdl != nullptr
                && owner->vhdl->kind == vhdl::UnitKind::package
                && same_vhdl_identifier(owner->vhdl->library, "std")
                && same_vhdl_identifier(owner->vhdl->name, "standard"));
        return named_natural && standard_owner;
    };
    bool all_standard_natural = true;
    for (std::size_t dimension { };
         dimension < array->array_dimensions.size(); ++dimension) {
        all_standard_natural = standard_natural(
            array->array_dimensions[dimension])
            && all_standard_natural;
    }
    if (!all_standard_natural) {
        return subtype;
    }

    const auto positional_aggregate = [&](const ExpressionId expression_id)
        -> std::optional<std::pair<std::size_t, SourceSpanId>> {
        const auto expression
            = find_expression(*design_, effective_, expression_id);
        if (!expression || expression->vhdl == nullptr
            || expression->vhdl->kind != vhdl::ExpressionKind::aggregate
            || expression->vhdl->associations.empty()
            || !std::ranges::all_of(
                expression->vhdl->associations,
                [](const vhdl::AggregateAssociation& association) {
                    return association.positional();
                })
            || expression->vhdl->associations.size()
                > static_cast<std::size_t>(
                    std::numeric_limits<std::int32_t>::max()) + 1U) {
            return std::nullopt;
        }
        return std::pair {
            expression->vhdl->associations.size(),
            expression->vhdl->source
        };
    };
    const auto outer = positional_aggregate(
        *declaration->vhdl->initializer);
    if (!outer) {
        return subtype;
    }
    std::optional<std::pair<std::size_t, SourceSpanId>> inner;
    const auto initializer = find_expression(
        *design_, effective_, *declaration->vhdl->initializer);
    if (!initializer || initializer->vhdl == nullptr) {
        return subtype;
    }
    for (const auto& association : initializer->vhdl->associations) {
        const auto row = positional_aggregate(association.value);
        if (!row || (inner && inner->first != row->first)) {
            return subtype;
        }
        const auto row_expression
            = find_expression(*design_, effective_, association.value);
        if (!row_expression || row_expression->vhdl == nullptr
            || !std::ranges::all_of(row_expression->vhdl->associations,
                [&](const vhdl::AggregateAssociation& element) {
                    return (
                        find_expression(*design_, effective_, element.value))
                        .has_value();
                })) {
            return subtype;
        }
        if (!inner) {
            inner = row;
        }
    }
    if (!inner || outer->first == 0U || inner->first == 0U
        || outer->first - 1U > static_cast<std::size_t>(
            std::numeric_limits<std::int32_t>::max())
        || inner->first - 1U > static_cast<std::size_t>(
            std::numeric_limits<std::int32_t>::max())) {
        return subtype;
    }

    std::vector<vhdl::RangeConstraint> constraints(2U);
    for (std::size_t dimension { }; dimension < constraints.size(); ++dimension) {
        constraints[dimension].kind = vhdl::RangeKind::array_index;
        constraints[dimension].left = 0;
        constraints[dimension].right = static_cast<std::int64_t>(
            (dimension == 0U ? outer->first : inner->first) - 1U);
        constraints[dimension].descending = false;
        constraints[dimension].source
            = dimension == 0U ? outer->second : inner->second;
    }
    subtype->constraints = std::move(constraints);
    subtype->unconstrained = false;
    return effective_vhdl_subtype(*subtype, use_scope).value_or(
        std::move(*subtype));
}

std::optional<bool> CompiledDesignResolver::vhdl_array_shapes_match(
    const vhdl::SubtypeIndication& left, const ScopeId left_scope,
    const vhdl::SubtypeIndication& right, const ScopeId right_scope) const
{
    return vhdl_array_shapes_match(
        left, left_scope, right, right_scope, false);
}

std::optional<bool> CompiledDesignResolver::vhdl_array_lengths_match(
    const vhdl::SubtypeIndication& left, const ScopeId left_scope,
    const vhdl::SubtypeIndication& right, const ScopeId right_scope) const
{
    return vhdl_array_shapes_match(
        left, left_scope, right, right_scope, true);
}

std::optional<bool> CompiledDesignResolver::vhdl_array_shapes_match(
    const vhdl::SubtypeIndication& left, const ScopeId left_scope,
    const vhdl::SubtypeIndication& right, const ScopeId right_scope,
    const bool compare_lengths) const
{
    if (design_ == nullptr) {
        return std::nullopt;
    }
    const auto effective_left = effective_vhdl_subtype(
        left, left_scope);
    const auto effective_right = effective_vhdl_subtype(
        right, right_scope);
    if (!effective_left || !effective_right
        || vhdl_profile_type_class(
               *design_, effective_, *effective_left)
            != VhdlProfileTypeClass::array
        || vhdl_profile_type_class(
               *design_, effective_, *effective_right)
            != VhdlProfileTypeClass::array) {
        return std::nullopt;
    }

    const auto array_constraints = [&](
                                       const vhdl::SubtypeIndication& subtype)
        -> std::optional<std::vector<vhdl::RangeConstraint>> {
        if (!subtype.constraints.empty()) {
            return subtype.constraints;
        }
        auto type = subtype.type_mark.target;
        std::set<TypeId> visiting;
        while (type.valid() && visiting.insert(type).second) {
            const auto fallback = find_type(*design_, effective_, type);
            if (!fallback || fallback->vhdl == nullptr) {
                break;
            }
            const auto& definition = *fallback->vhdl;
            if (definition.form == vhdl::TypeForm::array) {
                std::vector<vhdl::RangeConstraint> constraints;
                constraints.reserve(definition.array_dimensions.size());
                for (const auto& dimension : definition.array_dimensions) {
                    if (!dimension.constraint) {
                        return std::nullopt;
                    }
                    constraints.push_back(*dimension.constraint);
                }
                return constraints;
            }
            if ((definition.form != vhdl::TypeForm::subtype
                    && definition.form != vhdl::TypeForm::alias
                    && definition.form != vhdl::TypeForm::scalar)
                || !definition.base.type_mark.target.valid()) {
                break;
            }
            type = definition.base.type_mark.target;
        }
        return std::nullopt;
    };
    const auto left_constraints = array_constraints(*effective_left);
    const auto right_constraints = array_constraints(*effective_right);
    if (!left_constraints || !right_constraints) {
        if (effective_left->unconstrained
            != effective_right->unconstrained) {
            return false;
        }
        return std::nullopt;
    }
    if (left_constraints->size() != right_constraints->size()) {
        return false;
    }

    bool residual { };
    const auto boundary = [&](const vhdl::RangeConstraint& constraint,
                              const bool left_boundary) {
        const auto folded = left_boundary
            ? constraint.left : constraint.right;
        const auto expression = left_boundary
            ? constraint.left_expression : constraint.right_expression;
        if (folded) {
            return folded;
        }
        return expression && effective_ != nullptr
            ? effective_->evaluate_integral_expression(*expression)
            : std::nullopt;
    };
    for (std::size_t index { }; index < left_constraints->size(); ++index) {
        const auto& left_dimension = (*left_constraints)[index];
        const auto& right_dimension = (*right_constraints)[index];
        if (!compare_lengths
            && (left_dimension.direction_from_bounds
                || right_dimension.direction_from_bounds)) {
            residual = true;
            continue;
        }
        if (!compare_lengths
            && (left_dimension.descending != right_dimension.descending
                || left_dimension.null != right_dimension.null)) {
            return false;
        }
        const auto left_first = boundary(left_dimension, true);
        const auto left_last = boundary(left_dimension, false);
        const auto right_first = boundary(right_dimension, true);
        const auto right_last = boundary(right_dimension, false);
        if (!left_first || !left_last || !right_first || !right_last) {
            residual = true;
            continue;
        }
        if (compare_lengths) {
            if ((left_dimension.direction_from_bounds
                    && *left_first != *left_last)
                || (right_dimension.direction_from_bounds
                    && *right_first != *right_last)) {
                residual = true;
                continue;
            }
            const auto is_null = [](const vhdl::RangeConstraint& range,
                                     const std::int64_t first,
                                     const std::int64_t last) {
                return range.null
                    || (range.descending ? first < last : first > last);
            };
            const bool left_null = is_null(
                left_dimension, *left_first, *left_last);
            const bool right_null = is_null(
                right_dimension, *right_first, *right_last);
            if (left_null != right_null) {
                return false;
            }
            // Compare inclusive lengths without adding one: a range spanning
            // the complete int64 domain has an unrepresentable uint64 length.
            const auto distance = [](const std::int64_t first,
                                      const std::int64_t last) {
                return support::index_distance(first, last);
            };
            if (!left_null
                && distance(*left_first, *left_last)
                    != distance(*right_first, *right_last)) {
                return false;
            }
        } else if (*left_first != *right_first || *left_last != *right_last) {
            return false;
        }
    }
    return residual ? std::nullopt : std::optional<bool> { true };
}

bool CompiledDesignResolver::vhdl_subtype_profiles_match(
    const vhdl::SubtypeIndication& left, const ScopeId left_scope,
    const vhdl::SubtypeIndication& right, const ScopeId right_scope) const
{
    if (design_ == nullptr) {
        return false;
    }
    // `T'element` and `T'index(N)` conform when their prefixes do (IEEE
    // 1076-2008 4.10, 16.2.3), whether or not T is bound yet.
    if (left.predefined_attribute && right.predefined_attribute
        && *left.predefined_attribute == *right.predefined_attribute
        && left.constraints.empty() && right.constraints.empty()) {
        const auto dimension = [&](const vhdl::SubtypeIndication& subtype)
            -> std::optional<std::int64_t> {
            if (!subtype.predefined_attribute_dimension) {
                return 1;
            }
            return effective_ != nullptr
                ? effective_->evaluate_integral_expression(
                      *subtype.predefined_attribute_dimension)
                : std::nullopt;
        };
        const auto left_dimension = dimension(left);
        if (left_dimension && left_dimension == dimension(right)) {
            auto left_prefix = left;
            auto right_prefix = right;
            for (auto* prefix : { &left_prefix, &right_prefix }) {
                prefix->predefined_attribute.reset();
                prefix->predefined_attribute_dimension.reset();
            }
            if (vhdl_subtype_profiles_match(
                    left_prefix, left_scope, right_prefix, right_scope)) {
                return true;
            }
        }
    }
    auto effective_left = effective_vhdl_subtype(
        left, left_scope).value_or(left);
    auto effective_right = effective_vhdl_subtype(
        right, right_scope).value_or(right);
    const auto normalize_standard_array_placeholder = [](
        vhdl::SubtypeIndication& subtype) {
        if (subtype.type_mark.target.valid()
            || !subtype.constraints.empty()) {
            return;
        }
        std::uint64_t element_width { };
        switch (subtype.builtin_type) {
        case vhdl::BuiltinTypeIdentity::std_standard_string:
            element_width = 8U;
            break;
        case vhdl::BuiltinTypeIdentity::std_standard_bit_vector:
            element_width = 1U;
            break;
        default:
            return;
        }
        if (subtype.executable_width
            && *subtype.executable_width == element_width) {
            subtype.unconstrained = true;
            subtype.executable_width.reset();
        }
    };
    normalize_standard_array_placeholder(effective_left);
    normalize_standard_array_placeholder(effective_right);
    // The same open generic (or an open part of one, `T'element`) on both
    // sides conforms (IEEE 1076-2019 4.10, 6.5.3).
    if (effective_left.unspecified_class
            != vhdl::UnspecifiedTypeClass::none
        && effective_left.unspecified_class
            == effective_right.unspecified_class
        && !effective_left.unspecified_inference_identity.empty()
        && effective_left.unspecified_inference_identity
            == effective_right.unspecified_inference_identity) {
        return true;
    }
    // FSIM_VHDL_RESOLVE_TRACE names both sides of a failed match.
    const auto mismatch = [&] {
        if (vhdl_resolve_trace_enabled) {
            std::fprintf(stderr,
                "FSIM_VHDL_RESOLVE profile-mismatch left=%s/%u/%s/%d/%d/%d "
                "right=%s/%u/%s/%d/%d/%d frames=%zu\n",
                effective_left.type_mark.spelling.c_str(),
                effective_left.type_mark.target.value(),
                effective_left.unspecified_inference_identity.c_str(),
                static_cast<int>(effective_left.unspecified_class),
                static_cast<int>(effective_left.predefined_attribute
                        ? static_cast<int>(*effective_left.predefined_attribute)
                        : -1),
                static_cast<int>(effective_vhdl_subtype(left, left_scope)
                        .has_value()),
                effective_right.type_mark.spelling.c_str(),
                effective_right.type_mark.target.value(),
                effective_right.unspecified_inference_identity.c_str(),
                static_cast<int>(effective_right.unspecified_class),
                static_cast<int>(effective_right.predefined_attribute
                        ? static_cast<int>(*effective_right.predefined_attribute)
                        : -1),
                static_cast<int>(effective_vhdl_subtype(right, right_scope)
                        .has_value()),
                binding_frames_.size());
        }
        return false;
    };
    if (effective_left.unspecified_class
        != vhdl::UnspecifiedTypeClass::none) {
        return vhdl_unspecified_profile_accepts(
                   effective_left.unspecified_class,
                   vhdl_profile_type_class(
                       *design_, effective_, effective_right))
            || mismatch();
    }
    if (effective_right.unspecified_class
        != vhdl::UnspecifiedTypeClass::none) {
        return vhdl_unspecified_profile_accepts(
                   effective_right.unspecified_class,
                   vhdl_profile_type_class(
                       *design_, effective_, effective_left))
            || mismatch();
    }
    if (!same_vhdl_subtype(
            *design_, effective_, effective_left, effective_right)) {
        return mismatch();
    }
    const auto shapes = vhdl_array_shapes_match(
        effective_left, left_scope, effective_right, right_scope);
    return !shapes || *shapes;
}

std::optional<CompiledBindingFrame>
CompiledDesignResolver::bind_vhdl_generics(
    const std::span<const DeclarationId> formals,
    const std::span<const vhdl::Association> associations,
    const ScopeId use_scope) const
{
    CompiledBindingFrame result;
    result.reserve(formals.size());
    for (const auto formal : formals) {
        CompiledActualBinding binding;
        binding.formal = formal;
        result.push_back(std::move(binding));
    }
    std::vector<bool> bound(formals.size());
    std::vector<bool> defaulted(formals.size());
    std::size_t positional { };
    for (const auto& association : associations) {
        std::optional<std::size_t> selected;
        if (association.formal) {
            // An operator symbol formal (`"=" => eq`) matches its designator
            // with or without the quotes (IEEE 1076-2008 4.2.1).
            const auto unquoted = [](std::string_view text) {
                if (text.size() >= 2U && text.front() == '"'
                    && text.back() == '"') {
                    text = text.substr(1U, text.size() - 2U);
                }
                return text;
            };
            const auto name = unquoted(vhdl_spelling(*association.formal));
            for (std::size_t index { }; index < formals.size(); ++index) {
                const auto declaration = find_declaration(
                    *design_, effective_, formals[index]);
                if (declaration && declaration->vhdl != nullptr
                    && same_vhdl_identifier(
                        unquoted(declaration->vhdl->name), name)) {
                    if (selected) {
                        return std::nullopt;
                    }
                    selected = index;
                }
            }
        } else {
            while (positional < bound.size() && bound[positional]) {
                ++positional;
            }
            if (positional < bound.size()) {
                selected = positional++;
            }
        }
        if (!selected || bound[*selected]) {
            return std::nullopt;
        }
        bound[*selected] = true;
        defaulted[*selected]
            = association.kind == vhdl::AssociationKind::default_box;
        if (!defaulted[*selected]) {
            result[*selected].expression = association.expression;
            result[*selected].vhdl_type = association.type;
        }
    }
    for (std::size_t index { }; index < formals.size(); ++index) {
        const auto declaration = find_declaration(
            *design_, effective_, formals[index]);
        if (!declaration || declaration->vhdl == nullptr) {
            return std::nullopt;
        }
        auto& binding = result[index];
        if (!bound[index] || defaulted[index]) {
            // An omitted/default-box association may intentionally forward
            // an outer generic actual.  An explicit association, however,
            // is the innermost binding and must not be shadowed by an older
            // specialization binding for the same declaration identity.
            binding.actual_declaration
                = actual_declaration(formals[index]);
            binding.expression = declaration->vhdl->initializer;
            binding.vhdl_type = declaration->vhdl->default_type;
        }
        if (!binding.actual_declaration && binding.expression) {
            if (const auto selected = resolve_expression_name(
                    *binding.expression, { }, true).unique()) {
                binding.actual_declaration
                    = actual_declaration(*selected).value_or(*selected);
            }
        }
        // An overloaded subprogram name actual denotes the overload whose
        // profile conforms to the formal's, after the generic bindings made
        // so far (IEEE 1076-2008 6.5.7.2).
        if (!binding.actual_declaration && binding.expression
            && bound[index] && !defaulted[index]
            && (declaration->vhdl->form
                    == vhdl::DeclarationForm::generic_function
                || declaration->vhdl->form
                    == vhdl::DeclarationForm::generic_procedure)) {
            const auto actual = find_expression(
                *design_, effective_, *binding.expression);
            if (actual && actual->vhdl != nullptr
                && actual->vhdl->kind == vhdl::ExpressionKind::name
                && actual->vhdl->referenced_name) {
                std::vector<CompiledBindingFrame> frames {
                    binding_frames_.begin(), binding_frames_.end()
                };
                frames.push_back(result);
                const CompiledDesignResolver bound_resolver {
                    *design_, selected_unit_, effective_, frames
                };
                if (const auto selected
                    = bound_resolver
                          .resolve_vhdl_default_callable(formals[index],
                              use_scope.valid() ? use_scope
                                                : actual->vhdl->scope,
                              &*actual->vhdl->referenced_name)
                          .unique()) {
                    binding.actual_declaration = selected->key;
                }
            }
        }
        if (declaration->vhdl->form
                == vhdl::DeclarationForm::generic_type
            && !binding.vhdl_type && binding.expression) {
            auto expression
                = find_expression(*design_, effective_, *binding.expression);
            // A constrained type mark as the actual (`t => bit_vector(7
            // downto 0)`, IEEE 1076-2008 6.5.7.2, 6.3) is the type mark
            // with its index constraints: `T(l to r)` is a slice of the
            // mark, `T(c1, c2)` a call whose operands are the ranges.
            std::vector<vhdl::RangeConstraint> actual_constraints;
            if (expression && expression->vhdl != nullptr) {
                const auto range_of = [&](const ExpressionId range_id)
                    -> std::optional<vhdl::RangeConstraint> {
                    const auto range = find_expression(
                        *design_, effective_, range_id);
                    if (!range || range->vhdl == nullptr
                        || range->vhdl->kind
                            != vhdl::ExpressionKind::binary
                        || range->vhdl->operands.size() != 2U
                        || (!same_vhdl_identifier(range->vhdl->text, "to")
                            && !same_vhdl_identifier(
                                range->vhdl->text, "downto"))) {
                        return std::nullopt;
                    }
                    vhdl::RangeConstraint constraint;
                    constraint.kind = vhdl::RangeKind::array_index;
                    constraint.left_expression = range->vhdl->operands[0];
                    constraint.right_expression = range->vhdl->operands[1];
                    constraint.descending = same_vhdl_identifier(
                        range->vhdl->text, "downto");
                    constraint.source = range->vhdl->source;
                    return constraint;
                };
                const auto& written = *expression->vhdl;
                if (written.kind == vhdl::ExpressionKind::slice
                    && written.operands.size() == 3U
                    && (same_vhdl_identifier(written.text, "to")
                        || same_vhdl_identifier(written.text, "downto"))) {
                    vhdl::RangeConstraint constraint;
                    constraint.kind = vhdl::RangeKind::array_index;
                    constraint.left_expression = written.operands[1];
                    constraint.right_expression = written.operands[2];
                    constraint.descending
                        = same_vhdl_identifier(written.text, "downto");
                    constraint.source = written.source;
                    actual_constraints.push_back(std::move(constraint));
                    expression = find_expression(
                        *design_, effective_, written.operands.front());
                } else if (written.kind == vhdl::ExpressionKind::call
                    && written.referenced_name
                    && !written.operands.empty()) {
                    for (const auto operand : written.operands) {
                        auto constraint = range_of(operand);
                        if (!constraint) {
                            actual_constraints.clear();
                            break;
                        }
                        actual_constraints.push_back(std::move(*constraint));
                    }
                }
            }
            // `obj'subtype` as the actual denotes the subtype of the
            // object (IEEE 1076-2019 16.2.2), the form a call's generic map
            // uses for its argument's type (9.3.4).
            if (expression && expression->vhdl != nullptr
                && expression->vhdl->kind == vhdl::ExpressionKind::call
                && expression->vhdl->text == "'subtype"
                && expression->vhdl->operands.size() == 1U) {
                const auto prefix = find_expression(*design_, effective_,
                    expression->vhdl->operands.front());
                const auto* name = prefix && prefix->vhdl != nullptr
                        && prefix->vhdl->referenced_name
                    ? &*prefix->vhdl->referenced_name
                    : nullptr;
                const auto selected = name == nullptr ? std::nullopt
                    : name->selected                  ? name->selected
                    : name->overloads.size() == 1U
                    ? std::optional { name->overloads.front() }
                    : std::nullopt;
                const auto object = selected
                    ? find_declaration(*design_, effective_, *selected)
                    : std::nullopt;
                if (object && object->vhdl != nullptr
                    && object->vhdl->subtype
                    && !object->vhdl->subtype->has_unspecified_parts()) {
                    binding.vhdl_type = *object->vhdl->subtype;
                }
            }
            if (!binding.vhdl_type && expression
                && expression->vhdl != nullptr
                && (expression->vhdl->kind == vhdl::ExpressionKind::name
                    || !actual_constraints.empty())) {
                vhdl::SubtypeIndication subtype;
                if (expression->vhdl->referenced_name) {
                    const auto& name
                        = *expression->vhdl->referenced_name;
                    subtype.type_mark.spelling = std::string {
                        vhdl_spelling(name) };
                    const auto selected = name.selected
                        ? name.selected
                        : name.overloads.size() == 1U
                        ? std::optional { name.overloads.front() }
                        : std::nullopt;
                    const auto actual = selected
                        ? find_declaration(
                              *design_, effective_, *selected)
                        : std::nullopt;
                    if (actual && actual->vhdl != nullptr) {
                        if (actual->vhdl->declared_type) {
                            subtype.type_mark.target
                                = *actual->vhdl->declared_type;
                        }
                        if (actual->vhdl->subtype) {
                            subtype = *actual->vhdl->subtype;
                        }
                    }
                    // `inst.t` naming a generic type of another package
                    // instance denotes that instance's actual (IEEE
                    // 1076-2008 6.5.3, 4.9).
                    if (actual && actual->vhdl != nullptr
                        && actual->vhdl->form
                            == vhdl::DeclarationForm::generic_type) {
                        const auto members = resolve_vhdl_package_members(
                            name,
                            use_scope.valid() ? use_scope
                                              : expression->vhdl->scope,
                            {});
                        if (members.candidates.size() == 1U
                            && members.candidates.front().package_instance) {
                            const auto& frame
                                = members.candidates.front().generic_bindings;
                            const auto chained = std::ranges::find(frame,
                                *selected, &CompiledActualBinding::formal);
                            if (chained != frame.end()
                                && chained->vhdl_type) {
                                subtype = *chained->vhdl_type;
                            }
                        }
                    }
                } else {
                    subtype.type_mark.spelling
                        = expression->vhdl->text;
                }
                const auto spelling = simple_vhdl_name(
                    subtype.type_mark.spelling);
                if (same_vhdl_identifier(spelling, "integer")
                    || same_vhdl_identifier(spelling, "natural")
                    || same_vhdl_identifier(spelling, "positive")) {
                    subtype.domain = vhdl::ValueDomain::integer;
                } else if (same_vhdl_identifier(
                               spelling, "boolean")) {
                    subtype.domain = vhdl::ValueDomain::boolean;
                } else if (same_vhdl_identifier(spelling, "string")) {
                    subtype.domain = vhdl::ValueDomain::string;
                } else if (same_vhdl_identifier(spelling, "bit")) {
                    subtype.domain = vhdl::ValueDomain::bit2;
                }
                if (!actual_constraints.empty()) {
                    subtype.constraints = std::move(actual_constraints);
                    subtype.unconstrained = false;
                    subtype.executable_width.reset();
                }
                if (!subtype.type_mark.spelling.empty()
                    || subtype.type_mark.target.valid()) {
                    binding.vhdl_type = std::move(subtype);
                }
            }
        }
        if (!binding.expression && !binding.vhdl_type
            && !binding.actual_declaration) {
            const auto form = declaration->vhdl->form;
            if ((form == vhdl::DeclarationForm::generic_function
                    || form
                        == vhdl::DeclarationForm::generic_procedure)
                && declaration->vhdl->callable
                && (declaration->vhdl->callable->default_callable
                    || declaration->vhdl->callable->default_box)) {
                // The formal's profile names earlier generics of the same
                // list (`function "+"(l, r : t) return t is <>`): match the
                // default with the bindings made so far (IEEE 1076-2008
                // 6.5.6.2).
                std::vector<CompiledBindingFrame> frames {
                    binding_frames_.begin(), binding_frames_.end()
                };
                frames.push_back(result);
                const CompiledDesignResolver bound_resolver {
                    *design_, selected_unit_, effective_, frames
                };
                const auto resolved
                    = bound_resolver.resolve_vhdl_default_callable(
                    formals[index],
                    use_scope.valid()
                        ? use_scope
                        : declaration->vhdl->scope,
                    declaration->vhdl->callable->default_callable
                        ? &*declaration->vhdl->callable->default_callable
                        : nullptr);
                const auto selected = resolved.unique();
                // An operator box default with no visible declared match
                // denotes the predefined operation (IEEE 1076-2008 9.2).
                // So does a box default naming an implicitly declared
                // operation such as TO_STRING (5.2.6, 5.3.2.4, 5.7).
                const auto implicit_operation = [](const std::string_view n) {
                    return same_vhdl_identifier(n, "to_string")
                        || same_vhdl_identifier(n, "minimum")
                        || same_vhdl_identifier(n, "maximum")
                        || same_vhdl_identifier(n, "to_hstring")
                        || same_vhdl_identifier(n, "to_ostring");
                };
                const bool predefined_operator
                    = !selected && resolved.candidates.empty()
                    && declaration->vhdl->callable->default_box
                    && (vhdl::operator_designator(declaration->vhdl->name)
                        || implicit_operation(declaration->vhdl->name));
                if (!selected && !predefined_operator) {
                    return std::nullopt;
                }
                if (selected) {
                    binding.actual_declaration = selected->key;
                }
            } else if (form != vhdl::DeclarationForm::generic_function
                && form != vhdl::DeclarationForm::generic_procedure
                && form != vhdl::DeclarationForm::generic_package) {
                return std::nullopt;
            }
        }
    }
    // A type actual is a type mark of the association's context, not of the
    // formal's (IEEE 1076-2008 6.5.7.2).
    for (std::size_t index { }; index < formals.size(); ++index) {
        auto& actual = result[index].vhdl_type;
        if (!bound[index] || defaulted[index] || !actual
            || actual->type_mark.target.valid()
            || actual->builtin_type != vhdl::BuiltinTypeIdentity::none
            || actual->type_mark.spelling.empty()
            || actual->type_mark.spelling.starts_with(vhdl::builtin_prefix)
            || !use_scope.valid()) {
            continue;
        }
        const auto declaration = find_declaration(
            *design_, effective_, formals[index]);
        if (!declaration || declaration->vhdl == nullptr
            || declaration->vhdl->form
                != vhdl::DeclarationForm::generic_type) {
            continue;
        }
        if (const auto named = resolve_vhdl_named_type(
                actual->type_mark.spelling, use_scope)) {
            actual->type_mark.target = *named;
        } else if (same_vhdl_identifier(
                       simple_vhdl_name(actual->type_mark.spelling),
                       "line")) {
            // STD.TEXTIO.LINE has no compiled declaration; its objects are
            // dynamic strings, as the frontend types a LINE object.
            actual->domain = vhdl::ValueDomain::string;
        }
    }
    return result;
}

std::optional<CompiledVhdlPackageInstanceBinding>
CompiledDesignResolver::vhdl_package_instance_binding(
    const DeclarationId instance_id, const ScopeId use_scope) const
{
    const auto instance = find_declaration(*design_, effective_, instance_id);
    if (!instance || instance->vhdl == nullptr
        || instance->vhdl->form != vhdl::DeclarationForm::package_instance
        || !instance->vhdl->package) {
        return std::nullopt;
    }
    const auto lookup_units = vhdl_lookup_units(
        *design_, selected_unit_,
        use_scope.valid() ? use_scope : instance->vhdl->scope);
    if (lookup_units.empty()) {
        return std::nullopt;
    }
    const auto& profile = *instance->vhdl->package;
    auto template_name = vhdl_spelling(profile.template_name);
    auto library = normalized_library(lookup_units.front()->library);
    if (const auto separator = template_name.rfind('.');
        separator != std::string_view::npos) {
        library = effective_vhdl_library(
            template_name.substr(0U, separator),
            lookup_units.front()->library);
        template_name.remove_prefix(separator + 1U);
    }
    for (const auto& package : design_->vhdl_units()) {
        if (package.kind != vhdl::UnitKind::package
            || !package.primary_name.empty()
            || !same_vhdl_identifier(package.name, template_name)
            || !same_vhdl_identifier(
                normalized_library(package.library), library)) {
            continue;
        }
        std::vector<DeclarationId> formals;
        for (const auto id : package.declarations) {
            const auto declaration = find_declaration(
                *design_, effective_, id);
            if (declaration && declaration->vhdl != nullptr
                && vhdl::is_generic_formal(declaration->vhdl->form)) {
                formals.push_back(id);
            }
        }
        if (auto bindings = bind_vhdl_generics(
                formals, profile.generic_map, instance->vhdl->scope)) {
            return CompiledVhdlPackageInstanceBinding {
                package.id, std::move(*bindings) };
        }
    }
    return std::nullopt;
}

const vhdl::Unit* CompiledDesignResolver::vhdl_library_package_actual(
    const ExpressionId actual) const
{
    if (design_ == nullptr) {
        return nullptr;
    }
    const auto expression = find_expression(*design_, effective_, actual);
    if (!expression || expression->vhdl == nullptr
        || expression->vhdl->kind != vhdl::ExpressionKind::name) {
        return nullptr;
    }
    const auto& source = *expression->vhdl;
    const auto parts = vhdl_name_parts(source.referenced_name
            ? source.referenced_name->text()
            : std::string_view { source.text });
    if (parts.size() != 2U) {
        return nullptr;
    }
    const auto lookup_units = vhdl_lookup_units(
        *design_, selected_unit_, source.scope);
    if (lookup_units.empty()) {
        return nullptr;
    }
    const auto library = effective_vhdl_library(
        parts.front(), lookup_units.front()->library);
    for (const auto& package : design_->vhdl_units()) {
        if (package.kind != vhdl::UnitKind::package
            || !package.primary_name.empty()
            || !same_vhdl_identifier(package.name, parts.back())
            || !same_vhdl_identifier(
                normalized_library(package.library), library)) {
            continue;
        }
        // A generic package is a template, not an instance.
        const bool generic = std::ranges::any_of(package.declarations,
            [&](const DeclarationId id) {
                const auto declaration
                    = find_declaration(*design_, effective_, id);
                return declaration && declaration->vhdl != nullptr
                    && vhdl::is_generic_formal(declaration->vhdl->form);
            });
        return generic ? nullptr : &package;
    }
    return nullptr;
}

CompiledVhdlPackageResolution
CompiledDesignResolver::resolve_vhdl_package_members(
    const vhdl::Name& name, const ScopeId use_scope,
    const CompiledDeclarationPredicate& predicate) const
{
    const auto parts = vhdl_name_parts(vhdl_spelling(name));
    if (parts.empty() || parts.size() > 3U) {
        return { parts.empty() ? CompiledResolutionStatus::invalid
                               : CompiledResolutionStatus::not_found,
            { } };
    }
    const auto lookup_units = vhdl_lookup_units(
        *design_, selected_unit_, use_scope);
    if (lookup_units.empty()) {
        return { CompiledResolutionStatus::invalid, { } };
    }
    const auto* owner = lookup_units.front();

    std::vector<CompiledVhdlPackageMember> candidates;
    // Every template path searches the requested final name component.
    const auto indexed_members
        = design_->vhdl_package_members(parts.back());
    const bool indexed_empty
        = indexed_members && indexed_members->empty();
    if (semantic_profile_diagnostics_enabled) {
        profile_vhdl_package_member_lookup(indexed_empty);
    }
    if (indexed_empty) {
        return { CompiledResolutionStatus::not_found, { } };
    }
    const auto append_template = [&](const std::string_view library,
                                     const std::string_view package_name,
                                     const std::string_view member,
                                     const std::optional<DeclarationId> instance,
                                     const CompiledBindingFrame* bindings) {
        const auto append_member = [&](const UnitId template_unit,
                                       const DeclarationId member_id) {
            const auto member_view = find_declaration(
                *design_, effective_, member_id);
            if (!member_view
                || (predicate && !predicate(*member_view))) {
                return;
            }
            CompiledVhdlPackageMember candidate;
            candidate.member = member_id;
            candidate.package_instance = instance;
            candidate.template_unit = template_unit;
            if (bindings != nullptr) {
                candidate.generic_bindings = *bindings;
            }
            candidates.push_back(std::move(candidate));
        };
        if (indexed_members) {
            for (const auto& entry : *indexed_members) {
                if (!same_vhdl_identifier(entry.library, library)
                    || !same_vhdl_identifier(
                        entry.package, package_name)) {
                    continue;
                }
                append_member(entry.template_unit, entry.member);
            }
            return;
        }
        for (const auto& package : design_->vhdl_units()) {
            if (package.kind != vhdl::UnitKind::package
                || !package.primary_name.empty()
                || !same_vhdl_identifier(package.name, package_name)
                || !same_vhdl_identifier(
                    normalized_library(package.library), library)) {
                continue;
            }
            for (const auto declaration_id : package.declarations) {
                const auto declaration = find_declaration(
                    *design_, effective_, declaration_id);
                if (!declaration || declaration->vhdl == nullptr) {
                    continue;
                }
                if (same_vhdl_identifier(
                        declaration->vhdl->name, member)) {
                    append_member(package.id, declaration_id);
                }
                if (!declaration->vhdl->declared_type) {
                    continue;
                }
                const auto type = find_type(
                    *design_, effective_, *declaration->vhdl->declared_type);
                if (!type || type->vhdl == nullptr) {
                    continue;
                }
                for (const auto& literal :
                    type->vhdl->enumeration_literals) {
                    if (same_vhdl_identifier(
                        literal.spelling, member)) {
                        append_member(package.id, literal.declaration);
                    }
                }
                for (const auto& unit : type->vhdl->physical_units) {
                    if (same_vhdl_identifier(unit.name, member)) {
                        append_member(package.id, unit.declaration);
                    }
                }
            }
        }
    };

    const auto append_instance = [&](const DeclarationId instance_id,
                                     const std::string_view member) {
        // Binding an instance's generics can resolve a default subprogram
        // (`is <>`) that names the instance again; it is not its own actual.
        thread_local std::set<std::uint32_t> binding_instances;
        if (!binding_instances.insert(instance_id.value()).second) {
            return;
        }
        struct BindingGuard {
            std::uint32_t id;
            ~BindingGuard() { binding_instances.erase(id); }
        } binding_guard { instance_id.value() };
        auto binding = vhdl_package_instance_binding(instance_id, use_scope);
        const auto package = binding
            ? design_->find_unit(binding->template_unit)
            : std::nullopt;
        if (!package || package->vhdl == nullptr) {
            return;
        }
        append_template(normalized_library(package->vhdl->library),
            package->vhdl->name, member, instance_id,
            &binding->generic_bindings);
    };

    // The package instances named `name` in the nearest enclosing scope of
    // the use site that declares one.
    const auto local_package_instances = [&](const std::string_view name) {
        std::vector<DeclarationId> instances;
        auto scope = use_scope;
        for (std::size_t depth { };
             scope.valid() && depth <= design_->semantics.scopes().size();
             ++depth) {
            visit_vhdl_declarations_in_scope(
                *design_, effective_, scope, name,
                [&](const vhdl::Declaration& declaration) {
                    if (declaration.form
                            == vhdl::DeclarationForm::package_instance
                        && same_vhdl_identifier(declaration.name, name)) {
                        instances.push_back(declaration.id);
                    }
                });
            if (!instances.empty()) {
                sort_unique(instances);
                break;
            }
            const auto* record = find_scope(*design_, scope);
            // Declarations of an entity, package instances included, are
            // visible in its architectures (IEEE 1076-2008 12.1).
            if (record != nullptr && record->unit.valid()
                && record->unit.value() < design_->semantics.units().size()) {
                const auto& units = design_->semantics.units();
                const auto& owner_unit = units[record->unit.value()];
                if (owner_unit.kind == UnitKind::vhdl_architecture
                    && owner_unit.scope == scope) {
                    for (const auto& entity : units) {
                        if (entity.kind != UnitKind::vhdl_entity
                            || !same_vhdl_identifier(
                                entity.library, owner_unit.library)
                            || (!same_vhdl_identifier(
                                    entity.name, owner_unit.name)
                                && !same_vhdl_identifier(entity.name,
                                    owner_unit.secondary_name))) {
                            continue;
                        }
                        visit_vhdl_declarations_in_scope(
                            *design_, effective_, entity.scope, name,
                            [&](const vhdl::Declaration& declaration) {
                                if (declaration.form
                                        == vhdl::DeclarationForm::
                                            package_instance
                                    && same_vhdl_identifier(
                                        declaration.name, name)) {
                                    instances.push_back(declaration.id);
                                }
                            });
                    }
                    if (!instances.empty()) {
                        sort_unique(instances);
                        break;
                    }
                }
            }
            if (record == nullptr || !record->parent) {
                break;
            }
            scope = *record->parent;
        }
        // A package instance declared in a package is visible where a use
        // clause makes that package's declarations visible (IEEE 1076-2008
        // 4.9, 12.4).
        if (instances.empty()) {
            for (const auto* unit : lookup_units) {
                for (const auto& item : unit->context) {
                    if (item.kind != vhdl::ContextKind::use_clause) {
                        continue;
                    }
                    for (const auto& selected : item.selected_names) {
                        const auto selected_parts = vhdl_name_parts(
                            vhdl_spelling(selected));
                        if ((selected_parts.size() != 2U
                                && selected_parts.size() != 3U)
                            || (!same_vhdl_identifier(
                                    selected_parts.back(), "all")
                                && !same_vhdl_identifier(
                                    selected_parts.back(), name))) {
                            continue;
                        }
                        const auto library = selected_parts.size() == 3U
                            ? effective_vhdl_library(
                                  selected_parts.front(), unit->library)
                            : normalized_library(unit->library);
                        const auto package = design_->find_unit(
                            UnitKind::vhdl_package, library,
                            selected_parts[selected_parts.size() - 2U]);
                        if (!package || package->identity == nullptr) {
                            continue;
                        }
                        visit_vhdl_declarations_in_scope(
                            *design_, effective_, package->identity->scope,
                            name, [&](const vhdl::Declaration& declaration) {
                                if (declaration.form
                                        == vhdl::DeclarationForm::
                                            package_instance
                                    && same_vhdl_identifier(
                                        declaration.name, name)) {
                                    instances.push_back(declaration.id);
                                }
                            });
                    }
                }
            }
            sort_unique(instances);
        }
        return instances;
    };

    if (parts.size() == 2U) {
        for (const auto instance : local_package_instances(parts.front())) {
            append_instance(instance, parts.back());
        }

        const auto consider_binding = [&](const CompiledActualBinding& binding) {
            auto bound_declaration = binding.actual_declaration;
            // The binding's own actual names its instance; the design-wide
            // actual of the formal is shared by every instance of the
            // generic package (IEEE 1076-2008 6.5.7.2).
            if (!bound_declaration && binding.expression) {
                const CompiledDeclarationPredicate package_declaration
                    = [](const CompiledDeclarationView& candidate) {
                          if (candidate.vhdl == nullptr) {
                              return false;
                          }
                          const auto form = candidate.vhdl->form;
                          return form
                                  == vhdl::DeclarationForm::package_instance
                              || form
                                  == vhdl::DeclarationForm::generic_package;
                      };
                bound_declaration = resolve_expression_name(
                    *binding.expression, package_declaration, true)
                                        .unique();
            }
            if (!bound_declaration) {
                bound_declaration = actual_declaration(binding.formal);
            }
            if (!bound_declaration) {
                // A library-level instance bound to the interface package
                // contributes its own members (IEEE 1076-2008 6.5.7.2).
                const auto formal = find_declaration(
                    *design_, effective_, binding.formal);
                const auto* package = binding.expression && formal
                        && formal->vhdl != nullptr
                        && formal->vhdl->form
                            == vhdl::DeclarationForm::generic_package
                        && same_vhdl_identifier(
                            formal->vhdl->name, parts.front())
                    ? vhdl_library_package_actual(*binding.expression)
                    : nullptr;
                if (package != nullptr) {
                    append_template(normalized_library(package->library),
                        package->name, parts.back(), std::nullopt, nullptr);
                }
                return;
            }
            const auto formal = find_declaration(
                *design_, effective_, binding.formal);
            const auto actual = actual_declaration(
                *bound_declaration).value_or(*bound_declaration);
            const auto actual_view = find_declaration(
                *design_, effective_, actual);
            if (!formal || formal->vhdl == nullptr
                || formal->vhdl->form
                    != vhdl::DeclarationForm::generic_package
                || !actual_view || actual_view->vhdl == nullptr
                || actual_view->vhdl->form
                    != vhdl::DeclarationForm::package_instance
                || (!same_vhdl_identifier(
                        formal->vhdl->name, parts.front())
                    && !same_vhdl_identifier(
                        actual_view->vhdl->name, parts.front()))) {
                return;
            }
            append_instance(actual, parts.back());
        };
        for (const auto& frame : binding_frames_) {
            for (const auto& binding : frame) {
                consider_binding(binding);
            }
        }
        if (effective_ != nullptr) {
            for (const auto& actual :
                effective_->specialization().actual_identities) {
                CompiledActualBinding binding;
                binding.formal = actual.declaration;
                binding.expression = actual.actual_expression;
                binding.actual_declaration = actual.actual_declaration;
                binding.systemverilog_type = actual.systemverilog_type;
                binding.vhdl_type = actual.vhdl_type;
                consider_binding(binding);
            }
        }
        if (!candidates.empty()) {
            std::ranges::sort(candidates, {},
                [](const CompiledVhdlPackageMember& candidate) {
                    return std::tuple { candidate.member,
                        candidate.package_instance };
                });
            const auto duplicate = std::ranges::unique(candidates, { },
                [](const CompiledVhdlPackageMember& candidate) {
                    return std::tuple { candidate.member,
                        candidate.package_instance };
                });
            candidates.erase(duplicate.begin(), duplicate.end());
            return { status_for(candidates.size()),
                std::move(candidates) };
        }
    }

    // `inst.p.m`: member `m` of the package instance bound to the package
    // generic `p` of `inst` (IEEE 1076-2008 6.5.7.2, 8.3).
    if (parts.size() == 3U) {
        vhdl::Name head;
        head.spelling
            = std::string { parts[0] } + "." + std::string { parts[1] };
        head.canonical = head.spelling;
        const auto generic_package_formals = resolve_vhdl_package_members(
            head, use_scope, [](const CompiledDeclarationView& view) {
                return view.vhdl != nullptr
                    && view.vhdl->form
                        == vhdl::DeclarationForm::generic_package;
            });
        const CompiledDeclarationPredicate instance_declaration
            = [](const CompiledDeclarationView& view) {
                  return view.vhdl != nullptr
                      && (view.vhdl->form
                              == vhdl::DeclarationForm::package_instance
                          || view.vhdl->form
                              == vhdl::DeclarationForm::generic_package);
              };
        for (const auto& formal_candidate :
            generic_package_formals.candidates) {
            for (const auto& binding_item :
                formal_candidate.generic_bindings) {
                if (binding_item.formal != formal_candidate.member) {
                    continue;
                }
                auto bound = binding_item.actual_declaration;
                if (!bound && binding_item.expression) {
                    bound = resolve_expression_name(*binding_item.expression,
                        instance_declaration, true)
                                .unique();
                }
                if (!bound) {
                    continue;
                }
                const auto inner
                    = actual_declaration(*bound).value_or(*bound);
                const auto inner_view
                    = find_declaration(*design_, effective_, inner);
                if (inner_view && inner_view->vhdl != nullptr
                    && inner_view->vhdl->form
                        == vhdl::DeclarationForm::package_instance) {
                    append_instance(inner, parts.back());
                }
            }
        }
        if (!candidates.empty()) {
            std::ranges::sort(candidates, {},
                [](const CompiledVhdlPackageMember& candidate) {
                    return std::tuple { candidate.member,
                        candidate.package_instance };
                });
            const auto duplicate = std::ranges::unique(candidates, { },
                [](const CompiledVhdlPackageMember& candidate) {
                    return std::tuple { candidate.member,
                        candidate.package_instance };
                });
            candidates.erase(duplicate.begin(), duplicate.end());
            return { status_for(candidates.size()),
                std::move(candidates) };
        }
    }

    if (parts.size() == 2U || parts.size() == 3U) {
        if (parts.size() == 2U) {
            append_template(normalized_library(owner->library),
                parts[0], parts[1], std::nullopt, nullptr);
            if (candidates.empty()) {
                visit_vhdl_linked_imports(*design_, lookup_units,
                    [&](const CompiledVhdlImport& imported) {
                        if (!same_vhdl_identifier(
                                imported.package, parts.front())
                            || !same_vhdl_identifier(
                                imported.member, parts.front())) {
                            return false;
                        }
                        append_template(imported.library,
                            imported.package, parts.back(),
                            std::nullopt, nullptr);
                        return false;
                    });
            }
        } else {
            const auto library = effective_vhdl_library(
                parts.front(), owner->library);
            append_template(library, parts[1], parts[2],
                std::nullopt, nullptr);
        }
        return { status_for(candidates.size()), std::move(candidates) };
    }

    // `use p.all` where `p` is a package generic: the members of the
    // package instance bound to it (IEEE 1076-2008 6.5.5, 12.4). The actual
    // is a package instance declaration or a library-level instance.
    struct BoundPackage {
        std::optional<DeclarationId> instance;
        const vhdl::Unit* library_package { };
    };
    const auto bound_package_instance = [&](const std::string_view formal_name)
        -> BoundPackage {
        BoundPackage found;
        const auto consider = [&](const CompiledActualBinding& binding) {
            const auto formal = find_declaration(
                *design_, effective_, binding.formal);
            if (!formal || formal->vhdl == nullptr
                || formal->vhdl->form != vhdl::DeclarationForm::generic_package
                || !same_vhdl_identifier(formal->vhdl->name, formal_name)) {
                return;
            }
            auto bound = binding.actual_declaration;
            if (!bound && binding.expression) {
                const CompiledDeclarationPredicate package_declaration
                    = [](const CompiledDeclarationView& candidate) {
                          return candidate.vhdl != nullptr
                              && candidate.vhdl->form
                                  == vhdl::DeclarationForm::package_instance;
                      };
                bound = resolve_expression_name(
                    *binding.expression, package_declaration, true)
                            .unique();
            }
            if (!bound) {
                bound = actual_declaration(binding.formal);
            }
            if (bound) {
                found.instance = actual_declaration(*bound).value_or(*bound);
            } else if (binding.expression) {
                found.library_package
                    = vhdl_library_package_actual(*binding.expression);
            }
        };
        for (const auto& frame : binding_frames_) {
            for (const auto& binding : frame) {
                consider(binding);
            }
        }
        if (!found.instance && found.library_package == nullptr
            && effective_ != nullptr) {
            for (const auto& actual :
                effective_->specialization().actual_identities) {
                CompiledActualBinding binding;
                binding.formal = actual.declaration;
                binding.expression = actual.actual_expression;
                binding.actual_declaration = actual.actual_declaration;
                consider(binding);
            }
        }
        return found;
    };

    struct ContextAncestor {
        UnitId unit;
        const ContextAncestor* parent;
    };
    const auto visit_context = [&](const auto& self,
                                   const vhdl::Unit& context_owner,
                                   const ContextAncestor* parent) -> void {
        for (auto* ancestor = parent; ancestor != nullptr;
             ancestor = ancestor->parent) {
            if (ancestor->unit == context_owner.id) {
                return;
            }
        }
        const ContextAncestor active { context_owner.id, parent };
        for (const auto& item : context_owner.context) {
            for (const auto& selected : item.selected_names) {
                const auto selected_parts = vhdl_name_parts(
                    vhdl_spelling(selected));
                if (item.kind == vhdl::ContextKind::use_clause) {
                    if (selected_parts.size() != 2U
                        && selected_parts.size() != 3U) {
                        continue;
                    }
                    const auto package_index
                        = selected_parts.size() - 2U;
                    // `use pkg."+"` names an operator symbol, spelled
                    // with or without its quotes (IEEE 1076-2008 12.4).
                    const auto unquoted = [](std::string_view text) {
                        if (text.size() >= 2U && text.front() == '"'
                            && text.back() == '"') {
                            text = text.substr(1U, text.size() - 2U);
                        }
                        return text;
                    };
                    // A use clause naming a type mark also makes visible
                    // the package's explicit homographs of the predefined
                    // operators of its base type, and the base type's
                    // enumeration literals and physical units (IEEE
                    // 1076-2008 12.4 e).
                    const auto designator = unquoted(parts.back());
                    static constexpr std::array<std::string_view, 16U>
                        word_operators { "and", "or", "nand", "nor",
                            "xor", "xnor", "not", "abs", "mod", "rem",
                            "sll", "srl", "sla", "sra", "rol", "ror" };
                    const bool operator_symbol = !designator.empty()
                        && (parts.back().front() == '"'
                            || (std::isalpha(static_cast<unsigned char>(
                                    designator.front())) == 0
                                && designator.front() != '\\'
                                && designator.front() != '\'')
                            || std::ranges::any_of(word_operators,
                                [&](const std::string_view word) {
                                    return same_vhdl_identifier(
                                        designator, word);
                                }));
                    const auto type_operators = [&]() -> std::optional<TypeId> {
                        if (designator.empty()
                            || context_owner.standard
                                < std::string_view { "2008" }) {
                            return std::nullopt;
                        }
                        const auto types
                            = design_->vhdl_type_declarations_named(
                                selected_parts.back());
                        if (!types) {
                            return std::nullopt;
                        }
                        for (const auto id : *types) {
                            const auto type = find_declaration(
                                *design_, effective_, id);
                            const auto library = selected_parts.size()
                                    == 3U
                                ? selected_parts.front()
                                : std::string_view { "work" };
                            // A subtype names its type mark's base type.
                            const auto declared = type && type->vhdl != nullptr
                                ? type->vhdl->declared_type
                                    ? *type->vhdl->declared_type
                                    : type->vhdl->subtype
                                    ? type->vhdl->subtype->type_mark.target
                                    : TypeId { }
                                : TypeId { };
                            if (declared.valid()
                                && design_->vhdl_scope_in_package(
                                    type->vhdl->scope, library,
                                    selected_parts[package_index])) {
                                return effective_ != nullptr
                                    ? vhdl_root_type(*effective_, declared)
                                    : vhdl_root_type(*design_, declared);
                            }
                        }
                        return std::nullopt;
                    };
                    std::optional<TypeId> operator_type;
                    if (!same_vhdl_identifier(
                            selected_parts.back(), "all")
                        && !same_vhdl_identifier(
                            unquoted(selected_parts.back()),
                            unquoted(parts.back()))) {
                        operator_type = type_operators();
                        if (vhdl_resolve_trace_enabled) {
                            std::fprintf(stderr,
                                "FSIM_VHDL_RESOLVE type-operators name=%s "
                                "use=%s standard=%s found=%d\n",
                                std::string { parts.back() }.c_str(),
                                std::string { vhdl_spelling(selected) }
                                    .c_str(),
                                context_owner.standard.c_str(),
                                static_cast<int>(operator_type.has_value()));
                        }
                        if (!operator_type) {
                            continue;
                        }
                    }
                    const auto first_appended = candidates.size();
                    // Only the operators whose profile names the base type.
                    const auto keep_type_operators = [&] {
                        if (!operator_type) {
                            return;
                        }
                        const auto names_type = [&](const std::optional<
                                                    vhdl::SubtypeIndication>&
                                                    subtype) {
                            return subtype
                                && (effective_ != nullptr
                                           ? vhdl_root_type(*effective_,
                                                 subtype->type_mark.target)
                                           : vhdl_root_type(*design_,
                                                 subtype->type_mark.target))
                                == *operator_type;
                        };
                        const auto root = find_type(
                            *design_, effective_, *operator_type);
                        const auto unrelated = [&](const CompiledVhdlPackageMember&
                                                       candidate) {
                            const auto member = find_declaration(
                                *design_, effective_, candidate.member);
                            if (!member || member->vhdl == nullptr) {
                                return true;
                            }
                            if (!member->vhdl->callable) {
                                return !root || root->vhdl == nullptr
                                    || (std::ranges::none_of(
                                            root->vhdl->enumeration_literals,
                                            [&](const auto& literal) {
                                                return literal.declaration
                                                    == candidate.member;
                                            })
                                        && std::ranges::none_of(
                                            root->vhdl->physical_units,
                                            [&](const auto& unit) {
                                                return unit.declaration
                                                    == candidate.member;
                                            }));
                            }
                            if (!operator_symbol) {
                                return true;
                            }
                            const auto& callable = *member->vhdl->callable;
                            if (names_type(callable.return_type)) {
                                return false;
                            }
                            return std::ranges::none_of(callable.formals,
                                [&](const DeclarationId formal_id) {
                                    const auto formal = find_declaration(
                                        *design_, effective_, formal_id);
                                    return formal && formal->vhdl != nullptr
                                        && names_type(formal->vhdl->subtype);
                                });
                        };
                        const auto removed = std::remove_if(
                            candidates.begin()
                                + static_cast<std::ptrdiff_t>(first_appended),
                            candidates.end(), unrelated);
                        candidates.erase(removed, candidates.end());
                    };
                    if (selected_parts.size() == 2U) {
                        const auto bound = bound_package_instance(
                            selected_parts.front());
                        if (bound.instance) {
                            append_instance(*bound.instance, parts.back());
                            keep_type_operators();
                            continue;
                        }
                        if (bound.library_package != nullptr) {
                            append_template(normalized_library(
                                                bound.library_package->library),
                                bound.library_package->name, parts.back(),
                                std::nullopt, nullptr);
                            keep_type_operators();
                            continue;
                        }
                        // `use inst.all` of a local package instance
                        // (IEEE 1076-2008 4.9, 12.4).
                        const auto instances = local_package_instances(
                            selected_parts.front());
                        if (!instances.empty()) {
                            for (const auto instance : instances) {
                                append_instance(instance, parts.back());
                            }
                            keep_type_operators();
                            continue;
                        }
                    }
                    const auto library = selected_parts.size() == 3U
                        ? effective_vhdl_library(selected_parts.front(),
                              context_owner.library)
                        : normalized_library(context_owner.library);
                    append_template(library,
                        selected_parts[package_index], parts.back(),
                        std::nullopt, nullptr);
                    keep_type_operators();
                    continue;
                }
                if (item.kind != vhdl::ContextKind::context_reference) {
                    continue;
                }
                visit_referenced_vhdl_contexts(*design_, selected_parts,
                    context_owner.library, [&](const vhdl::Unit& context) {
                        self(self, context, &active);
                        return false;
                    });
            }
        }
    };
    for (const auto* context_owner : lookup_units) {
        visit_context(visit_context, *context_owner, nullptr);
    }
    // Link metadata is the canonical, relocation-stable record of package
    // edges. Use it as a second view of use clauses so a projected unit whose
    // selected-name spelling was normalized independently still imports the
    // same package members. Preserve the selector from the actual context
    // item so a package edge originating in a selected expression cannot
    // broaden a selective use clause into a wildcard import.
    visit_vhdl_linked_imports(*design_, lookup_units,
        [&](const CompiledVhdlImport& imported) {
        if (!same_vhdl_identifier(imported.member, "all")
            && !same_vhdl_identifier(
                imported.member, parts.back())) {
            return false;
        }
        append_template(imported.library, imported.package,
            parts.back(), std::nullopt, nullptr);
        return false;
    });
    std::ranges::sort(candidates, {},
        [](const CompiledVhdlPackageMember& candidate) {
            return std::tuple { candidate.member,
                candidate.package_instance };
        });
    const auto duplicate = std::ranges::unique(candidates, { },
        [](const CompiledVhdlPackageMember& candidate) {
            return std::tuple { candidate.member,
                candidate.package_instance };
        });
    candidates.erase(duplicate.begin(), duplicate.end());
    return { status_for(candidates.size()), std::move(candidates) };
}

bool CompiledDesignResolver::vhdl_package_member_visible(
    const vhdl::Unit& lookup_unit, const vhdl::Unit& package,
    const std::string_view member) const
{
    if (design_ == nullptr
        || package.kind != vhdl::UnitKind::package
        || !package.primary_name.empty()) {
        return false;
    }
    // IEEE 1076-2019 13.2 gives every design unit an implicit context
    // clause naming STD.STANDARD. Keep this package visibility independent
    // of explicit use clauses; lexical declarations still rank ahead of it.
    if (same_vhdl_identifier(
            normalized_library(package.library), "std")
        && same_vhdl_identifier(package.name, "standard")) {
        return true;
    }
    if (lookup_unit.kind == vhdl::UnitKind::package
        && !lookup_unit.primary_name.empty()
        && same_vhdl_identifier(
            normalized_library(lookup_unit.library),
            normalized_library(package.library))
        && same_vhdl_identifier(lookup_unit.name, package.name)) {
        return true;
    }
    std::unordered_set<std::uint32_t> active_contexts;
    const auto visit = [&](const auto& self,
                           const vhdl::Unit& owner) -> bool {
        if (!active_contexts.insert(owner.id.value()).second) {
            return false;
        }
        for (const auto& item : owner.context) {
            for (const auto& selected : item.selected_names) {
                const auto parts = vhdl_name_parts(vhdl_spelling(selected));
                if (item.kind == vhdl::ContextKind::use_clause) {
                    if (parts.size() != 2U && parts.size() != 3U) {
                        continue;
                    }
                    const auto package_index = parts.size() - 2U;
                    const auto library = parts.size() == 3U
                        ? effective_vhdl_library(
                              parts.front(), owner.library)
                        : normalized_library(owner.library);
                    if (same_vhdl_identifier(
                            library, normalized_library(package.library))
                        && same_vhdl_identifier(
                            parts[package_index], package.name)
                        && (same_vhdl_identifier(parts.back(), "all")
                            || same_vhdl_identifier(
                                parts.back(), member))) {
                        active_contexts.erase(owner.id.value());
                        return true;
                    }
                    continue;
                }
                if (item.kind == vhdl::ContextKind::context_reference
                    && visit_referenced_vhdl_contexts(*design_, parts,
                        owner.library, [&](const vhdl::Unit& context) {
                            return self(self, context);
                        })) {
                    active_contexts.erase(owner.id.value());
                    return true;
                }
            }
        }
        active_contexts.erase(owner.id.value());
        return false;
    };
    if (visit(visit, lookup_unit)) {
        return true;
    }
    const auto entity = primary_vhdl_unit(*design_, lookup_unit);
    return entity && visit(visit, **entity);
}

bool CompiledDesignResolver::vhdl_library_name_visible(
    const std::string_view name, const ScopeId use_scope) const
{
    if (design_ == nullptr || name.empty()) {
        return false;
    }
    const auto lookup_units = vhdl_lookup_units(
        *design_, selected_unit_, use_scope);
    if (lookup_units.empty()) {
        return false;
    }
    const auto implicit = [&](const vhdl::Unit& unit) {
        return same_vhdl_identifier(name, "std")
            || same_vhdl_identifier(name, "work")
            || std::ranges::any_of(
                unit.predefined_environment.implicit_libraries,
                [&](const std::string_view candidate) {
                    return same_vhdl_identifier(candidate, name);
                });
    };
    if (std::ranges::any_of(lookup_units,
            [&](const vhdl::Unit* unit) {
                return unit != nullptr && implicit(*unit);
            })) {
        return true;
    }

    std::unordered_set<std::uint32_t> active_contexts;
    const auto visible_from = [&](const auto& self,
                                  const vhdl::Unit& context_owner) -> bool {
        if (!active_contexts.insert(context_owner.id.value()).second) {
            return false;
        }
        for (const auto& item : context_owner.context) {
            for (const auto& selected : item.selected_names) {
                const auto parts = vhdl_name_parts(vhdl_spelling(selected));
                if (item.kind == vhdl::ContextKind::library_clause) {
                    if (parts.size() == 1U
                        && same_vhdl_identifier(parts.front(), name)) {
                        active_contexts.erase(context_owner.id.value());
                        return true;
                    }
                    continue;
                }
                if (item.kind == vhdl::ContextKind::context_reference
                    && visit_referenced_vhdl_contexts(*design_, parts,
                        context_owner.library, [&](const vhdl::Unit& context) {
                            return self(self, context);
                        })) {
                    active_contexts.erase(context_owner.id.value());
                    return true;
                }
            }
        }
        active_contexts.erase(context_owner.id.value());
        return false;
    };
    return std::ranges::any_of(lookup_units,
        [&](const vhdl::Unit* unit) {
            return unit != nullptr && visible_from(visible_from, *unit);
        });
}

bool CompiledDesignResolver::vhdl_standard_package_member_visible(
    const vhdl::Name& name, const ScopeId use_scope) const
{
    if (design_ == nullptr) {
        return false;
    }
    const auto parts = vhdl_name_parts(vhdl_spelling(name));
    if (parts.empty() || parts.size() > 3U) {
        return false;
    }
    if (parts.size() == 3U
        && builtin_standard_package_exports(
            parts[0], parts[1], parts[2])) {
        return true;
    }
    const auto lookup_units = vhdl_lookup_units(
        *design_, selected_unit_, use_scope);
    if (lookup_units.empty()) {
        return false;
    }
    const auto* owner = lookup_units.front();
    const auto exported = [&](const std::string_view library,
                              const std::string_view package_name,
                              const std::string_view member) {
        for (const auto& package : design_->vhdl_units()) {
            if (package.kind == vhdl::UnitKind::package
                && package.primary_name.empty()
                && same_vhdl_identifier(package.library, library)
                && same_vhdl_identifier(package.name, package_name)
                && (standard_package_exports(package, member)
                    || std::ranges::any_of(package.declarations,
                        [&](const DeclarationId id) {
                            const auto declaration = find_declaration(
                                *design_, effective_, id);
                            return declaration
                                && declaration->vhdl != nullptr
                                && same_vhdl_identifier(
                                    declaration->vhdl->name, member);
                        }))) {
                return true;
            }
        }
        return builtin_standard_package_exports(
            library, package_name, member);
    };
    if (parts.size() == 3U) {
        return exported(
            effective_vhdl_library(parts.front(), owner->library),
            parts[1], parts[2]);
    }
    if (parts.size() == 2U) {
        if (exported(normalized_library(owner->library),
                parts[0], parts[1])) {
            return true;
        }
        return visit_vhdl_linked_imports(*design_, lookup_units,
            [&](const CompiledVhdlImport& imported) {
                return same_vhdl_identifier(
                           imported.package, parts.front())
                    && same_vhdl_identifier(
                        imported.member, parts.front())
                    && exported(imported.library, imported.package,
                        parts.back());
            });
    }
    const auto member = parts.front();
    const auto visible_from = [&](const auto& self,
                                  const vhdl::Unit& context_owner) -> bool {
        for (const auto& item : context_owner.context) {
            for (const auto& selected : item.selected_names) {
                const auto selected_parts = vhdl_name_parts(
                    vhdl_spelling(selected));
                if (item.kind == vhdl::ContextKind::use_clause
                    && (selected_parts.size() == 2U
                        || selected_parts.size() == 3U)) {
                    const auto package_index
                        = selected_parts.size() - 2U;
                    if (!same_vhdl_identifier(
                            selected_parts.back(), "all")
                        && !same_vhdl_identifier(
                            selected_parts.back(), member)) {
                        continue;
                    }
                    const auto library = selected_parts.size() == 3U
                        ? effective_vhdl_library(
                              selected_parts.front(), context_owner.library)
                        : normalized_library(context_owner.library);
                    if (exported(library,
                            selected_parts[package_index], member)) {
                        return true;
                    }
                } else if (item.kind
                        == vhdl::ContextKind::context_reference
                    && visit_referenced_vhdl_contexts(*design_,
                        selected_parts, context_owner.library,
                        [&](const vhdl::Unit& context) {
                            return self(self, context);
                        })) {
                    return true;
                }
            }
        }
        return false;
    };
    for (const auto* context_owner : lookup_units) {
        if (visible_from(visible_from, *context_owner)) {
            return true;
        }
    }
    if (visit_vhdl_linked_imports(*design_, lookup_units,
            [&](const CompiledVhdlImport& imported) {
        if ((same_vhdl_identifier(imported.member, "all")
                || same_vhdl_identifier(imported.member, member))
            && exported(
                imported.library, imported.package, member)) {
            return true;
        }
        return false;
    })) {
        return true;
    }
    return false;
}

bool CompiledDesignResolver::vhdl_builtin_type_visible(
    std::string_view spelling, const ScopeId use_scope) const
{
    spelling = vhdl::without_builtin_prefix(spelling);
    vhdl::Name name;
    name.spelling = spelling;
    name.canonical = spelling;
    return vhdl_standard_package_member_visible(name, use_scope);
}

bool CompiledDesignResolver::vhdl_builtin_package_member_imported(
    const std::string_view library, const std::string_view package,
    const std::string_view member, const ScopeId use_scope) const
{
    if (design_ == nullptr
        || !builtin_standard_package_exports(library, package, member)) {
        return false;
    }
    const auto lookup_units = vhdl_lookup_units(
        *design_, selected_unit_, use_scope);
    const auto matching_import = [&](const CompiledVhdlImport& imported) {
        return same_vhdl_identifier(imported.library, library)
            && same_vhdl_identifier(imported.package, package)
            && (same_vhdl_identifier(imported.member, "all")
                || same_vhdl_identifier(imported.member, member));
    };
    std::unordered_set<std::uint32_t> visited_contexts;
    const auto imports_from = [&](const auto& self,
                                  const vhdl::Unit& owner) -> bool {
        if (!visited_contexts.insert(owner.id.value()).second) {
            return false;
        }
        const std::vector<const vhdl::Unit*> direct_owner { &owner };
        if (visit_vhdl_linked_imports(
                *design_, direct_owner, matching_import)) {
            return true;
        }
        for (const auto& item : owner.context) {
            if (item.kind != vhdl::ContextKind::context_reference) {
                continue;
            }
            for (const auto& selected : item.selected_names) {
                const auto parts = vhdl_name_parts(vhdl_spelling(selected));
                if (parts.empty()) {
                    continue;
                }
                const auto context_library
                    = context_reference_library(parts, owner.library);
                const vhdl::Unit* referenced_context { };
                bool duplicate_context { };
                for (const auto& candidate : design_->vhdl_units()) {
                    if (!is_named_vhdl_context(
                            candidate, context_library, parts.back())) {
                        continue;
                    }
                    if (referenced_context != nullptr
                        && referenced_context->id != candidate.id) {
                        duplicate_context = true;
                        break;
                    }
                    referenced_context = &candidate;
                }
                if (!duplicate_context && referenced_context != nullptr
                    && self(self, *referenced_context)) {
                    return true;
                }
            }
        }
        return false;
    };
    return std::ranges::any_of(lookup_units,
        [&](const vhdl::Unit* owner) {
            return owner != nullptr && imports_from(imports_from, *owner);
        });
}

CompiledDeclarationResolution CompiledDesignResolver::resolve_vhdl(
    const vhdl::Name& name, const ScopeId use_scope,
    const CompiledDeclarationPredicate& predicate) const
{
    const auto spelling = vhdl_spelling(name);
    const auto parts = vhdl_name_parts(spelling);
    if (parts.empty()) {
        return { CompiledResolutionStatus::invalid, { } };
    }

    const auto in_scope = [&](const ScopeId scope) {
        std::vector<DeclarationId> candidates;
        visit_vhdl_declarations_in_scope(
            *design_, effective_, scope, parts.back(),
            [&](const vhdl::Declaration& declaration) {
                if (same_vhdl_identifier(
                        declaration.name, parts.back())
                    && (!predicate || predicate(
                        CompiledDeclarationView { nullptr,
                            &declaration }))) {
                    candidates.push_back(declaration.id);
                }
            });
        return declaration_result(std::move(candidates));
    };
    if (parts.size() > 1U) {
        const auto packages = resolve_vhdl_package_members(
            name, use_scope, predicate);
        std::vector<DeclarationId> declarations;
        declarations.reserve(packages.candidates.size());
        for (const auto& candidate : packages.candidates) {
            declarations.push_back(candidate.member);
        }
        // An expanded name whose prefix denotes an enclosing construct, a
        // labeled statement, a subprogram, or the design unit, selects a
        // declaration of that construct (IEEE 1076-2008 8.3).
        if (declarations.empty() && parts.size() == 2U) {
            auto enclosing = use_scope;
            for (std::size_t depth { }; enclosing.valid()
                 && depth <= design_->semantics.scopes().size();
                 ++depth) {
                const auto* record = find_scope(*design_, enclosing);
                if (record == nullptr) {
                    break;
                }
                std::vector<ScopeId> selected;
                const auto owner = design_->find_unit(record->unit);
                const bool unit_scope = owner && owner->identity != nullptr
                    && owner->identity->scope == enclosing;
                if (unit_scope) {
                    // Named by the design unit, below.
                } else if (same_vhdl_identifier(record->name, parts.front())) {
                    selected.push_back(enclosing);
                } else if (record->name == "<loop>") {
                    // A loop scope is named by its statement's label.
                    const auto& statements = design_->vhdl_hir.statements();
                    if (std::ranges::any_of(statements,
                            [&](const vhdl::Statement& statement) {
                                return statement.nested_scope == enclosing
                                    && (same_vhdl_identifier(
                                            statement.label, parts.front())
                                        || same_vhdl_identifier(
                                            statement.loop_label,
                                            parts.front()));
                            })) {
                        selected.push_back(enclosing);
                    }
                }
                if (unit_scope) {
                    const auto& unit = owner;
                    if (unit && unit->vhdl != nullptr) {
                        const auto entity = primary_vhdl_unit(
                            *design_, *unit->vhdl);
                        if (same_vhdl_identifier(
                                unit->vhdl->name, parts.front())) {
                            selected.push_back(enclosing);
                            if (entity) {
                                selected.push_back((**entity).scope);
                            }
                        } else if (entity
                            && same_vhdl_identifier(
                                (**entity).name, parts.front())) {
                            selected.push_back((**entity).scope);
                        }
                    }
                }
                for (const auto candidate : selected) {
                    auto tier = in_scope(candidate);
                    if (tier.status != CompiledResolutionStatus::not_found) {
                        return tier;
                    }
                }
                if (!record->parent) {
                    break;
                }
                enclosing = *record->parent;
            }
        }
        return declaration_result(std::move(declarations));
    }
    // Enumeration literals are overloadable and literals of different types
    // are not homographs: an inner literal leaves outer literals of the same
    // designator visible, up to a nonoverloadable declaration (IEEE
    // 1076-2008 12.3). The walk includes the primary unit's region (12.1).
    const auto is_literal = [&](const DeclarationId id) {
        const auto declaration = find_declaration(*design_, effective_, id);
        return declaration && declaration->vhdl != nullptr
            && declaration->vhdl->form
                == vhdl::DeclarationForm::enumeration_literal;
    };
    const auto outer_literals = [&](const ScopeId found,
                                    std::vector<DeclarationId> literals) {
        auto next = found;
        const Scope* last { };
        bool primary_visited { };
        for (std::size_t depth { };
             depth <= design_->semantics.scopes().size(); ++depth) {
            const auto* record = find_scope(*design_, next);
            if (record != nullptr) {
                last = record;
            }
            if (record != nullptr && record->parent) {
                next = *record->parent;
            } else if (!primary_visited && last != nullptr) {
                primary_visited = true;
                const auto unit = design_->find_unit(last->unit);
                const auto entity = unit && unit->vhdl != nullptr
                    ? primary_vhdl_unit(*design_, *unit->vhdl)
                    : std::nullopt;
                if (!entity || (**entity).scope == next) {
                    break;
                }
                next = (**entity).scope;
            } else {
                break;
            }
            bool stop { };
            std::vector<DeclarationId> tier;
            visit_vhdl_declarations_in_scope(
                *design_, effective_, next, parts.back(),
                [&](const vhdl::Declaration& declaration) {
                    if (!same_vhdl_identifier(
                            declaration.name, parts.back())) {
                        return;
                    }
                    if (declaration.form
                        != vhdl::DeclarationForm::enumeration_literal) {
                        stop = true;
                        return;
                    }
                    if (!predicate || predicate(CompiledDeclarationView {
                            nullptr, &declaration })) {
                        tier.push_back(declaration.id);
                    }
                });
            if (stop) {
                break;
            }
            literals.insert(literals.end(), tier.begin(), tier.end());
        }
        return declaration_result(std::move(literals));
    };
    // A declaration is visible from its end onwards (IEEE 1076-2008 12.3):
    // in the use's own region, one written after the use does not yet hide
    // an outer declaration.
    const auto declared_after_use = [&](const DeclarationId id) {
        const auto declaration = find_declaration(*design_, effective_, id);
        const auto& spans = design_->semantics.source_spans();
        if (!declaration || declaration->vhdl == nullptr
            || !declaration->vhdl->source.valid() || !name.source.valid()
            || declaration->vhdl->source.value() >= spans.size()
            || name.source.value() >= spans.size()) {
            return false;
        }
        const auto& declared = spans[declaration->vhdl->source.value()];
        const auto& used = spans[name.source.value()];
        return declared.file == used.file && !declared.expansion
            && !used.expansion && declared.begin.offset > used.begin.offset;
    };
    auto scope = use_scope;
    const Scope* root { };
    for (std::size_t depth { };
         scope.valid() && depth <= design_->semantics.scopes().size();
         ++depth) {
        auto tier = in_scope(scope);
        if (scope == use_scope && !tier.candidates.empty()
            && std::ranges::all_of(tier.candidates, declared_after_use)) {
            tier = { CompiledResolutionStatus::not_found, { } };
        }
        if (tier.status == CompiledResolutionStatus::not_found) {
            if (const auto declared = vhdl_protected_declaration_scope(
                    *design_, effective_, scope)) {
                tier = in_scope(*declared);
            }
        }
        if (tier.status != CompiledResolutionStatus::not_found) {
            if (!tier.candidates.empty()
                && std::ranges::all_of(tier.candidates, is_literal)) {
                return outer_literals(scope, tier.candidates);
            }
            return tier;
        }
        const auto* record = find_scope(*design_, scope);
        if (record == nullptr) {
            break;
        }
        root = record;
        if (!record->parent) {
            break;
        }
        scope = *record->parent;
    }

    // A secondary unit's region extends its primary unit's (IEEE 1076-2008
    // 12.1): a package body sees its own package's generics before the
    // generics of the instances that enclose its specialization (12.3).
    if (root != nullptr) {
        const auto root_unit = design_->find_unit(root->unit);
        if (root_unit && root_unit->vhdl != nullptr) {
            if (const auto entity = primary_vhdl_unit(
                    *design_, *root_unit->vhdl)) {
                auto tier = in_scope((**entity).scope);
                if (tier.status != CompiledResolutionStatus::not_found) {
                    return tier;
                }
            }
        }
    }

    std::vector<DeclarationId> overlay_formals;
    const auto append_formal = [&](const DeclarationId formal) {
        const auto declaration = find_declaration(
            *design_, effective_, formal);
        if (declaration && declaration->vhdl != nullptr
            && same_vhdl_identifier(
                declaration->vhdl->name, parts.back())
            && (!predicate || predicate(*declaration))) {
            overlay_formals.push_back(formal);
        }
    };
    // The innermost frame naming the formal binds it: a generic package
    // instantiated in another's body has generics of the same name as the
    // enclosing instance (IEEE 1076-2008 4.9, 12.3).
    for (auto frame = binding_frames_.rbegin();
         frame != binding_frames_.rend() && overlay_formals.empty();
         ++frame) {
        for (const auto& binding : *frame) {
            append_formal(binding.formal);
        }
    }
    if (overlay_formals.empty() && effective_ != nullptr) {
        for (const auto& actual :
            effective_->specialization().actual_identities) {
            append_formal(actual.declaration);
        }
    }
    auto tier = declaration_result(std::move(overlay_formals));
    if (tier.status != CompiledResolutionStatus::not_found) {
        return tier;
    }

    const auto packages = resolve_vhdl_package_members(
        name, use_scope, predicate);
    std::vector<DeclarationId> declarations;
    declarations.reserve(packages.candidates.size());
    for (const auto& candidate : packages.candidates) {
        declarations.push_back(candidate.member);
    }
    tier = declaration_result(std::move(declarations));
    if (tier.status != CompiledResolutionStatus::not_found) {
        return tier;
    }

    std::vector<DeclarationId> fallback;
    if (name.selected && accepted(
            *design_, effective_, *name.selected, predicate)) {
        fallback.push_back(*name.selected);
    }
    for (const auto candidate : name.overloads) {
        if (accepted(*design_, effective_, candidate, predicate)) {
            fallback.push_back(candidate);
        }
    }
    return declaration_result(std::move(fallback));
}

CompiledDeclarationResolution
CompiledDesignResolver::resolve_vhdl_callable_candidates(
    const vhdl::Name& name, const ScopeId use_scope) const
{
    const CompiledDeclarationPredicate callable = [](const auto& view) {
        return view.vhdl != nullptr
            && vhdl_callable_form(view.vhdl->form)
            && (view.vhdl->callable.has_value()
                || view.vhdl->form
                    == vhdl::DeclarationForm::generic_function_instance
                || view.vhdl->form
                    == vhdl::DeclarationForm::generic_procedure_instance);
    };
    // A subprogram alias (IEEE 1076-2008 6.6.3) is an overload in the
    // region of its declaration and denotes the target overload its
    // signature selects.
    const CompiledDeclarationPredicate callable_or_alias
        = [&](const auto& view) {
              return callable(view)
                  || (view.vhdl != nullptr
                      && view.vhdl->form == vhdl::DeclarationForm::alias
                      && view.vhdl->alias_signature
                      && view.vhdl->alias_target);
          };
    // Visibility is determined at the use site.  Retained frontend
    // identities are a fallback inside resolve_vhdl(), not a tier above
    // lexical declarations: otherwise an imported homograph retained on an
    // occurrence defeats a declaration in the nearest enclosing scope.
    // Resolving the spelling first also preserves complete overload sets when
    // `selected` is an indexing interpretation rather than a callable.
    auto visible = resolve_vhdl(name, use_scope, callable_or_alias);
    // Subprograms are overloadable: an inner declaration hides only its
    // homographs, so the outer regions' subprograms of other profiles stay
    // visible (IEEE 1076-2008 12.3). A nonoverloadable declaration of the
    // designator, or an unknown profile, ends the walk.
    if (const auto parts = vhdl_name_parts(vhdl_spelling(name));
        parts.size() == 1U && !visible.candidates.empty()) {
        const auto first = find_declaration(
            *design_, effective_, visible.candidates.front());
        std::optional<ScopeId> found_scope;
        if (first && first->vhdl != nullptr) {
            for (auto scope = use_scope; scope.valid();) {
                if (scope == first->vhdl->scope) {
                    found_scope = scope;
                    break;
                }
                const auto* record = find_scope(*design_, scope);
                if (record == nullptr || !record->parent) {
                    break;
                }
                scope = *record->parent;
            }
        }
        std::vector<std::string> keys;
        bool known = found_scope.has_value();
        for (const auto candidate : visible.candidates) {
            const auto key = vhdl_callable_homograph_key(
                *design_, effective_, candidate);
            if (!key) {
                known = false;
                break;
            }
            keys.push_back(*key);
        }
        std::vector<DeclarationId> outer;
        std::optional<ScopeId> scope;
        const Scope* root { };
        if (known) {
            const auto* record = find_scope(*design_, *found_scope);
            root = record;
            if (record != nullptr && record->parent) {
                scope = *record->parent;
            }
        }
        bool primary_visited { };
        const auto advance = [&] {
            const auto* record = find_scope(*design_, *scope);
            if (record != nullptr) {
                root = record;
            }
            if (record != nullptr && record->parent) {
                scope = *record->parent;
                return;
            }
            scope.reset();
            if (primary_visited || root == nullptr) {
                return;
            }
            primary_visited = true;
            const auto root_unit = design_->find_unit(root->unit);
            if (root_unit && root_unit->vhdl != nullptr) {
                if (const auto entity = primary_vhdl_unit(
                        *design_, *root_unit->vhdl);
                    entity && (**entity).scope != *found_scope) {
                    scope = (**entity).scope;
                }
            }
        };
        if (known && !scope && root != nullptr) {
            scope = *found_scope;
            advance();
        }
        while (known && scope && scope->valid()) {
            bool stop { };
            std::vector<DeclarationId> tier;
            visit_vhdl_declarations_in_scope(
                *design_, effective_, *scope, parts.back(),
                [&](const vhdl::Declaration& declaration) {
                    if (!same_vhdl_identifier(
                            declaration.name, parts.back())) {
                        return;
                    }
                    if (declaration.form
                        == vhdl::DeclarationForm::enumeration_literal) {
                        return;
                    }
                    if (!callable_or_alias(CompiledDeclarationView {
                            nullptr, &declaration })) {
                        stop = true;
                        return;
                    }
                    tier.push_back(declaration.id);
                });
            if (stop) {
                break;
            }
            for (const auto candidate : tier) {
                const auto key = vhdl_callable_homograph_key(
                    *design_, effective_, candidate);
                if (!key) {
                    stop = true;
                    break;
                }
                if (std::ranges::find(keys, *key) == keys.end()) {
                    outer.push_back(candidate);
                    keys.push_back(*key);
                }
            }
            if (stop) {
                break;
            }
            advance();
        }
        if (!outer.empty()) {
            auto merged = visible.candidates;
            merged.insert(merged.end(), outer.begin(), outer.end());
            visible = declaration_result(std::move(merged));
        }
    }
    const auto is_alias = [&](const DeclarationId id) {
        const auto declaration = find_declaration(*design_, effective_, id);
        return declaration && declaration->vhdl != nullptr
            && declaration->vhdl->form == vhdl::DeclarationForm::alias;
    };
    if (std::ranges::none_of(visible.candidates, is_alias)) {
        return visible;
    }
    thread_local std::size_t alias_depth { };
    if (alias_depth > 8U) {
        return { };
    }
    ++alias_depth;
    struct DepthGuard {
        std::size_t& depth;
        ~DepthGuard() { --depth; }
    } depth_guard { alias_depth };
    const auto signature_matches = [&](const vhdl::Declaration& alias,
                                       const DeclarationId target_id) {
        return vhdl_callable_matches_signature(
            *alias.alias_signature, alias.scope, target_id);
    };
    std::vector<DeclarationId> expanded;
    for (const auto candidate : visible.candidates) {
        if (!is_alias(candidate)) {
            expanded.push_back(candidate);
            continue;
        }
        const auto alias = find_declaration(*design_, effective_, candidate);
        auto target_name = *alias->vhdl->alias_target;
        target_name.selected.reset();
        target_name.overloads.clear();
        const auto targets = resolve_vhdl_callable_candidates(
            target_name, alias->vhdl->scope);
        for (const auto target : targets.candidates) {
            if (signature_matches(*alias->vhdl, target)
                && std::ranges::find(expanded, target) == expanded.end()) {
                expanded.push_back(target);
            }
        }
    }
    return declaration_result(std::move(expanded));
}

bool CompiledDesignResolver::vhdl_callable_matches_signature(
    const vhdl::AliasSignature& signature, const ScopeId signature_scope,
    const DeclarationId target_id) const
{
    if (design_ == nullptr) {
        return false;
    }
    // Signature type marks and formal subtypes match by base type.
    const auto type_key = [&](const vhdl::SubtypeIndication& subtype,
                              const ScopeId scope)
        -> std::optional<std::string> {
        auto linked = effective_vhdl_subtype(subtype, scope);
        if (!linked) {
            linked = subtype;
        }
        const auto root = [&](const TypeId type) {
            const auto id = effective_ != nullptr
                ? vhdl_root_type(*effective_, type)
                : vhdl_root_type(*design_, type);
            return "#" + std::to_string(id.value());
        };
        const auto builtin = [](vhdl::BuiltinTypeIdentity identity) {
            if (identity
                == vhdl::BuiltinTypeIdentity::ieee_std_logic_1164_std_logic_vector) {
                identity
                    = vhdl::BuiltinTypeIdentity::ieee_std_logic_1164_std_ulogic_vector;
            }
            return "b" + std::to_string(static_cast<unsigned>(identity));
        };
        if (linked->type_mark.target.valid()) {
            return root(linked->type_mark.target);
        }
        if (linked->builtin_type != vhdl::BuiltinTypeIdentity::none) {
            return builtin(linked->builtin_type);
        }
        const auto identity = resolve_vhdl_type_identity(
            linked->type_mark.spelling, scope);
        if (identity.type) {
            return root(*identity.type);
        }
        if (identity.builtin_type != vhdl::BuiltinTypeIdentity::none) {
            return builtin(identity.builtin_type);
        }
        if (identity.standard_scalar_type
            != CompiledVhdlStandardScalarIdentity::none) {
            return "s"
                + std::to_string(
                    static_cast<unsigned>(identity.standard_scalar_type));
        }
        if (linked->domain != vhdl::ValueDomain::unknown) {
            return "d"
                + std::to_string(static_cast<unsigned>(linked->domain));
        }
        return std::nullopt;
    };
    const auto target = find_declaration(*design_, effective_, target_id);
    if (!target || target->vhdl == nullptr || !target->vhdl->callable) {
        return false;
    }
    const auto& profile = *target->vhdl->callable;
    if (profile.function != signature.result.has_value()
        || profile.formals.size() != signature.parameters.size()) {
        return false;
    }
    for (std::size_t index { }; index < profile.formals.size(); ++index) {
        const auto formal = find_declaration(
            *design_, effective_, profile.formals[index]);
        if (!formal || formal->vhdl == nullptr || !formal->vhdl->subtype) {
            return false;
        }
        const auto formal_key = type_key(
            *formal->vhdl->subtype, formal->vhdl->scope);
        if (!formal_key
            || formal_key
                != type_key(signature.parameters[index], signature_scope)) {
            return false;
        }
    }
    if (signature.result) {
        const auto& result = profile.return_type
            ? profile.return_type
            : target->vhdl->subtype;
        const auto result_key = result
            ? type_key(*result, target->vhdl->scope)
            : std::nullopt;
        if (!result_key
            || result_key != type_key(*signature.result, signature_scope)) {
            return false;
        }
    }
    return true;
}

CompiledVhdlCallableResolutionResult
CompiledDesignResolver::resolve_vhdl_callables(
    const vhdl::Name& name, const ScopeId use_scope) const
{
    const auto visible = resolve_vhdl_callable_candidates(name, use_scope);
    auto package_members = resolve_vhdl_package_members(
        name, use_scope, [](const auto& view) {
            return view.vhdl != nullptr
                && vhdl_callable_form(view.vhdl->form)
                && (view.vhdl->callable.has_value()
                    || view.vhdl->form
                        == vhdl::DeclarationForm::generic_function_instance
                || view.vhdl->form
                        == vhdl::DeclarationForm::generic_procedure_instance);
        });
    const auto parts = vhdl_name_parts(vhdl_spelling(name));
    if (parts.size() == 1U && effective_ != nullptr) {
        std::optional<std::string_view> selected_package;
        const auto selected = name.selected
            ? find_declaration(*design_, effective_, *name.selected)
            : std::nullopt;
        if (selected && selected->vhdl != nullptr
            && selected->vhdl->scope.valid()) {
            const auto* scope = find_scope(
                *design_, selected->vhdl->scope);
            const auto owner = scope != nullptr
                ? design_->find_unit(scope->unit)
                : std::nullopt;
            if (owner && owner->vhdl != nullptr
                && owner->vhdl->kind == vhdl::UnitKind::package) {
                selected_package = owner->vhdl->name;
            }
        }
        for (const auto& actual :
            effective_->specialization().actual_identities) {
            const auto formal = find_declaration(
                *design_, effective_, actual.declaration);
            if (!formal || formal->vhdl == nullptr
                || formal->vhdl->form
                    != vhdl::DeclarationForm::generic_package
                || !formal->vhdl->package) {
                continue;
            }
            auto template_name = vhdl_spelling(
                formal->vhdl->package->template_name);
            const auto separator = template_name.rfind('.');
            if (separator != std::string_view::npos) {
                template_name.remove_prefix(separator + 1U);
            }
            if (selected_package
                && !same_vhdl_identifier(
                    template_name, *selected_package)) {
                continue;
            }
            vhdl::Name qualified;
            qualified.spelling = formal->vhdl->name + "."
                + std::string { parts.front() };
            qualified.canonical = qualified.spelling;
            auto active = resolve_vhdl_package_members(
                qualified, use_scope, [](const auto& view) {
                    return view.vhdl != nullptr
                        && vhdl_callable_form(view.vhdl->form)
                        && (view.vhdl->callable.has_value()
                            || view.vhdl->form
                                == vhdl::DeclarationForm::
                                    generic_function_instance
                            || view.vhdl->form
                                == vhdl::DeclarationForm::
                                    generic_procedure_instance);
                });
            package_members.candidates.insert(
                package_members.candidates.end(),
                std::make_move_iterator(active.candidates.begin()),
                std::make_move_iterator(active.candidates.end()));
        }
    }
    if (parts.size() == 1U && !visible.candidates.empty()) {
        const auto imported = [&](const DeclarationId declaration) {
            return std::ranges::any_of(
                package_members.candidates, [&](const auto& candidate) {
                    return candidate.member == declaration;
                });
        };
        // resolve_vhdl() already applies lexical-tier hiding.  A local
        // declaration hides an imported one only when the two are homographs
        // (IEEE 1076-2008 12.3, 12.4); imported overloads with other
        // parameter and result type profiles stay visible.  An unknown
        // profile is treated as a homograph.
        const auto profile_key = [&](const DeclarationId id) {
            return vhdl_callable_homograph_key(*design_, effective_, id);
        };
        std::vector<std::optional<std::string>> local_profiles;
        for (const auto declaration : visible.candidates) {
            if (!imported(declaration)) {
                local_profiles.push_back(profile_key(declaration));
            }
        }
        if (!local_profiles.empty()) {
            const auto unknown_local = std::ranges::any_of(local_profiles,
                [](const auto& profile) { return !profile.has_value(); });
            std::erase_if(package_members.candidates, [&](const auto& candidate) {
                if (unknown_local) {
                    return true;
                }
                const auto profile = profile_key(candidate.member);
                return !profile
                    || std::ranges::find(local_profiles, profile)
                        != local_profiles.end();
            });
        }
    }
    if (vhdl_resolve_trace_enabled) {
        std::fprintf(stderr,
            "FSIM_VHDL_RESOLVE callables name=%s scope=%u lexical=%zu "
            "packages=%zu\n",
            std::string { vhdl_spelling(name) }.c_str(), use_scope.value(),
            visible.candidates.size(), package_members.candidates.size());
        for (const auto declaration : visible.candidates) {
            std::fprintf(stderr, "  lexical %u\n", declaration.value());
        }
        for (const auto& candidate : package_members.candidates) {
            std::fprintf(stderr, "  package %u\n", candidate.member.value());
        }
    }
    std::vector<CompiledVhdlCallableResolution> candidates;
    std::set<DeclarationId> package_declarations;
    for (const auto& package : package_members.candidates) {
        package_declarations.insert(package.member);
        auto package_frames = std::vector<CompiledBindingFrame> {
            binding_frames_.begin(), binding_frames_.end()
        };
        if (!package.generic_bindings.empty()) {
            package_frames.push_back(package.generic_bindings);
        }
        auto resolved = CompiledDesignResolver {
            *design_, selected_unit_, effective_, package_frames
        }.resolve_vhdl_callable(package.member);
        for (auto candidate : resolved.candidates) {
            candidate.package_instance = package.package_instance;
            candidate.generic_bindings.insert(
                candidate.generic_bindings.begin(),
                package.generic_bindings.begin(),
                package.generic_bindings.end());
            candidates.push_back(std::move(candidate));
        }
    }
    const auto qualified = vhdl_name_parts(vhdl_spelling(name)).size() > 1U;
    // The generic package (library and primary name) declaring `id`, for a
    // declaration of its specification or its body.
    const auto generic_package_of = [&](const DeclarationId id)
        -> std::optional<std::pair<std::string_view, std::string_view>> {
        const auto declaration = find_declaration(*design_, effective_, id);
        const auto* scope = declaration && declaration->vhdl != nullptr
            ? find_scope(*design_, declaration->vhdl->scope)
            : nullptr;
        const auto owner = scope != nullptr
            ? design_->find_unit(scope->unit)
            : std::nullopt;
        if (!owner || owner->vhdl == nullptr
            || owner->vhdl->kind != vhdl::UnitKind::package) {
            return std::nullopt;
        }
        return std::pair { std::string_view { owner->vhdl->library },
            std::string_view { owner->vhdl->primary_name.empty()
                    ? owner->vhdl->name
                    : owner->vhdl->primary_name } };
    };
    for (const auto declaration : visible.candidates) {
        // A selected package name is resolved through the package instance,
        // because its generic environment is part of callable identity.  The
        // frontend-selected declaration may have been relocated or forwarded
        // and therefore need not equal the package member record.
        if ((qualified && !package_members.candidates.empty())
            || package_declarations.contains(declaration)) {
            continue;
        }
        const auto resolved = resolve_vhdl_callable(declaration);
        for (auto candidate : resolved.candidates) {
            // Specialization may relocate a reference to a generic
            // subprogram formal directly to its concrete actual.  Package
            // lookup still reaches that same body through the instantiated
            // package and preserves the package's complete generic binding
            // environment.  Prefer that richer resolution; retaining the
            // bare relocated declaration as a second candidate makes one
            // callable look ambiguous and loses the bindings needed by its
            // parameter and result profiles.
            const auto instantiated_alias = std::ranges::any_of(
                candidates, [&](const auto& retained) {
                    return retained.package_instance
                        && retained.body == candidate.body;
                });
            // A subprogram of a generic package named from within that
            // package's instance (a sibling call in the body) denotes the
            // instance's own copy (IEEE 1076-2008 4.9), so it keeps the
            // innermost active frame that binds the package's generics.
            if (!instantiated_alias && candidate.generic_bindings.empty()) {
                if (const auto owner = generic_package_of(candidate.body)) {
                    for (auto frame = binding_frames_.rbegin();
                         frame != binding_frames_.rend(); ++frame) {
                        const auto binds_owner = std::ranges::any_of(*frame,
                            [&](const CompiledActualBinding& binding) {
                                const auto formal
                                    = generic_package_of(binding.formal);
                                return formal
                                    && same_vhdl_identifier(
                                        formal->first, owner->first)
                                    && same_vhdl_identifier(
                                        formal->second, owner->second);
                            });
                        if (binds_owner) {
                            candidate.generic_bindings = *frame;
                            break;
                        }
                    }
                }
            }
            if (!instantiated_alias) {
                candidates.push_back(std::move(candidate));
            }
        }
    }
    normalize_vhdl_callable_resolutions(candidates);
    return { status_for(candidates.size()), std::move(candidates) };
}

CompiledVhdlCallableResolutionResult
CompiledDesignResolver::resolve_vhdl_callable(
    const DeclarationId requested) const
{
    auto selected = requested;
    CompiledBindingFrame propagated_bindings;
    const auto retain_frame = [&](const CompiledBindingFrame& frame) {
        for (const auto& binding : frame) {
            const auto retained = std::ranges::find(
                propagated_bindings, binding.formal,
                &CompiledActualBinding::formal);
            if (retained == propagated_bindings.end()) {
                propagated_bindings.push_back(binding);
            }
        }
    };
    const auto referenced_declaration = [&](const ExpressionId expression)
        -> std::optional<DeclarationId> {
        const auto view = find_expression(*design_, effective_, expression);
        const auto* name = view && view->vhdl != nullptr
                && view->vhdl->referenced_name
            ? &*view->vhdl->referenced_name
            : nullptr;
        if (name == nullptr) {
            return std::nullopt;
        }
        if (name->selected) {
            return name->selected;
        }
        return name->overloads.size() == 1U
            ? std::optional { name->overloads.front() }
            : std::nullopt;
    };
    std::optional<CompiledBindingFrame> specialization_bindings;
    const auto retain_specialization_bindings = [&] {
        if (effective_ == nullptr) {
            return;
        }
        if (!specialization_bindings) {
            specialization_bindings.emplace();
            const auto& actuals
                = effective_->specialization().actual_identities;
            specialization_bindings->reserve(actuals.size());
            for (const auto& actual : actuals) {
                specialization_bindings->push_back({
                    actual.declaration,
                    actual.actual_expression,
                    actual.actual_declaration,
                    actual.systemverilog_type,
                    actual.vhdl_type,
                });
            }
        }
        retain_frame(*specialization_bindings);
    };
    const auto next_actual = [&](const DeclarationId formal)
        -> std::optional<DeclarationId> {
        for (auto frame = binding_frames_.rbegin();
             frame != binding_frames_.rend(); ++frame) {
            const auto binding = std::ranges::find(
                *frame, formal, &CompiledActualBinding::formal);
            if (binding == frame->end()) {
                continue;
            }
            const auto actual = binding->actual_declaration
                ? binding->actual_declaration
                : binding->expression
                ? referenced_declaration(*binding->expression)
                : std::nullopt;
            // An actual declared outside the region that declares the
            // formal (`"=" => eq42` from the instantiating architecture)
            // runs in its own environment, not the instance's (IEEE
            // 1076-2008 6.5.6.2, 12.1).
            if (actual) {
                const auto formal_view = find_declaration(
                    *design_, effective_, formal);
                const auto actual_view = find_declaration(
                    *design_, effective_, *actual);
                if (formal_view && formal_view->vhdl != nullptr
                    && actual_view && actual_view->vhdl != nullptr
                    && formal_view->vhdl->scope.valid()
                    && actual_view->vhdl->scope.valid()
                    && !scope_within(design_->semantics,
                        actual_view->vhdl->scope, formal_view->vhdl->scope)
                    && actual_view->vhdl->form
                        != vhdl::DeclarationForm::generic_function
                    && actual_view->vhdl->form
                        != vhdl::DeclarationForm::generic_procedure) {
                    return actual;
                }
            }
            // Follow a callable actual one frame at a time. Flattening the
            // chain through actual_declaration() loses the sibling value,
            // type, package, and subprogram bindings from intermediate
            // package-generic environments. Retain the matching frame and
            // every enclosing frame because an explicit inner association
            // can already name a flattened callable while its sibling type
            // or value associations still forward through an outer frame.
            for (auto environment = frame;
                 environment != binding_frames_.rend(); ++environment) {
                retain_frame(*environment);
            }
            retain_specialization_bindings();
            return actual;
        }
        if (effective_ == nullptr) {
            return std::nullopt;
        }
        const auto& actuals
            = effective_->specialization().actual_identities;
        const auto actual = std::ranges::find(
            actuals, formal,
            &SpecializedHirActualIdentity::declaration);
        if (actual == actuals.end()) {
            return std::nullopt;
        }
        retain_specialization_bindings();
        return actual->actual_declaration
            ? actual->actual_declaration
            : actual->actual_expression
            ? referenced_declaration(*actual->actual_expression)
            : std::nullopt;
    };
    std::set<DeclarationId> visiting;
    while (visiting.insert(selected).second) {
        const auto declaration = find_declaration(
            *design_, effective_, selected);
        if (!declaration || declaration->vhdl == nullptr) {
            return { CompiledResolutionStatus::not_found, { } };
        }
        const auto form = declaration->vhdl->form;
        if (form == vhdl::DeclarationForm::generic_function
            || form == vhdl::DeclarationForm::generic_procedure) {
            const auto actual = next_actual(selected);
            if (!actual) {
                return { CompiledResolutionStatus::not_found, { } };
            }
            selected = *actual;
            continue;
        }

        if (form == vhdl::DeclarationForm::generic_function_instance
            || form
                == vhdl::DeclarationForm::generic_procedure_instance) {
            if (!declaration->vhdl->package) {
                return { CompiledResolutionStatus::invalid, { } };
            }
            // A box default of the instance's own generic list can name the
            // instance being resolved (`procedure p is new p ...`); it does
            // not denote that instance (IEEE 1076-2008 4.4, 6.5.6.2).
            thread_local std::vector<DeclarationId> resolving_instances;
            if (std::ranges::find(resolving_instances, selected)
                != resolving_instances.end()) {
                return { CompiledResolutionStatus::not_found, { } };
            }
            resolving_instances.push_back(selected);
            struct ResolvingInstance {
                std::vector<DeclarationId>& active;
                ~ResolvingInstance() { active.pop_back(); }
            } resolving_instance { resolving_instances };
            const auto& instance = *declaration->vhdl->package;
            std::vector<DeclarationId> templates;
            if (instance.template_name.selected) {
                templates.push_back(*instance.template_name.selected);
            } else {
                // An expanded template name (`work.pkg.f`) names the
                // template by its last part (IEEE 1076-2008 4.4, 8.3).
                const auto template_spelling
                    = vhdl_spelling(instance.template_name);
                const auto template_simple_name = std::string_view {
                    template_spelling
                }.substr(std::string_view { template_spelling }.rfind('.')
                    + 1U);
                for (const auto& candidate :
                    design_->vhdl_hir.declarations()) {
                    const auto template_form
                        = candidate.form
                            == vhdl::DeclarationForm::generic_function_template
                        || candidate.form
                            == vhdl::DeclarationForm::generic_procedure_template;
                    if (template_form && same_vhdl_identifier(candidate.name,
                            template_simple_name)) {
                        templates.push_back(candidate.id);
                    }
                }
            }
            sort_unique(templates);
            // The template's subprogram, whose type marks distinguish
            // overloaded templates (IEEE 1076-2008 4.5.1) and pair a
            // declaration with its body (4.10).
            const auto template_subprogram
                = [&](const vhdl::Declaration& candidate)
                -> std::optional<DeclarationId> {
                for (const auto child : candidate.children) {
                    const auto record = find_declaration(
                        *design_, effective_, child);
                    if (record && record->vhdl != nullptr
                        && record->vhdl->callable
                        && (record->vhdl->form
                                == vhdl::DeclarationForm::function
                            || record->vhdl->form
                                == vhdl::DeclarationForm::procedure)) {
                        return child;
                    }
                }
                return std::nullopt;
            };
            const auto template_profile
                = [&](const vhdl::Declaration& candidate) {
                std::string profile;
                const auto subprogram = template_subprogram(candidate);
                const auto record = subprogram
                    ? find_declaration(*design_, effective_, *subprogram)
                    : std::nullopt;
                if (!record || record->vhdl == nullptr) {
                    return profile;
                }
                const auto spelling
                    = [](const vhdl::SubtypeIndication& subtype) {
                    std::string text { subtype.type_mark.spelling };
                    for (auto& character : text) {
                        character = static_cast<char>(std::tolower(
                            static_cast<unsigned char>(character)));
                    }
                    return text;
                };
                for (const auto formal_id : record->vhdl->callable->formals) {
                    const auto formal = find_declaration(
                        *design_, effective_, formal_id);
                    if (formal && formal->vhdl != nullptr
                        && formal->vhdl->subtype) {
                        profile += spelling(*formal->vhdl->subtype);
                    }
                    profile.push_back(',');
                }
                if (record->vhdl->callable->return_type) {
                    profile += "return "
                        + spelling(*record->vhdl->callable->return_type);
                }
                return profile;
            };
            // `f is new g [real, integer return integer]` selects one of
            // overloaded templates by its signature (IEEE 1076-2019 4.4).
            if (declaration->vhdl->alias_signature) {
                std::erase_if(templates, [&](const DeclarationId id) {
                    const auto view = find_declaration(
                        *design_, effective_, id);
                    const auto subprogram = view && view->vhdl != nullptr
                        ? template_subprogram(*view->vhdl)
                        : std::nullopt;
                    return !subprogram
                        || !vhdl_callable_matches_signature(
                            *declaration->vhdl->alias_signature,
                            declaration->vhdl->scope, *subprogram);
                });
            }
            std::vector<CompiledVhdlCallableResolution> results;
            for (const auto template_id : templates) {
                auto template_view = find_declaration(
                    *design_, effective_, template_id);
                if (!template_view || template_view->vhdl == nullptr) {
                    continue;
                }
                const auto profile = template_profile(*template_view->vhdl);
                std::vector<DeclarationId> completions { template_id };
                const auto* template_scope = find_scope(
                    *design_, template_view->vhdl->scope);
                const auto template_owner = template_scope != nullptr
                    ? design_->find_unit(template_scope->unit)
                    : std::nullopt;
                for (const auto& candidate :
                    design_->vhdl_hir.declarations()) {
                    if (candidate.id == template_id
                        || candidate.form != template_view->vhdl->form
                        || !same_vhdl_identifier(candidate.name,
                            template_view->vhdl->name)) {
                        continue;
                    }
                    const auto candidate_view = find_declaration(
                        *design_, effective_, candidate.id);
                    const auto* candidate_scope = candidate_view
                            && candidate_view->vhdl != nullptr
                        ? find_scope(*design_,
                              candidate_view->vhdl->scope)
                        : nullptr;
                    const auto candidate_owner = candidate_scope != nullptr
                        ? design_->find_unit(candidate_scope->unit)
                        : std::nullopt;
                    const auto same_unit_family = template_owner
                        && candidate_owner
                        && (template_owner->identity->id
                                == candidate_owner->identity->id
                            || (template_owner->vhdl != nullptr
                                && candidate_owner->vhdl != nullptr
                                && template_owner->vhdl->kind
                                    == vhdl::UnitKind::package
                                && candidate_owner->vhdl->kind
                                    == vhdl::UnitKind::package
                                && same_vhdl_identifier(
                                    normalized_library(
                                        template_owner->vhdl->library),
                                    normalized_library(
                                        candidate_owner->vhdl->library))
                                && same_vhdl_identifier(
                                    template_owner->vhdl->name,
                                    candidate_owner->vhdl->name)));
                    if (same_unit_family
                        && template_profile(candidate) == profile) {
                        completions.push_back(candidate.id);
                    }
                }
                for (const auto completion : completions) {
                    const auto completed = find_declaration(
                        *design_, effective_, completion);
                    if (!completed || completed->vhdl == nullptr) {
                        continue;
                    }
                    std::vector<DeclarationId> generics;
                    std::vector<DeclarationId> bodies;
                    for (const auto child : completed->vhdl->children) {
                        const auto record = find_declaration(
                            *design_, effective_, child);
                        if (!record || record->vhdl == nullptr) {
                            continue;
                        }
                        if (vhdl::is_generic_formal(record->vhdl->form)) {
                            generics.push_back(child);
                        } else if (record->vhdl->form
                                == vhdl::DeclarationForm::function
                            || record->vhdl->form
                                == vhdl::DeclarationForm::procedure) {
                            if (record->vhdl->callable
                                && record->vhdl->callable->defined) {
                                bodies.push_back(child);
                            }
                        }
                    }
                    const auto bindings = bind_vhdl_generics(
                        generics, instance.generic_map,
                        declaration->vhdl->scope);
                    if (bodies.size() == 1U && bindings) {
                        auto resolved_bindings = propagated_bindings;
                        for (const auto& binding : *bindings) {
                            const auto retained = std::ranges::find(
                                resolved_bindings, binding.formal,
                                &CompiledActualBinding::formal);
                            if (retained == resolved_bindings.end()) {
                                resolved_bindings.push_back(binding);
                            } else {
                                *retained = binding;
                            }
                        }
                        results.push_back({ requested, bodies.front(),
                            std::nullopt,
                            std::move(resolved_bindings) });
                    }
                }
            }
            normalize_vhdl_callable_resolutions(results);
            return { status_for(results.size()), std::move(results) };
        }

        if (!declaration->vhdl->callable) {
            return { CompiledResolutionStatus::not_found, { } };
        }
        if (form != vhdl::DeclarationForm::function
            && form != vhdl::DeclarationForm::procedure) {
            return { CompiledResolutionStatus::invalid, { } };
        }
        if (declaration->vhdl->callable->defined) {
            // A recursive call in the body of a generic subprogram denotes
            // the instance being elaborated (IEEE 1076-2008 4.4): it keeps
            // the active bindings of the template's generics.
            if (propagated_bindings.empty() && !binding_frames_.empty()) {
                const auto owner = std::ranges::find_if(
                    design_->vhdl_hir.declarations(),
                    [&](const vhdl::Declaration& candidate) {
                        return (candidate.form
                                       == vhdl::DeclarationForm::
                                           generic_function_template
                                   || candidate.form
                                       == vhdl::DeclarationForm::
                                           generic_procedure_template)
                            && std::ranges::find(candidate.children, selected)
                                != candidate.children.end();
                    });
                if (owner != design_->vhdl_hir.declarations().end()) {
                    CompiledBindingFrame retained;
                    for (const auto child : owner->children) {
                        const auto generic = find_declaration(
                            *design_, effective_, child);
                        if (!generic || generic->vhdl == nullptr
                            || !vhdl::is_generic_formal(generic->vhdl->form)) {
                            continue;
                        }
                        for (auto frame = binding_frames_.rbegin();
                             frame != binding_frames_.rend(); ++frame) {
                            const auto binding = std::ranges::find(*frame,
                                child, &CompiledActualBinding::formal);
                            if (binding != frame->end()) {
                                retained.push_back(*binding);
                                break;
                            }
                        }
                    }
                    propagated_bindings = std::move(retained);
                }
            }
            return { CompiledResolutionStatus::unique,
                { { requested, selected, std::nullopt,
                    std::move(propagated_bindings) } } };
        }
        const auto* scope = find_scope(
            *design_, declaration->vhdl->scope);
        const auto owner = scope != nullptr
            ? design_->find_unit(scope->unit)
            : std::nullopt;
        if (!owner || owner->vhdl == nullptr
            || owner->vhdl->kind != vhdl::UnitKind::package) {
            return { CompiledResolutionStatus::not_found, { } };
        }
        std::vector<DeclarationId> bodies;
        for (const auto& unit : design_->vhdl_units()) {
            if (unit.kind != vhdl::UnitKind::package
                || unit.primary_name.empty()
                || !same_vhdl_identifier(unit.library,
                    owner->vhdl->library)
                || !same_vhdl_identifier(unit.name, owner->vhdl->name)) {
                continue;
            }
            for (const auto id : unit.declarations) {
                const auto candidate = find_declaration(
                    *design_, effective_, id);
                if (candidate && candidate->vhdl != nullptr
                    && candidate->vhdl->callable
                    && candidate->vhdl->callable->defined
                    && same_vhdl_identifier(candidate->vhdl->name,
                        declaration->vhdl->name)
                    && vhdl_callable_profile_matches(
                        requested, id, true)) {
                    bodies.push_back(id);
                }
            }
        }
        sort_unique(bodies);
        std::vector<CompiledVhdlCallableResolution> results;
        for (const auto body : bodies) {
            results.push_back(
                { requested, body, std::nullopt,
                    propagated_bindings });
        }
        return { status_for(results.size()), std::move(results) };
    }
    return { CompiledResolutionStatus::invalid, { } };
}

CompiledVhdlCallableResolutionResult
CompiledDesignResolver::resolve_vhdl_default_callable(
    const DeclarationId expected, const ScopeId use_scope,
    const vhdl::Name* const name) const
{
    std::vector<CompiledVhdlCallableResolution> candidates;
    const auto append = [&](const vhdl::Name& candidate_name) {
        auto resolved = resolve_vhdl_callables(candidate_name, use_scope);
        for (auto& candidate : resolved.candidates) {
            std::vector<CompiledBindingFrame> frames {
                binding_frames_.begin(), binding_frames_.end()
            };
            if (!candidate.generic_bindings.empty()) {
                frames.push_back(candidate.generic_bindings);
            }
            const CompiledDesignResolver profile_resolver {
                *design_, selected_unit_, effective_, frames
            };
            const auto key = find_declaration(
                *design_, effective_, candidate.key);
            const auto profile = key && key->vhdl != nullptr
                    && key->vhdl->callable
                ? candidate.key
                : candidate.body;
            if (profile_resolver.vhdl_callable_profile_matches(
                    expected, profile, true)) {
                candidates.push_back(std::move(candidate));
            }
        }
    };
    if (name != nullptr) {
        append(*name);
    } else if (const auto formal
               = find_declaration(*design_, effective_, expected);
               formal && formal->vhdl != nullptr) {
        // A box default denotes a visible subprogram with the formal's own
        // designator and a conforming profile (IEEE 1076-2008 6.5.6.2).
        vhdl::Name candidate_name;
        candidate_name.spelling = formal->vhdl->name;
        candidate_name.canonical = formal->vhdl->name;
        append(candidate_name);
    }
    normalize_vhdl_callable_resolutions(candidates);
    return { status_for(candidates.size()), std::move(candidates) };
}

bool CompiledDesignResolver::vhdl_callable_profile_matches(
    const DeclarationId expected_id, const DeclarationId candidate_id,
    const bool include_function_result) const
{
    return vhdl_callable_profiles_match(expected_id, candidate_id,
        include_function_result, true);
}

bool CompiledDesignResolver::vhdl_callable_profiles_homographic(
    const DeclarationId left, const DeclarationId right) const
{
    return vhdl_callable_profiles_match(left, right, true, false);
}

bool CompiledDesignResolver::vhdl_package_templates_match(
    const DeclarationId expected, const DeclarationId actual) const
{
    const auto left = find_declaration(*design_, effective_, expected);
    const auto right = find_declaration(*design_, effective_, actual);
    if (!left || left->vhdl == nullptr || !left->vhdl->package
        || !right || right->vhdl == nullptr || !right->vhdl->package) {
        return false;
    }
    const auto& left_name = left->vhdl->package->template_name;
    const auto& right_name = right->vhdl->package->template_name;
    if (left_name.selected && right_name.selected) {
        return *left_name.selected == *right_name.selected;
    }
    return same_vhdl_identifier(
        simple_vhdl_name(vhdl_spelling(left_name)),
        simple_vhdl_name(vhdl_spelling(right_name)));
}

bool CompiledDesignResolver::vhdl_package_generic_maps_match(
    const DeclarationId expected, const DeclarationId actual) const
{
    const auto left = find_declaration(*design_, effective_, expected);
    const auto right = find_declaration(*design_, effective_, actual);
    if (!left || left->vhdl == nullptr || !left->vhdl->package
        || !right || right->vhdl == nullptr || !right->vhdl->package
        || !vhdl_package_templates_match(expected, actual)) {
        return false;
    }
    const auto& expected_profile = *left->vhdl->package;
    const auto& actual_profile = *right->vhdl->package;
    if (expected_profile.generic_map_box) {
        return true;
    }

    auto owner_library = std::string_view { "work" };
    if (const auto* scope = find_scope(*design_, left->vhdl->scope)) {
        if (const auto unit = design_->find_unit(scope->unit);
            unit && unit->vhdl != nullptr) {
            owner_library = normalized_library(unit->vhdl->library);
        }
    }
    auto template_name = vhdl_spelling(expected_profile.template_name);
    auto template_library = owner_library;
    if (const auto separator = template_name.rfind('.');
        separator != std::string_view::npos) {
        template_library = effective_vhdl_library(
            template_name.substr(0U, separator), owner_library);
        template_name.remove_prefix(separator + 1U);
    }
    std::vector<const vhdl::Unit*> templates;
    for (const auto& unit : design_->vhdl_units()) {
        if (unit.kind == vhdl::UnitKind::package
            && unit.primary_name.empty()
            && same_vhdl_identifier(unit.name, template_name)
            && same_vhdl_identifier(
                normalized_library(unit.library), template_library)) {
            templates.push_back(&unit);
        }
    }
    if (templates.size() != 1U) {
        return false;
    }
    std::vector<DeclarationId> formals;
    for (const auto id : templates.front()->declarations) {
        const auto declaration = find_declaration(
            *design_, effective_, id);
        if (declaration && declaration->vhdl != nullptr
            && vhdl::is_generic_formal(declaration->vhdl->form)) {
            formals.push_back(id);
        }
    }
    const auto expected_bindings = bind_vhdl_generics(
        formals, expected_profile.generic_map, left->vhdl->scope);
    const auto actual_bindings = bind_vhdl_generics(
        formals, actual_profile.generic_map, right->vhdl->scope);
    if (!expected_bindings || !actual_bindings
        || expected_bindings->size() != actual_bindings->size()) {
        return false;
    }
    const auto expression_view = [&](const ExpressionId id) {
        return find_expression(*design_, effective_, id);
    };
    const auto effective_package_subtype = [&](
        vhdl::SubtypeIndication input, const ScopeId use_scope) {
        return effective_vhdl_subtype(input, use_scope)
            .value_or(std::move(input));
    };
    const auto binding_subtype = [&](const CompiledActualBinding& binding)
        -> std::optional<vhdl::SubtypeIndication> {
        if (binding.vhdl_type) {
            return binding.vhdl_type;
        }
        if (!binding.actual_declaration) {
            return std::nullopt;
        }
        const auto declaration = find_declaration(
            *design_, effective_, *binding.actual_declaration);
        if (!declaration || declaration->vhdl == nullptr
            || declaration->vhdl->form
                != vhdl::DeclarationForm::generic_type) {
            return std::nullopt;
        }
        for (auto frame = binding_frames_.rbegin();
             frame != binding_frames_.rend(); ++frame) {
            const auto mapped_actual = std::ranges::find(
                *frame, *binding.actual_declaration,
                &CompiledActualBinding::formal);
            if (mapped_actual == frame->end()) {
                continue;
            }
            if (mapped_actual->vhdl_type) {
                return mapped_actual->vhdl_type;
            }
            if (mapped_actual->actual_declaration) {
                const auto selected = find_declaration(
                    *design_, effective_,
                    *mapped_actual->actual_declaration);
                if (selected && selected->vhdl != nullptr) {
                    if (selected->vhdl->subtype) {
                        return selected->vhdl->subtype;
                    }
                    if (selected->vhdl->declared_type) {
                        vhdl::SubtypeIndication result;
                        result.type_mark.target
                            = *selected->vhdl->declared_type;
                        result.type_mark.spelling
                            = selected->vhdl->name;
                        return result;
                    }
                }
            }
            break;
        }
        if (effective_ != nullptr) {
            const auto& actuals
                = effective_->specialization().actual_identities;
            const auto specialization_actual = std::ranges::find(
                actuals, *binding.actual_declaration,
                &SpecializedHirActualIdentity::declaration);
            if (specialization_actual != actuals.end()) {
                if (specialization_actual->vhdl_type) {
                    return specialization_actual->vhdl_type;
                }
                if (specialization_actual->actual_declaration) {
                    const auto selected = find_declaration(
                        *design_, effective_,
                        *specialization_actual->actual_declaration);
                    if (selected && selected->vhdl != nullptr) {
                        if (selected->vhdl->subtype) {
                            return selected->vhdl->subtype;
                        }
                        if (selected->vhdl->declared_type) {
                            vhdl::SubtypeIndication result;
                            result.type_mark.target
                                = *selected->vhdl->declared_type;
                            result.type_mark.spelling
                                = selected->vhdl->name;
                            return result;
                        }
                    }
                }
            }
        }
        return std::nullopt;
    };
    for (std::size_t index { }; index < expected_bindings->size();
         ++index) {
        const auto& expected_binding = (*expected_bindings)[index];
        const auto& actual_binding = (*actual_bindings)[index];
        // A formal map may read earlier generics of the same interface
        // list, bound by the association frames (IEEE 1076-2008 6.5.6.2).
        const auto integral_value = [&](const std::optional<ExpressionId>
                                            expression)
            -> std::optional<std::int64_t> {
            if (effective_ == nullptr || !expression) {
                return std::nullopt;
            }
            if (!binding_frames_.empty()) {
                if (const auto bound = effective_->evaluate_integral_expression(
                        *expression,
                        vhdl_frames_integral_binding(binding_frames_))) {
                    return bound;
                }
            }
            return effective_->evaluate_integral_expression(*expression);
        };
        const auto expected_value = integral_value(expected_binding.expression);
        const auto actual_value = integral_value(actual_binding.expression);
        if (expected_value || actual_value) {
            if (expected_value != actual_value) {
                return false;
            }
            continue;
        }
        const auto expected_type = binding_subtype(expected_binding);
        const auto actual_type = binding_subtype(actual_binding);
        if (expected_type || actual_type) {
            if (!expected_type || !actual_type
                || !same_vhdl_subtype(
                    *design_, effective_, effective_package_subtype(*expected_type, left->vhdl->scope),
                    effective_package_subtype(
                        *actual_type, right->vhdl->scope))) {
                return false;
            }
            continue;
        }
        if (expected_binding.actual_declaration
            || actual_binding.actual_declaration) {
            if (!expected_binding.actual_declaration
                || !actual_binding.actual_declaration) {
                return false;
            }
            const auto expected_declaration = actual_declaration(
                *expected_binding.actual_declaration)
                    .value_or(*expected_binding.actual_declaration);
            const auto actual_declaration_id = actual_declaration(
                *actual_binding.actual_declaration)
                    .value_or(*actual_binding.actual_declaration);
            if (expected_declaration != actual_declaration_id) {
                return false;
            }
            continue;
        }
        if (expected_binding.expression || actual_binding.expression) {
            if (!expected_binding.expression || !actual_binding.expression) {
                return false;
            }
            const auto expected_expression = expression_view(
                *expected_binding.expression);
            const auto actual_expression = expression_view(
                *actual_binding.expression);
            if (!expected_expression || !actual_expression
                || expected_expression->vhdl == nullptr
                || actual_expression->vhdl == nullptr
                || expected_expression->vhdl->kind
                    != actual_expression->vhdl->kind
                || expected_expression->vhdl->text
                    != actual_expression->vhdl->text) {
                return false;
            }
        }
    }
    return true;
}

bool CompiledDesignResolver::vhdl_callable_profiles_match(
    const DeclarationId expected_id, const DeclarationId candidate_id,
    const bool include_function_result,
    const bool compare_interface_modes) const
{
    const auto expected = find_declaration(
        *design_, effective_, expected_id);
    const auto candidate = find_declaration(
        *design_, effective_, candidate_id);
    if (!expected || expected->vhdl == nullptr
        || !expected->vhdl->callable || !candidate
        || candidate->vhdl == nullptr || !candidate->vhdl->callable
        || expected->vhdl->callable->function
            != candidate->vhdl->callable->function
        || expected->vhdl->callable->formals.size()
            != candidate->vhdl->callable->formals.size()) {
        return false;
    }
    for (std::size_t index { };
         index < expected->vhdl->callable->formals.size(); ++index) {
        const auto left = find_declaration(*design_, effective_,
            expected->vhdl->callable->formals[index]);
        const auto right = find_declaration(*design_, effective_,
            candidate->vhdl->callable->formals[index]);
        if (!left || left->vhdl == nullptr || !left->vhdl->subtype
            || !right || right->vhdl == nullptr || !right->vhdl->subtype
            || (compare_interface_modes
                && (left->vhdl->direction
                        != right->vhdl->direction
                    || left->vhdl->object_class
                        != right->vhdl->object_class))
            || !vhdl_subtype_profiles_match(
                *left->vhdl->subtype, left->vhdl->scope,
                *right->vhdl->subtype, right->vhdl->scope)) {
            return false;
        }
    }
    if (!include_function_result
        || !expected->vhdl->callable->function) {
        return true;
    }
    const auto* left = expected->vhdl->callable->return_type
        ? &*expected->vhdl->callable->return_type
        : expected->vhdl->subtype ? &*expected->vhdl->subtype : nullptr;
    const auto* right = candidate->vhdl->callable->return_type
        ? &*candidate->vhdl->callable->return_type
        : candidate->vhdl->subtype ? &*candidate->vhdl->subtype : nullptr;
    return left != nullptr && right != nullptr
        && vhdl_subtype_profiles_match(*left, expected->vhdl->scope,
            *right, candidate->vhdl->scope);
}

bool CompiledDesignResolver::vhdl_procedure_is_time_free(
    const DeclarationId procedure) const
{
    const auto declaration = find_declaration(
        *design_, effective_, procedure);
    if (!declaration || declaration->vhdl == nullptr
        || !declaration->vhdl->callable
        || declaration->vhdl->callable->function) {
        return false;
    }
    std::set<StatementId> visited;
    const auto time_free = [&](const auto& self,
                               const StatementId statement_id) -> bool {
        if (!visited.insert(statement_id).second) {
            return true;
        }
        const auto statement = effective_ != nullptr
            ? effective_->find_statement(statement_id)
            : design_->find_statement(statement_id);
        if (!statement || statement->vhdl == nullptr) {
            return true;
        }
        const auto& source = *statement->vhdl;
        if (source.kind == vhdl::StatementKind::wait_statement
            || source.kind == vhdl::StatementKind::signal_assignment) {
            return false;
        }
        for (const auto nested : source.statements) {
            if (!self(self, nested)) {
                return false;
            }
        }
        for (const auto nested : source.else_statements) {
            if (!self(self, nested)) {
                return false;
            }
        }
        for (const auto& alternative : source.alternatives) {
            for (const auto nested : alternative.statements) {
                if (!self(self, nested)) {
                    return false;
                }
            }
        }
        return true;
    };
    return std::ranges::all_of(declaration->vhdl->statements,
        [&](const auto statement) { return time_free(time_free, statement); });
}

std::vector<CompiledVhdlDuplicateCallableProfile>
CompiledDesignResolver::duplicate_vhdl_callable_profiles(
    const std::span<const DeclarationId> declarations) const
{
    std::vector<DeclarationId> retained;
    std::vector<CompiledVhdlDuplicateCallableProfile> result;
    for (const auto id : declarations) {
        const auto declaration = find_declaration(
            *design_, effective_, id);
        if (!declaration || declaration->vhdl == nullptr
            || !declaration->vhdl->callable
            || !declaration->vhdl->callable->defined
            || (declaration->vhdl->form
                    != vhdl::DeclarationForm::function
                && declaration->vhdl->form
                    != vhdl::DeclarationForm::procedure)) {
            continue;
        }
        const auto duplicate = std::ranges::find_if(retained,
            [&](const DeclarationId candidate_id) {
                const auto candidate = find_declaration(
                    *design_, effective_, candidate_id);
                return candidate && candidate->vhdl != nullptr
                    && candidate->vhdl->form
                        == declaration->vhdl->form
                    && same_vhdl_identifier(candidate->vhdl->name,
                        declaration->vhdl->name)
                    && vhdl_callable_profiles_homographic(
                        candidate_id, id);
            });
        if (duplicate != retained.end()) {
            result.push_back({ *duplicate, id,
                declaration->vhdl->callable->function });
            continue;
        }
        retained.push_back(id);
    }
    return result;
}

CompiledDeclarationResolution
CompiledDesignResolver::resolve_expression_name(
    const ExpressionId expression,
    const CompiledDeclarationPredicate& predicate,
    const bool systemverilog_same_library_fallback) const
{
    if (std::ranges::find(
            resolving_expression_names_, expression)
        != resolving_expression_names_.end()) {
        return { CompiledResolutionStatus::not_found, { } };
    }
    resolving_expression_names_.push_back(expression);
    struct ExpressionResolutionGuard {
        std::vector<ExpressionId>& active;

        ~ExpressionResolutionGuard()
        {
            active.pop_back();
        }
    } guard { resolving_expression_names_ };

    const auto view = find_expression(*design_, effective_, expression);
    if (!view) {
        return { CompiledResolutionStatus::invalid, { } };
    }
    if (view->systemverilog != nullptr) {
        const auto& source = *view->systemverilog;
        const CompiledDeclarationPredicate expression_candidate
            = [&](const CompiledDeclarationView& declaration) {
                  if (predicate && !predicate(declaration)) {
                      return false;
                  }
                  return source.kind != sv::ExpressionKind::call
                      || (declaration.systemverilog != nullptr
                          && declaration.systemverilog->callable
                          && declaration.systemverilog->callable->function);
              };
        if (source.referenced_name) {
            return resolve_systemverilog_name(*source.referenced_name,
                source.scope, expression_candidate,
                systemverilog_same_library_fallback);
        }
        if (source.kind != sv::ExpressionKind::name
            && source.kind != sv::ExpressionKind::call) {
            return { CompiledResolutionStatus::not_found, { } };
        }
        return resolve_systemverilog(source.text, source.scope,
            expression_candidate,
            systemverilog_same_library_fallback);
    }
    const auto& source = *view->vhdl;
    if (!source.referenced_name) {
        return { CompiledResolutionStatus::not_found, { } };
    }
    return resolve_vhdl(
        *source.referenced_name, source.scope, predicate);
}

CompiledDeclarationResolution
CompiledDesignResolver::resolve_systemverilog_expression(
    const ExpressionId expression,
    const CompiledDeclarationPredicate& predicate,
    const bool same_library_fallback) const
{
    const auto resolved = resolve_expression_name(
        expression, predicate, same_library_fallback);
    if (resolved.status != CompiledResolutionStatus::not_found) {
        return resolved;
    }
    const auto view = find_expression(*design_, effective_, expression);
    if (!view || view->systemverilog == nullptr) {
        return { CompiledResolutionStatus::invalid, { } };
    }
    const auto& source = *view->systemverilog;
    const auto spelling = source.referenced_name
        ? std::string_view { source.referenced_name->spelling }
        : std::string_view { source.text };
    const auto separator = spelling.find('.');
    if (separator == std::string_view::npos) {
        return resolved;
    }
    if (separator == 0U || separator + 1U >= spelling.size()) {
        return { CompiledResolutionStatus::invalid, { } };
    }
    const auto root = spelling.substr(0U, separator);
    const auto package_separator = root.rfind("::");
    const auto package = package_separator == std::string_view::npos
        ? std::string_view { }
        : root.substr(0U, package_separator);
    const auto name = package_separator == std::string_view::npos
        ? root
        : root.substr(package_separator + 2U);
    // The package may carry its library (`work::P::s.x`).
    const auto simple_package = [](std::string_view qualifier) {
        while (true) {
            const auto next = qualifier.find("::");
            if (!simple_systemverilog_identifier(qualifier.substr(0U, next))) {
                return false;
            }
            if (next == std::string_view::npos) {
                return true;
            }
            qualifier.remove_prefix(next + 2U);
        }
    };
    if (!simple_systemverilog_identifier(name)
        || (!package.empty() && !simple_package(package))) {
        return { CompiledResolutionStatus::invalid, { } };
    }
    const auto member_path = spelling.substr(separator + 1U);
    const CompiledDeclarationPredicate target
        = [&](const CompiledDeclarationView& declaration) {
              if ((predicate && !predicate(declaration))
                  || declaration.systemverilog == nullptr
                  || !declaration.systemverilog->type) {
                  return false;
              }
              auto type = *declaration.systemverilog->type;
              auto remaining = member_path;
              std::size_t resolved_parameters { };
              // An unpacked structure member of a tagged union shares its
              // bit layout (IEEE 1800-2023 7.3.2).
              bool inside_tagged { };
              while (!remaining.empty()) {
                  // A typedef denotes its base type (IEEE 1800-2017 6.18),
                  // including another unit's type kept only by spelling.
                  type = underlying_systemverilog_type(
                      type, declaration.systemverilog->scope)
                             .value_or(type);
                  if (!type.target.target.valid()) {
                      return false;
                  }
                  auto definition
                      = find_type(*design_, effective_, type.target.target);
                  // A typedef of a structure (for example one imported
                  // from a package) denotes the structure.
                  std::unordered_set<std::uint32_t> aliases;
                  while (definition && definition->systemverilog != nullptr
                      && definition->systemverilog->form
                          == sv::TypeForm::alias
                      && definition->systemverilog->base.target.target
                             .valid()
                      && aliases.insert(definition->systemverilog->base
                                            .target.target.value())
                             .second) {
                      const auto base
                          = definition->systemverilog->base.target.target;
                      definition = find_type(*design_, effective_, base);
                  }
                  const auto structure = [&](const sv::TypeForm form) {
                      return form == sv::TypeForm::packed_structure
                          || form == sv::TypeForm::packed_union
                          || form == sv::TypeForm::tagged_union
                          || (inside_tagged
                              && form == sv::TypeForm::unpacked_structure);
                  };
                  if (!definition || definition->systemverilog == nullptr
                      || !structure(definition->systemverilog->form)) {
                      // A type parameter denotes its actual type in this
                      // specialization (IEEE 1800-2017 6.20.3).
                      const auto effective = effective_systemverilog_type(
                          type, declaration.systemverilog->scope);
                      if (resolved_parameters++ < 8U && effective
                          && !(*effective == type)
                          && effective->target.target.valid()) {
                          type = *effective;
                          continue;
                      }
                      return false;
                  }
                  const auto& record = *definition->systemverilog;
                  inside_tagged = inside_tagged
                      || record.form == sv::TypeForm::tagged_union;
                  const auto member_separator = remaining.find('.');
                  const auto member_name = remaining.substr(
                      0U, member_separator);
                  const auto member = std::ranges::find(
                      record.members, member_name,
                      &sv::PackedMember::name);
                  if (member == record.members.end()) {
                      return false;
                  }
                  if (member_separator == std::string_view::npos) {
                      return true;
                  }
                  type = member->type;
                  remaining.remove_prefix(member_separator + 1U);
              }
              return false;
          };
    return resolve_systemverilog(
        root, source.scope, target, same_library_fallback);
}

CompiledDeclarationResolution
CompiledDesignResolver::resolve_systemverilog_target(
    ExpressionId expression,
    const CompiledDeclarationPredicate& predicate,
    const bool same_library_fallback) const
{
    std::unordered_set<std::uint32_t> visiting;
    while (expression.valid()
        && visiting.insert(expression.value()).second) {
        const auto view = find_expression(*design_, effective_, expression);
        if (!view || view->systemverilog == nullptr) {
            return { CompiledResolutionStatus::invalid, { } };
        }
        const auto& source = *view->systemverilog;
        if ((source.kind != sv::ExpressionKind::index
                && source.kind != sv::ExpressionKind::slice)
            || source.operands.empty()) {
            return resolve_systemverilog_expression(
                expression, predicate, same_library_fallback);
        }
        expression = source.operands.front();
    }
    return { CompiledResolutionStatus::invalid, { } };
}

} // namespace fsim::semantic
