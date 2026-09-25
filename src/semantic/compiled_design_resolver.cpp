// SPDX-License-Identifier: Apache-2.0
#include "fsim/semantic/compiled_design_resolver.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <iterator>
#include <limits>
#include <ranges>
#include <set>
#include <tuple>
#include <unordered_set>

namespace fsim::semantic {
namespace {

std::string_view normalized_library(const std::string_view library)
{
    return library.empty() ? std::string_view { "work" } : library;
}

bool extended_vhdl_identifier(const std::string_view value)
{
    return value.size() >= 2U && value.front() == '\\'
        && value.back() == '\\';
}

bool same_vhdl_identifier(
    const std::string_view left, const std::string_view right)
{
    if (extended_vhdl_identifier(left)
        || extended_vhdl_identifier(right)) {
        return left == right;
    }
    const auto lower_ascii = [](const unsigned char character) {
        return character >= 'A' && character <= 'Z'
            ? static_cast<unsigned char>(character - 'A' + 'a')
            : character;
    };
    return left.size() == right.size()
        && std::ranges::equal(left, right,
            [&](const unsigned char lhs, const unsigned char rhs) {
                return lower_ascii(lhs) == lower_ascii(rhs);
            });
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
    return name.canonical.empty()
        ? std::string_view { name.spelling }
        : std::string_view { name.canonical };
}

std::vector<std::string_view> vhdl_name_parts(
    const std::string_view name)
{
    std::vector<std::string_view> result;
    std::size_t begin { };
    while (begin < name.size()) {
        const auto end = name.find('.', begin);
        const auto part = name.substr(begin,
            end == std::string_view::npos
                ? std::string_view::npos
                : end - begin);
        if (part.empty()) {
            return { };
        }
        result.push_back(part);
        if (end == std::string_view::npos) {
            break;
        }
        begin = end + 1U;
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

template <typename Visitor>
void visit_vhdl_declarations_in_scope(const CompiledDesign& design,
    const SpecializedHirUnit* const effective, const ScopeId scope,
    Visitor&& visitor)
{
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

std::optional<std::vector<DeclarationId>> rank_systemverilog_constants(
    const CompiledDesign& design, const SpecializedHirUnit* const effective,
    const std::vector<DeclarationId>& candidates)
{
    using Form = sv::DeclarationForm;
    auto winning_rank = 0U;
    std::vector<DeclarationId> ranked;
    for (const auto id : candidates) {
        const auto declaration = find_declaration(design, effective, id);
        if (!declaration || declaration->systemverilog == nullptr) {
            return std::nullopt;
        }
        const auto form = declaration->systemverilog->form;
        if (form != Form::parameter
            && form != Form::local_parameter
            && form != Form::enumeration_literal) {
            return std::nullopt;
        }
        const auto rank = form == Form::enumeration_literal ? 2U : 1U;
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
        return enumeration.form == sv::DeclarationForm::enumeration_literal
            && parameter.form == sv::DeclarationForm::local_parameter
            && enumeration.scope == parameter.scope
            && enumeration.source == parameter.source
            && enumeration.name == parameter.name;
    };
    return is_pair(*first->systemverilog, *second->systemverilog)
        || is_pair(*second->systemverilog, *first->systemverilog);
}

sv::TypeReference retain_systemverilog_declarator(
    sv::TypeReference replacement, const sv::TypeReference& use)
{
    if (use.packed_range) {
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

bool vhdl_actual_form(const vhdl::DeclarationForm form)
{
    using Form = vhdl::DeclarationForm;
    return form == Form::generic_constant
        || form == Form::generic_type
        || form == Form::generic_function
        || form == Form::generic_procedure
        || form == Form::generic_package;
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

bool same_vhdl_subtype(const vhdl::SubtypeIndication& left,
    const vhdl::SubtypeIndication& right)
{
    if (left.type_mark.target.valid()
        && right.type_mark.target.valid()
        && left.type_mark.target == right.type_mark.target) {
        return true;
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
    if (same_vhdl_identifier(spelling, "time")) {
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
    const auto primary_kind
        = secondary.kind == vhdl::UnitKind::architecture
        ? std::optional { vhdl::UnitKind::entity }
        : secondary.kind == vhdl::UnitKind::package
                && !secondary.primary_name.empty()
        ? std::optional { vhdl::UnitKind::package }
        : std::nullopt;
    if (!primary_kind) {
        return std::nullopt;
    }
    const vhdl::Unit* selected { };
    for (const auto& unit : design.vhdl_units()) {
        const auto package_declaration
            = *primary_kind == vhdl::UnitKind::package;
        if (unit.kind != *primary_kind
            || (package_declaration && !unit.primary_name.empty())
            || !same_vhdl_identifier(
                normalized_library(unit.library),
                normalized_library(secondary.library))
            || !same_vhdl_identifier(
                unit.name, package_declaration
                    ? std::string_view { secondary.name }
                    : std::string_view { secondary.primary_name })) {
            continue;
        }
        if (selected != nullptr) {
            return std::nullopt;
        }
        selected = &unit;
    }
    return selected != nullptr
        ? std::optional<const vhdl::Unit*> { selected }
        : std::nullopt;
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
    const auto direct = result;
    for (const auto* unit : direct) {
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
                    const auto qualified = parts.size() == 3U;
                    const auto specified_library = qualified
                        ? std::string_view { parts.front() }
                        : normalized_library(unit->library);
                    const auto library = same_vhdl_identifier(
                            specified_library, "work")
                        ? normalized_library(unit->library)
                        : specified_library;
                    const auto package = qualified
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

bool standard_package_exports(const vhdl::Unit& package,
    const std::string_view member)
{
    return std::ranges::any_of(package.standard_package_declarations,
        [&](const std::string_view candidate) {
            return same_vhdl_identifier(candidate, member);
        });
}

bool builtin_standard_package_exports(const std::string_view library,
    const std::string_view package, const std::string_view member)
{
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

} // namespace

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
                const auto view = effective_ != nullptr
                    ? effective_->find_expression(*binding->expression)
                    : design_->find_expression(*binding->expression);
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
                if (!next && actual->actual_expression) {
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
    const bool same_library_fallback) const
{
    if (design_ == nullptr) {
        return { CompiledResolutionStatus::invalid, { } };
    }
    const auto separator = name.rfind("::");
    const auto member = separator == std::string_view::npos
        ? name
        : name.substr(separator + 2U);
    const auto package_name = separator == std::string_view::npos
        ? std::string_view { }
        : name.substr(0U, separator);
    if (member.empty() || (separator != std::string_view::npos
                              && package_name.empty())) {
        return { CompiledResolutionStatus::invalid, { } };
    }

    auto owner = design_->find_unit(selected_unit_);
    if (const auto* scope = find_scope(*design_, use_scope)) {
        if (const auto scoped = design_->find_unit(scope->unit);
            scoped && scoped->systemverilog != nullptr) {
            owner = scoped;
        }
    }
    if (!owner || owner->systemverilog == nullptr) {
        return { CompiledResolutionStatus::invalid, { } };
    }
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
        if (!import.member || import.member->spelling != member) {
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
        if (!import.wildcard) {
            continue;
        }
        auto tier = package_tier(import.package.spelling);
        wildcard_candidates.insert(wildcard_candidates.end(),
            tier.candidates.begin(), tier.candidates.end());
    }
    result = make_result(std::move(wildcard_candidates));
    if (result.status != CompiledResolutionStatus::not_found
        || !same_library_fallback) {
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

CompiledDeclarationResolution
CompiledDesignResolver::resolve_systemverilog_name(
    const sv::Name& name, const ScopeId use_scope,
    const CompiledDeclarationPredicate& predicate,
    const bool same_library_fallback) const
{
    if (design_ == nullptr || name.spelling.empty()) {
        return { CompiledResolutionStatus::invalid, { } };
    }
    const auto simple_name = name.spelling.find('.') == std::string::npos
        && name.spelling.find("::") == std::string::npos;
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
    if (name.selected && retained_is_valid(*name.selected)) {
        return { CompiledResolutionStatus::unique, { *name.selected } };
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
    for (const auto candidate : name.overloads) {
        if (retained_is_valid(candidate)) {
            retained.push_back(candidate);
        }
    }
    if (!retained.empty()) {
        return normalize_synthetic_duplicate(
            declaration_result(std::move(retained)));
    }
    return normalize_synthetic_duplicate(resolve_systemverilog(
        name.spelling, use_scope, predicate, same_library_fallback));
}

CompiledDeclarationResolution
CompiledDesignResolver::resolve_systemverilog(
    const std::string_view name, const ScopeId use_scope,
    const CompiledDeclarationPredicate& predicate,
    const bool same_library_fallback,
    const std::optional<ScopeId> stop_scope) const
{
    if (design_ == nullptr) {
        return { CompiledResolutionStatus::invalid, { } };
    }
    const auto separator = name.rfind("::");
    const auto member = separator == std::string_view::npos
        ? name
        : name.substr(separator + 2U);
    const auto package_name = separator == std::string_view::npos
        ? std::string_view { }
        : name.substr(0U, separator);
    if (member.empty() || (separator != std::string_view::npos
                              && package_name.empty())) {
        return { CompiledResolutionStatus::invalid, { } };
    }

    auto owner = design_->find_unit(selected_unit_);
    if (const auto* scope = find_scope(*design_, use_scope)) {
        if (const auto scoped = design_->find_unit(scope->unit);
            scoped && scoped->systemverilog != nullptr) {
            owner = scoped;
        }
    }
    if (!owner || owner->systemverilog == nullptr) {
        return { CompiledResolutionStatus::invalid, { } };
    }

    const auto declarations_in_scope = [&](const ScopeId scope) {
        std::vector<DeclarationId> candidates;
        visit_systemverilog_declarations_in_scope(
            *design_, effective_, scope,
            [&](const CompiledDeclarationView& view) {
                const auto& declaration = *view.systemverilog;
                if (declaration.name != member
                    || (predicate && !predicate(view))) {
                    return;
                }
                candidates.push_back(declaration.id);
            });
        return declaration_result(std::move(candidates));
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

    const auto package_tier = [&](const std::string_view requested) {
        return resolve_systemverilog_package_member(
            owner->systemverilog->library, requested, member, predicate);
    };
    if (!package_name.empty()) {
        auto result = package_tier(package_name);
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
    const CompiledDeclarationPredicate& predicate) const
{
    if (design_ == nullptr || receiver.empty() || member.empty()) {
        return { CompiledResolutionStatus::invalid, { } };
    }
    const auto interface_object = [](const CompiledDeclarationView& view) {
        if (view.systemverilog == nullptr) {
            return false;
        }
        const auto& declaration = *view.systemverilog;
        return !declaration.interface_type.empty()
            || (declaration.type
                && !declaration.type->interface_type.empty());
    };
    const auto receiver_id = resolve_systemverilog(
        receiver, use_scope, interface_object, false).unique();
    const auto receiver_view = receiver_id
        ? find_declaration(*design_, effective_, *receiver_id)
        : std::nullopt;
    if (!receiver_view || receiver_view->systemverilog == nullptr) {
        return { CompiledResolutionStatus::not_found, { } };
    }
    const auto& declaration = *receiver_view->systemverilog;
    const auto interface_name = !declaration.interface_type.empty()
        ? std::string_view { declaration.interface_type }
        : std::string_view { declaration.type->interface_type };
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
    const auto selected = design_->find_unit(
        UnitKind::systemverilog_interface, library, interface_name);
    if (!selected || selected->systemverilog == nullptr) {
        return { CompiledResolutionStatus::not_found, { } };
    }
    const auto& interface = *selected->systemverilog;
    if (!modport.empty()) {
        const auto view = std::ranges::find(
            interface.modports, modport, &sv::Modport::name);
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
    if (design_ == nullptr) {
        return { CompiledResolutionStatus::invalid, { } };
    }
    const auto separator = name.rfind("::");
    const auto member = separator == std::string_view::npos
        ? name
        : name.substr(separator + 2U);
    const auto package_name = separator == std::string_view::npos
        ? std::string_view { }
        : name.substr(0U, separator);
    if (member.empty() || (separator != std::string_view::npos
                              && package_name.empty())) {
        return { CompiledResolutionStatus::invalid, { } };
    }

    auto owner = design_->find_unit(selected_unit_);
    if (const auto* scope = find_scope(*design_, use_scope)) {
        if (const auto scoped = design_->find_unit(scope->unit);
            scoped && scoped->systemverilog != nullptr) {
            owner = scoped;
        }
    }
    if (!owner || owner->systemverilog == nullptr) {
        return { CompiledResolutionStatus::invalid, { } };
    }
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
    const bool same_library_fallback) const
{
    using Form = sv::DeclarationForm;
    const CompiledDeclarationPredicate constant = [](const auto& view) {
        return view.systemverilog != nullptr
            && (view.systemverilog->form == Form::parameter
                || view.systemverilog->form == Form::local_parameter
                || view.systemverilog->form == Form::enumeration_literal);
    };
    auto result = resolve_systemverilog(
        name, use_scope, constant, same_library_fallback);
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
    const bool same_library_fallback) const
{
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
        name, use_scope, named_type, same_library_fallback);
}

std::optional<TypeId> CompiledDesignResolver::resolve_vhdl_named_type(
    const std::string_view spelling,
    const std::optional<ScopeId> owner_scope) const
{
    if (design_ == nullptr) {
        return std::nullopt;
    }
    const auto simple_name = simple_vhdl_name(spelling);
    if (simple_name.empty()) {
        return std::nullopt;
    }
    const auto find_type = [&](const TypeId id) {
        return effective_ != nullptr
            ? effective_->find_type(id)
            : design_->find_type(id);
    };
    const auto last_colon = spelling.rfind(':');
    if (last_colon != std::string_view::npos && last_colon != 0U) {
        const auto offset_colon = spelling.rfind(':', last_colon - 1U);
        if (offset_colon != std::string_view::npos
            && offset_colon + 1U < last_colon) {
            const auto offset_spelling = spelling.substr(
                offset_colon + 1U, last_colon - offset_colon - 1U);
            std::uint64_t offset { };
            const auto parsed = std::from_chars(
                offset_spelling.data(),
                offset_spelling.data() + offset_spelling.size(), offset);
            if (parsed.ec == std::errc { }
                && parsed.ptr
                    == offset_spelling.data() + offset_spelling.size()) {
                const auto source_name = spelling.substr(0U, offset_colon);
                std::optional<TypeId> exact;
                for (const auto& stored : design_->vhdl_hir.types()) {
                    const auto type = find_type(stored.id);
                    if (!type || type->vhdl == nullptr
                        || !same_vhdl_identifier(
                            type->vhdl->name, simple_name)
                        || !type->vhdl->source.valid()
                        || type->vhdl->source.value()
                            >= design_->semantics.source_spans().size()) {
                        continue;
                    }
                    const auto& source = design_->semantics.source_spans()[
                        type->vhdl->source.value()];
                    if (source.logical_name != source_name
                        || source.begin.offset != offset) {
                        continue;
                    }
                    if (exact && *exact != type->vhdl->id) {
                        return std::nullopt;
                    }
                    exact = type->vhdl->id;
                }
                if (exact) {
                    return exact;
                }
            }
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
                template_name.canonical.empty()
                    ? std::string_view { template_name.spelling }
                    : std::string_view { template_name.canonical }) };
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
    for (const auto& stored : design_->vhdl_hir.types()) {
        const auto type = find_type(stored.id);
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
        if (score > selected_score) {
            selected = definition.id;
            selected_score = score;
            ambiguous = false;
        } else if (score == selected_score
            && selected != definition.id) {
            ambiguous = true;
        }
    }
    return ambiguous ? std::nullopt : selected;
}

std::optional<sv::TypeReference>
CompiledDesignResolver::effective_systemverilog_type(
    const sv::TypeReference& input, ScopeId use_scope) const
{
    if (design_ == nullptr) {
        return std::nullopt;
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
                const auto expression = effective_ != nullptr
                    ? effective_->find_expression(*binding->expression)
                    : design_->find_expression(*binding->expression);
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
            const auto type = effective_ != nullptr
                ? effective_->find_type(reference.target.target)
                : design_->find_type(reference.target.target);
            const auto fallback = type ? type
                                       : design_->find_type(
                                             reference.target.target);
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
        return resolve_systemverilog_named_type(
            reference.target.spelling, scope, false)
            .unique();
    };
    std::set<DeclarationId> visiting;
    const auto materialize = [&](const auto& self,
                                 sv::TypeReference candidate,
                                 const ScopeId scope)
        -> sv::TypeReference {
        const auto selected = type_declaration(candidate, scope);
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
                            std::move(resolved), candidate);
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
                        const auto definition = effective_ != nullptr
                            ? effective_->find_type(
                                  *nominal_type)
                            : design_->find_type(
                                  *nominal_type);
                        const auto fallback = definition
                            ? definition
                            : design_->find_type(
                                  *nominal_type);
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
        const auto type = effective_ != nullptr
            ? effective_->find_type(current.target.target)
            : design_->find_type(current.target.target);
        const auto definition = type
            ? type
            : design_->find_type(current.target.target);
        if (!definition || definition->systemverilog == nullptr) {
            break;
        }
        auto replacement = effective_systemverilog_type(
                               definition->systemverilog->base,
                               use_scope)
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
    const auto candidate = effective_ != nullptr
        ? effective_->find_expression(expression)
        : design_->find_expression(expression);
    if (!candidate || candidate->vhdl == nullptr
        || (candidate->vhdl->kind != vhdl::ExpressionKind::name
            && candidate->vhdl->kind
                != vhdl::ExpressionKind::logic_literal)) {
        return std::nullopt;
    }
    const auto spelling = std::string_view { candidate->vhdl->text };
    const auto matches = [&](const std::string_view literal) {
        return spelling.starts_with('\'')
            ? spelling == literal
            : same_vhdl_identifier(spelling, literal);
    };
    std::set<TypeId> visiting;
    const auto resolve = [&](const auto& self, const TypeId type_id)
        -> std::optional<std::int64_t> {
        if (!type_id.valid() || !visiting.insert(type_id).second) {
            return std::nullopt;
        }
        const auto type = effective_ != nullptr
            ? effective_->find_type(type_id)
            : design_->find_type(type_id);
        const auto fallback = type ? type : design_->find_type(type_id);
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
    if (design_ == nullptr) {
        return std::nullopt;
    }

    const auto make_boundary_binding = [](
        const std::span<const CompiledBindingFrame> active)
        -> SpecializedHirIntegralBinding {
        return [active](const DeclarationId formal)
            -> std::optional<ExpressionId> {
            for (auto frame = active.rbegin();
                 frame != active.rend(); ++frame) {
                const auto binding = std::ranges::find(
                    *frame, formal, &CompiledActualBinding::formal);
                if (binding == frame->end()) {
                    continue;
                }
                // The nearest frame owns the name even when it carries a
                // declaration/type binding rather than an expression.  Do
                // not expose an older frame through that shadowing boundary.
                return binding->expression;
            }
            return std::nullopt;
        };
    };
    const auto evaluate_boundary = [&](
        const std::optional<std::int64_t> folded,
        const std::optional<ExpressionId> residual,
        const std::span<const CompiledBindingFrame> active) {
        if (folded) {
            return folded;
        }
        return residual && effective_ != nullptr
            ? effective_->evaluate_integral_expression(
                  *residual, make_boundary_binding(active))
            : std::nullopt;
    };
    const auto materialize_boundaries = [&](
        vhdl::SubtypeIndication& subtype,
        const std::span<const CompiledBindingFrame> active) {
        for (auto& constraint : subtype.constraints) {
            if (!constraint.left && constraint.left_expression) {
                constraint.left = evaluate_boundary(
                    constraint.left, constraint.left_expression, active);
            }
            if (!constraint.right && constraint.right_expression) {
                constraint.right = evaluate_boundary(
                    constraint.right, constraint.right_expression, active);
            }
        }
    };
    const auto standard_width = [&]() -> std::uint8_t {
        const auto selected = design_->find_unit(selected_unit_);
        return selected && selected->vhdl != nullptr
                && selected->vhdl->standard == "2019"
            ? 64U
            : 32U;
    };
    const auto complete_predefined = [&](
        vhdl::SubtypeIndication& result,
        const std::span<const CompiledBindingFrame> active,
        const ScopeId subtype_scope) {
        materialize_boundaries(result, active);
        const auto name = simple_vhdl_name(result.type_mark.spelling);
        const auto stamp_builtin_vector_identity = [&]() {
            if (result.builtin_type != vhdl::BuiltinTypeIdentity::none
                || result.type_mark.target.valid()
                || result.domain != vhdl::ValueDomain::logic9) {
                return;
            }
            auto type_name = std::string_view {
                result.type_mark.spelling
            };
            constexpr auto builtin_prefix
                = std::string_view { "@builtin:" };
            if (type_name.starts_with(builtin_prefix)) {
                type_name.remove_prefix(builtin_prefix.size());
            }
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
            if (same_vhdl_identifier(name, "bit_vector")) {
                element_width = 1U;
                element_domain = vhdl::ValueDomain::bit2;
            } else if (same_vhdl_identifier(name, "boolean_vector")) {
                element_width = 1U;
                element_domain = vhdl::ValueDomain::boolean;
            } else if (same_vhdl_identifier(name, "string")) {
                element_width = 8U;
                element_domain = vhdl::ValueDomain::string;
            } else if (same_vhdl_identifier(name, "integer_vector")
                || same_vhdl_identifier(name, "time_vector")) {
                element_width = standard_width();
                element_domain = vhdl::ValueDomain::integer;
            } else if (same_vhdl_identifier(name, "std_logic_vector")
                || same_vhdl_identifier(name, "std_ulogic_vector")
                || same_vhdl_identifier(name, "signed")
                || same_vhdl_identifier(name, "unsigned")
                || same_vhdl_identifier(name, "ufixed")
                || same_vhdl_identifier(name, "sfixed")
                || same_vhdl_identifier(name, "unresolved_ufixed")
                || same_vhdl_identifier(name, "unresolved_sfixed")
                || same_vhdl_identifier(name, "float")
                || same_vhdl_identifier(name, "unresolved_float")
                || same_vhdl_identifier(name, "u_float")) {
                element_width = 1U;
                element_domain = vhdl::ValueDomain::logic9;
            }
            if (element_width == 0U) {
                return false;
            }
            if (result.domain == vhdl::ValueDomain::unknown) {
                result.domain = element_domain;
            }
            stamp_builtin_vector_identity();
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
                const auto null_range = constraint.null
                    || (constraint.descending && *left < *right)
                    || (!constraint.descending && *left > *right);
                const auto distance = *left >= *right
                    ? static_cast<std::uint64_t>(*left)
                        - static_cast<std::uint64_t>(*right)
                    : static_cast<std::uint64_t>(*right)
                        - static_cast<std::uint64_t>(*left);
                if (!null_range
                    && distance
                        == std::numeric_limits<std::uint64_t>::max()) {
                    result.executable_width.reset();
                    return true;
                }
                const auto dimension = null_range ? 0U : distance + 1U;
                if (dimension != 0U
                    && count
                        > std::numeric_limits<std::uint64_t>::max()
                            / dimension) {
                    result.executable_width.reset();
                    return true;
                }
                count *= dimension;
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
            || input.unspecified_class
                != vhdl::UnspecifiedTypeClass::none
            || !input.unspecified_component_classes.empty()
            || !input.unspecified_component_type_marks.empty()
            || input.unspecified_array_index_count != 0U
            || !input.unspecified_inference_identity.empty()
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
                                    const CompiledBindingFrame> active)
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
        for (auto frame = active.rbegin(); frame != active.rend(); ++frame) {
            const auto binding = std::ranges::find(
                *frame, formal, &CompiledActualBinding::formal);
            if (binding == frame->end()) {
                continue;
            }
            if (binding->vhdl_type) {
                return binding->vhdl_type;
            }
            if (binding->actual_declaration) {
                return declaration_type(*binding->actual_declaration);
            }
            if (binding->expression) {
                const auto resolution = CompiledDesignResolver {
                    *design_, selected_unit_, effective_, active
                }.resolve_expression_name(*binding->expression);
                if (const auto selected = resolution.unique()) {
                    return declaration_type(*selected);
                }
            }
            return std::nullopt;
        }
        if (effective_ == nullptr) {
            return std::nullopt;
        }
        const auto& actuals = effective_->specialization().actual_identities;
        const auto actual = std::ranges::find(
            actuals, formal, &SpecializedHirActualIdentity::declaration);
        if (actual == actuals.end()) {
            return std::nullopt;
        }
        if (actual->vhdl_type) {
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
        return std::nullopt;
    };

    std::function<std::optional<vhdl::SubtypeIndication>(
        vhdl::SubtypeIndication, ScopeId,
        std::vector<CompiledBindingFrame>&)> resolve;
    resolve = [&](vhdl::SubtypeIndication result, ScopeId scope,
                  std::vector<CompiledBindingFrame>& active_frames)
        -> std::optional<vhdl::SubtypeIndication> {
        materialize_boundaries(result, active_frames);
        const auto resolver = CompiledDesignResolver {
            *design_, selected_unit_, effective_, active_frames
        };
        if (result.type_mark.spelling.find('.') != std::string::npos) {
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
                const auto declaration = find_declaration(
                    *design_, effective_, package->member);
                if (declaration && declaration->vhdl != nullptr) {
                    auto selected = declaration->vhdl->subtype;
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
                        const bool append = !package->generic_bindings.empty()
                            && std::ranges::none_of(active_frames,
                                [&](const auto& frame) {
                                    return frame
                                        == package->generic_bindings;
                                });
                        if (append) {
                            active_frames.push_back(
                                package->generic_bindings);
                        }
                        auto effective = resolve(
                            *selected, declaration->vhdl->scope,
                            active_frames);
                        if (append) {
                            active_frames.pop_back();
                        }
                        return effective;
                    }
                }
            }
        }

        std::optional<DeclarationId> declaration_id;
        if (result.type_mark.target.valid()) {
            const auto type = effective_ != nullptr
                ? effective_->find_type(result.type_mark.target)
                : design_->find_type(result.type_mark.target);
            if (type && type->vhdl != nullptr) {
                declaration_id = type->vhdl->declaration;
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
        if (declaration_id) {
            const auto declaration = find_declaration(
                *design_, effective_, *declaration_id);
            if (declaration && declaration->vhdl != nullptr
                && declaration->vhdl->form
                    == vhdl::DeclarationForm::generic_type
                && active_generics.insert(*declaration_id).second) {
                auto replacement = bound_type(
                    *declaration_id, active_frames);
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
        const auto type = effective_ != nullptr
            ? effective_->find_type(result.type_mark.target)
            : design_->find_type(result.type_mark.target);
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
        if (definition.form == vhdl::TypeForm::subtype
            || definition.form == vhdl::TypeForm::alias
            || definition.form == vhdl::TypeForm::scalar) {
            auto inherited = definition.base;
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
            }
        }
        if (definition.form == vhdl::TypeForm::enumeration
            && !definition.enumeration_literals.empty()) {
            auto maximum = definition.enumeration_literals.size() - 1U;
            if (result.constraints.size() == 1U) {
                const auto& constraint = result.constraints.front();
                const auto left = evaluate_boundary(
                    constraint.left, constraint.left_expression,
                    active_frames);
                const auto right = evaluate_boundary(
                    constraint.right, constraint.right_expression,
                    active_frames);
                if (left && right && *left >= 0 && *right >= 0) {
                    maximum = static_cast<std::size_t>(
                        std::max(*left, *right));
                }
            }
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
        } else if (definition.form == vhdl::TypeForm::record) {
            std::uint64_t width { };
            auto domain = vhdl::ValueDomain::unknown;
            bool concrete = !definition.record_elements.empty();
            for (const auto& element : definition.record_elements) {
                const auto effective = resolve(
                    element.subtype, definition_scope, active_frames);
                if (!effective || !effective->executable_width
                    || *effective->executable_width == 0U
                    || width > std::numeric_limits<std::uint64_t>::max()
                            - *effective->executable_width) {
                    concrete = false;
                    break;
                }
                width += *effective->executable_width;
                if (domain == vhdl::ValueDomain::unknown) {
                    domain = effective->domain;
                } else if (domain != effective->domain) {
                    const auto rank = [](const vhdl::ValueDomain value) {
                        return value == vhdl::ValueDomain::logic9 ? 3
                            : value == vhdl::ValueDomain::logic4 ? 2
                            : value == vhdl::ValueDomain::unknown
                                || value == vhdl::ValueDomain::string
                            ? 0
                            : 1;
                    };
                    const auto merged = std::max(
                        rank(domain), rank(effective->domain));
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
                *definition.element_subtype, definition_scope,
                active_frames);
            if (definition.array_dimensions.size() == 1U
                && definition_declaration
                && definition_declaration->vhdl != nullptr
                && definition.element_subtype->domain
                    == vhdl::ValueDomain::logic9) {
                const auto& scopes = design_->semantics.scopes();
                const auto owner_scope = std::ranges::find(
                    scopes, definition_declaration->vhdl->scope,
                    &Scope::id);
                const auto owner = owner_scope != scopes.end()
                    ? design_->find_unit(owner_scope->unit)
                    : std::nullopt;
                if (owner && owner->vhdl != nullptr
                    && owner->vhdl->kind == vhdl::UnitKind::package
                    && owner->vhdl->primary_name.empty()
                    && same_vhdl_identifier(
                        owner->vhdl->library, "ieee")
                    && same_vhdl_identifier(
                        owner->vhdl->name, "std_logic_1164")) {
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
            std::uint64_t count { 1U };
            bool concrete = element && element->executable_width
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
            if (!result.constraints.empty()) {
                for (const auto& constraint : result.constraints) {
                    const auto left = evaluate_boundary(
                        constraint.left, constraint.left_expression,
                        active_frames);
                    const auto right = evaluate_boundary(
                        constraint.right, constraint.right_expression,
                        active_frames);
                    if (!left || !right) {
                        concrete = false;
                        break;
                    }
                    const auto null_range = constraint.null
                        || (constraint.descending && *left < *right)
                        || (!constraint.descending && *left > *right);
                    const auto distance = *left >= *right
                        ? static_cast<std::uint64_t>(*left)
                            - static_cast<std::uint64_t>(*right)
                        : static_cast<std::uint64_t>(*right)
                            - static_cast<std::uint64_t>(*left);
                    const auto dimension = null_range ? 0U : distance + 1U;
                    if ((!null_range && distance
                            == std::numeric_limits<std::uint64_t>::max())
                        || (dimension != 0U && count
                            > std::numeric_limits<std::uint64_t>::max()
                                / dimension)) {
                        concrete = false;
                        break;
                    }
                    count *= dimension;
                }
            } else {
                for (const auto& dimension : constraints) {
                    if (!dimension.constraint) {
                        concrete = false;
                        break;
                    }
                    const auto left = evaluate_boundary(
                        dimension.constraint->left,
                        dimension.constraint->left_expression,
                        active_frames);
                    const auto right = evaluate_boundary(
                        dimension.constraint->right,
                        dimension.constraint->right_expression,
                        active_frames);
                    if (!left || !right) {
                        concrete = false;
                        break;
                    }
                    const auto null_range = dimension.constraint->null
                        || (dimension.constraint->descending
                            && *left < *right)
                        || (!dimension.constraint->descending
                            && *left > *right);
                    const auto distance = *left >= *right
                        ? static_cast<std::uint64_t>(*left)
                            - static_cast<std::uint64_t>(*right)
                        : static_cast<std::uint64_t>(*right)
                            - static_cast<std::uint64_t>(*left);
                    const auto size = null_range ? 0U : distance + 1U;
                    if ((!null_range && distance
                            == std::numeric_limits<std::uint64_t>::max())
                        || (size != 0U && count
                            > std::numeric_limits<std::uint64_t>::max()
                                / size)) {
                        concrete = false;
                        break;
                    }
                    count *= size;
                }
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
            }
        }
        active_types.erase(result.type_mark.target);
        complete_predefined(result, active_frames, scope);
        return result;
    };
    return resolve(input, use_scope, frames);
}

std::optional<bool> CompiledDesignResolver::vhdl_array_shapes_match(
    const vhdl::SubtypeIndication& left, const ScopeId left_scope,
    const vhdl::SubtypeIndication& right, const ScopeId right_scope) const
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
            const auto view = effective_ != nullptr
                ? effective_->find_type(type)
                : design_->find_type(type);
            const auto fallback = view ? view : design_->find_type(type);
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
        if (left_dimension.descending != right_dimension.descending
            || left_dimension.null != right_dimension.null) {
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
        if (*left_first != *right_first || *left_last != *right_last) {
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
    const auto effective_left = effective_vhdl_subtype(
        left, left_scope).value_or(left);
    const auto effective_right = effective_vhdl_subtype(
        right, right_scope).value_or(right);
    if (effective_left.unspecified_class
        != vhdl::UnspecifiedTypeClass::none) {
        return vhdl_unspecified_profile_accepts(
            effective_left.unspecified_class,
            vhdl_profile_type_class(
                *design_, effective_, effective_right));
    }
    if (effective_right.unspecified_class
        != vhdl::UnspecifiedTypeClass::none) {
        return vhdl_unspecified_profile_accepts(
            effective_right.unspecified_class,
            vhdl_profile_type_class(
                *design_, effective_, effective_left));
    }
    if (!same_vhdl_subtype(effective_left, effective_right)) {
        return false;
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
            const auto name = vhdl_spelling(*association.formal);
            for (std::size_t index { }; index < formals.size(); ++index) {
                const auto declaration = find_declaration(
                    *design_, effective_, formals[index]);
                if (declaration && declaration->vhdl != nullptr
                    && same_vhdl_identifier(
                        declaration->vhdl->name, name)) {
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
        if (declaration->vhdl->form
                == vhdl::DeclarationForm::generic_type
            && !binding.vhdl_type && binding.expression) {
            const auto expression = effective_ != nullptr
                ? effective_->find_expression(*binding.expression)
                : design_->find_expression(*binding.expression);
            if (expression && expression->vhdl != nullptr
                && expression->vhdl->kind
                    == vhdl::ExpressionKind::name) {
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
                const auto resolved = resolve_vhdl_default_callable(
                    formals[index],
                    use_scope.valid()
                        ? use_scope
                        : declaration->vhdl->scope,
                    declaration->vhdl->callable->default_callable
                        ? &*declaration->vhdl->callable->default_callable
                        : nullptr);
                const auto selected = resolved.unique();
                if (!selected) {
                    return std::nullopt;
                }
                binding.actual_declaration = selected->key;
            } else if (form != vhdl::DeclarationForm::generic_function
                && form != vhdl::DeclarationForm::generic_procedure
                && form != vhdl::DeclarationForm::generic_package) {
                return std::nullopt;
            }
        }
    }
    return result;
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
        if (const auto indexed = design_->vhdl_package_members(member)) {
            for (const auto& entry : *indexed) {
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
                const auto type = effective_ != nullptr
                    ? effective_->find_type(
                          *declaration->vhdl->declared_type)
                    : design_->find_type(
                          *declaration->vhdl->declared_type);
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
        const auto instance = find_declaration(
            *design_, effective_, instance_id);
        if (!instance || instance->vhdl == nullptr
            || instance->vhdl->form
                != vhdl::DeclarationForm::package_instance
            || !instance->vhdl->package) {
            return;
        }
        const auto& profile = *instance->vhdl->package;
        auto template_name = vhdl_spelling(profile.template_name);
        auto library = normalized_library(owner->library);
        if (const auto separator = template_name.rfind('.');
            separator != std::string_view::npos) {
            library = effective_vhdl_library(
                template_name.substr(0U, separator),
                owner->library);
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
                    && vhdl_actual_form(declaration->vhdl->form)) {
                    formals.push_back(id);
                }
            }
            const auto bindings = bind_vhdl_generics(
                formals, profile.generic_map,
                instance->vhdl->scope);
            if (!bindings) {
                continue;
            }
            append_template(library, template_name, member,
                instance_id, &*bindings);
        }
    };

    if (parts.size() == 2U) {
        auto scope = use_scope;
        for (std::size_t depth { };
             scope.valid() && depth <= design_->semantics.scopes().size();
             ++depth) {
            std::vector<DeclarationId> instances;
            visit_vhdl_declarations_in_scope(*design_, effective_, scope,
                [&](const vhdl::Declaration& declaration) {
                    if (declaration.form
                            == vhdl::DeclarationForm::package_instance
                        && same_vhdl_identifier(
                            declaration.name, parts.front())) {
                        instances.push_back(declaration.id);
                    }
                });
            if (!instances.empty()) {
                sort_unique(instances);
                for (const auto instance : instances) {
                    append_instance(instance, parts.back());
                }
                break;
            }
            const auto* record = find_scope(*design_, scope);
            if (record == nullptr || !record->parent) {
                break;
            }
            scope = *record->parent;
        }

        const auto consider_binding = [&](const CompiledActualBinding& binding) {
            auto bound_declaration = binding.actual_declaration;
            if (!bound_declaration) {
                bound_declaration = actual_declaration(binding.formal);
            }
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

    if (parts.size() == 2U || parts.size() == 3U) {
        const auto qualified = parts.size() == 3U;
        const auto library = qualified
            ? effective_vhdl_library(parts.front(), owner->library)
            : normalized_library(owner->library);
        append_template(library, qualified ? parts[1] : parts[0],
            qualified ? parts[2] : parts[1], std::nullopt, nullptr);
        return { status_for(candidates.size()), std::move(candidates) };
    }

    std::unordered_set<std::uint32_t> active_contexts;
    const auto visit_context = [&](const auto& self,
                                   const vhdl::Unit& context_owner) -> void {
        if (!active_contexts.insert(context_owner.id.value()).second) {
            return;
        }
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
                    if (!same_vhdl_identifier(
                            selected_parts.back(), "all")
                        && !same_vhdl_identifier(
                            selected_parts.back(), parts.back())) {
                        continue;
                    }
                    const auto library = selected_parts.size() == 3U
                        ? effective_vhdl_library(selected_parts.front(),
                              context_owner.library)
                        : normalized_library(context_owner.library);
                    append_template(library,
                        selected_parts[package_index], parts.back(),
                        std::nullopt, nullptr);
                    continue;
                }
                if (item.kind != vhdl::ContextKind::context_reference
                    || selected_parts.empty()) {
                    continue;
                }
                const auto library = selected_parts.size() >= 2U
                    ? effective_vhdl_library(
                          selected_parts[selected_parts.size() - 2U],
                          context_owner.library)
                    : normalized_library(context_owner.library);
                for (const auto& context : design_->vhdl_units()) {
                    if (context.kind == vhdl::UnitKind::context
                        && same_vhdl_identifier(
                            normalized_library(context.library), library)
                        && same_vhdl_identifier(
                            context.name, selected_parts.back())) {
                        self(self, context);
                    }
                }
            }
        }
        active_contexts.erase(context_owner.id.value());
    };
    for (const auto* context_owner : lookup_units) {
        visit_context(visit_context, *context_owner);
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
                if (item.kind != vhdl::ContextKind::context_reference
                    || parts.empty()) {
                    continue;
                }
                const auto library = parts.size() >= 2U
                    ? effective_vhdl_library(
                          parts[parts.size() - 2U], owner.library)
                    : normalized_library(owner.library);
                for (const auto& context : design_->vhdl_units()) {
                    if (context.kind == vhdl::UnitKind::context
                        && same_vhdl_identifier(
                            normalized_library(context.library), library)
                        && same_vhdl_identifier(
                            context.name, parts.back())
                        && self(self, context)) {
                        active_contexts.erase(owner.id.value());
                        return true;
                    }
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
    if (parts.size() >= 2U) {
        const auto qualified = parts.size() == 3U;
        const auto library = qualified
            ? effective_vhdl_library(
                  parts.front(), owner->library)
            : normalized_library(owner->library);
        return exported(library,
            qualified ? parts[1] : parts[0],
            qualified ? parts[2] : parts[1]);
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
                    && !selected_parts.empty()) {
                    const auto library = selected_parts.size() >= 2U
                        ? effective_vhdl_library(
                              selected_parts[selected_parts.size() - 2U],
                              context_owner.library)
                        : normalized_library(context_owner.library);
                    for (const auto& context : design_->vhdl_units()) {
                        if (context.kind == vhdl::UnitKind::context
                            && same_vhdl_identifier(
                                context.library, library)
                            && same_vhdl_identifier(
                                context.name, selected_parts.back())
                            && self(self, context)) {
                            return true;
                        }
                    }
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
    constexpr auto prefix = std::string_view { "@builtin:" };
    if (spelling.starts_with(prefix)) {
        spelling.remove_prefix(prefix.size());
    }
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
    return visit_vhdl_linked_imports(*design_, lookup_units,
        [&](const CompiledVhdlImport& imported) {
            return same_vhdl_identifier(imported.library, library)
                && same_vhdl_identifier(imported.package, package)
                && (same_vhdl_identifier(imported.member, "all")
                    || same_vhdl_identifier(imported.member, member));
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

    if (parts.size() > 1U) {
        const auto packages = resolve_vhdl_package_members(
            name, use_scope, predicate);
        std::vector<DeclarationId> declarations;
        declarations.reserve(packages.candidates.size());
        for (const auto& candidate : packages.candidates) {
            declarations.push_back(candidate.member);
        }
        return declaration_result(std::move(declarations));
    }

    const auto in_scope = [&](const ScopeId scope) {
        std::vector<DeclarationId> candidates;
        visit_vhdl_declarations_in_scope(*design_, effective_, scope,
            [&](const vhdl::Declaration& declaration) {
                const auto view = find_declaration(
                    *design_, effective_, declaration.id);
                if (same_vhdl_identifier(
                        declaration.name, parts.back())
                    && view && (!predicate || predicate(*view))) {
                    candidates.push_back(declaration.id);
                }
            });
        return declaration_result(std::move(candidates));
    };
    auto scope = use_scope;
    const Scope* root { };
    for (std::size_t depth { };
         scope.valid() && depth <= design_->semantics.scopes().size();
         ++depth) {
        const auto tier = in_scope(scope);
        if (tier.status != CompiledResolutionStatus::not_found) {
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
    for (const auto& frame : binding_frames_) {
        for (const auto& binding : frame) {
            append_formal(binding.formal);
        }
    }
    if (effective_ != nullptr) {
        for (const auto& actual :
            effective_->specialization().actual_identities) {
            append_formal(actual.declaration);
        }
    }
    auto tier = declaration_result(std::move(overlay_formals));
    if (tier.status != CompiledResolutionStatus::not_found) {
        return tier;
    }

    if (root != nullptr) {
        const auto root_unit = design_->find_unit(root->unit);
        if (root_unit && root_unit->vhdl != nullptr) {
            if (const auto entity = primary_vhdl_unit(
                    *design_, *root_unit->vhdl)) {
                tier = in_scope((**entity).scope);
                if (tier.status != CompiledResolutionStatus::not_found) {
                    return tier;
                }
            }
        }
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
    // Visibility is determined at the use site.  Retained frontend
    // identities are a fallback inside resolve_vhdl(), not a tier above
    // lexical declarations: otherwise an imported homograph retained on an
    // occurrence defeats a declaration in the nearest enclosing scope.
    // Resolving the spelling first also preserves complete overload sets when
    // `selected` is an indexing interpretation rather than a callable.
    return resolve_vhdl(name, use_scope, callable);
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
        // resolve_vhdl() already applies lexical-tier hiding.  If its visible
        // tier contains a declaration that did not come from an imported
        // package, that local homograph hides every imported declaration with
        // the same designator.  Do not reintroduce those imports while
        // expanding declarations into callable bodies below.
        if (std::ranges::any_of(visible.candidates,
                [&](const DeclarationId declaration) {
                    return !imported(declaration);
                })) {
            package_members.candidates.clear();
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
        const auto view = effective_ != nullptr
            ? effective_->find_expression(expression)
            : design_->find_expression(expression);
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
            return binding->actual_declaration
                ? binding->actual_declaration
                : binding->expression
                ? referenced_declaration(*binding->expression)
                : std::nullopt;
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
            const auto& instance = *declaration->vhdl->package;
            std::vector<DeclarationId> templates;
            if (instance.template_name.selected) {
                templates.push_back(*instance.template_name.selected);
            } else {
                for (const auto& candidate :
                    design_->vhdl_hir.declarations()) {
                    const auto template_form
                        = candidate.form
                            == vhdl::DeclarationForm::generic_function_template
                        || candidate.form
                            == vhdl::DeclarationForm::generic_procedure_template;
                    if (template_form && same_vhdl_identifier(candidate.name,
                            vhdl_spelling(instance.template_name))) {
                        templates.push_back(candidate.id);
                    }
                }
            }
            sort_unique(templates);
            std::vector<CompiledVhdlCallableResolution> results;
            for (const auto template_id : templates) {
                auto template_view = find_declaration(
                    *design_, effective_, template_id);
                if (!template_view || template_view->vhdl == nullptr) {
                    continue;
                }
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
                    if (same_unit_family) {
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
                        if (vhdl_actual_form(record->vhdl->form)) {
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
    } else {
        std::vector<std::string> designators;
        for (const auto& declaration : design_->vhdl_hir.declarations()) {
            if (vhdl_callable_form(declaration.form)
                && declaration.callable) {
                designators.push_back(declaration.name);
            }
        }
        std::ranges::sort(designators, [](const std::string& left,
                                          const std::string& right) {
            if (same_vhdl_identifier(left, right)) {
                return false;
            }
            return left < right;
        });
        const auto duplicate = std::ranges::unique(
            designators, same_vhdl_identifier);
        designators.erase(duplicate.begin(), duplicate.end());
        for (const auto& designator : designators) {
            vhdl::Name candidate_name;
            candidate_name.spelling = designator;
            candidate_name.canonical = designator;
            append(candidate_name);
        }
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
            && vhdl_actual_form(declaration->vhdl->form)) {
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
        return effective_ != nullptr ? effective_->find_expression(id)
                                     : design_->find_expression(id);
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
        const auto expected_value = effective_
                && expected_binding.expression
            ? effective_->evaluate_integral_expression(
                  *expected_binding.expression)
            : std::nullopt;
        const auto actual_value = effective_
                && actual_binding.expression
            ? effective_->evaluate_integral_expression(
                  *actual_binding.expression)
            : std::nullopt;
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
                    effective_package_subtype(
                        *expected_type, left->vhdl->scope),
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

    const auto view = effective_ != nullptr
        ? effective_->find_expression(expression)
        : design_->find_expression(expression);
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
    const auto view = effective_ != nullptr
        ? effective_->find_expression(expression)
        : design_->find_expression(expression);
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
    if (!simple_systemverilog_identifier(name)
        || (!package.empty()
            && !simple_systemverilog_identifier(package))) {
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
              while (!remaining.empty()) {
                  if (!type.target.target.valid()) {
                      return false;
                  }
                  const auto definition = effective_ != nullptr
                      ? effective_->find_type(type.target.target)
                      : design_->find_type(type.target.target);
                  if (!definition
                      || definition->systemverilog == nullptr) {
                      return false;
                  }
                  const auto& record = *definition->systemverilog;
                  if (record.form != sv::TypeForm::packed_structure
                      && record.form != sv::TypeForm::packed_union
                      && record.form != sv::TypeForm::tagged_union) {
                      return false;
                  }
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
        const auto view = effective_ != nullptr
            ? effective_->find_expression(expression)
            : design_->find_expression(expression);
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
