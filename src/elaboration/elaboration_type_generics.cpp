// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

#include <sstream>

namespace fsim::elaboration::elaboration_detail {
namespace {

frontend::Type builtin_vhdl_type(const std::string_view name) {
    frontend::Type type;
    type.spelling = std::string{name};
    if (name == "bit" || name == "bit_vector") {
        type.domain = frontend::ValueDomain::Bit2;
    } else if (
        name == "std_logic" || name == "std_logic_vector"
        || name == "std_ulogic" || name == "std_ulogic_vector"
        || name == "signed" || name == "unsigned") {
        type.domain = frontend::ValueDomain::Logic9;
        type.is_signed = name == "signed";
    } else if (name == "boolean") {
        type.domain = frontend::ValueDomain::Boolean;
    } else if (
        name == "integer" || name == "natural"
        || name == "positive") {
        type.domain = frontend::ValueDomain::Integer;
        type.is_signed = true;
        constexpr auto first =
            std::int64_t{std::numeric_limits<std::int32_t>::min()};
        constexpr auto last =
            std::int64_t{std::numeric_limits<std::int32_t>::max()};
        type.integer_range = frontend::IntegerRange{
            name == "integer" ? first : name == "natural" ? 0 : 1,
            last,
            false};
    }
    return type;
}

std::string canonical_type_identity(const frontend::Type& type) {
    std::ostringstream output;
    output << "vhdl-type-v1;domain="
           << static_cast<unsigned>(type.domain)
           << ";spelling=" << type.spelling
           << ";nominal=" << type.nominal_type
           << ";signed=" << (type.is_signed ? 1 : 0);
    if (type.packed_range) {
        output << ";packed=" << type.packed_range->left << ':'
               << type.packed_range->right << ':'
               << (type.packed_range->descending ? 1 : 0);
    }
    if (type.integer_range) {
        output << ";integer=" << type.integer_range->left << ':'
               << type.integer_range->right << ':'
               << (type.integer_range->descending ? 1 : 0);
    }
    if (type.enumeration_range) {
        output << ";enum-range=" << type.enumeration_range->left << ':'
               << type.enumeration_range->right << ':'
               << (type.enumeration_range->descending ? 1 : 0);
    }
    for (const auto& literal : type.enumeration_literals) {
        output << ";literal=" << literal;
    }
    output << ";aggregate="
           << static_cast<unsigned>(type.packed_aggregate);
    for (const auto& member : type.packed_members) {
        output << ";member=" << member.name << ':'
               << static_cast<unsigned>(member.domain) << ':'
               << member.spelling << ':'
               << (member.is_signed ? 1 : 0) << ':'
               << member.lsb_offset;
        if (member.packed_range) {
            output << ':' << member.packed_range->left << ':'
                   << member.packed_range->right << ':'
                   << (member.packed_range->descending ? 1 : 0);
        }
    }
    if (type.vhdl_array) {
        output << ";array=" << type.vhdl_array->index_subtype << ':'
               << type.vhdl_array->element_spelling << ':'
               << type.vhdl_array->element_named_type << ':'
               << static_cast<unsigned>(
                      type.vhdl_array->element_domain)
               << ':' << (type.vhdl_array->unconstrained ? 1 : 0);
        if (type.vhdl_array->index_base_range) {
            output << ':' << type.vhdl_array->index_base_range->left
                   << ':' << type.vhdl_array->index_base_range->right
                   << ':'
                   << (type.vhdl_array->index_base_range->descending
                           ? 1
                           : 0);
        }
    }
    return output.str();
}

std::optional<frontend::Type> resolve_type_mark(
    const frontend::Expression& expression,
    const NamedTypeEnvironment& parent_types) {
    if (expression.kind != frontend::ExpressionKind::Identifier
        || !expression.operands.empty()) {
        return std::nullopt;
    }
    auto builtin = builtin_vhdl_type(expression.text);
    if (builtin.domain != frontend::ValueDomain::Unknown) {
        return builtin;
    }
    const auto found = parent_types.find(expression.text);
    if (found == parent_types.end()
        || found->second.interface_formal) {
        return std::nullopt;
    }
    return found->second.type;
}

} // namespace

NamedTypeEnvironment local_vhdl_type_environment(
    const DesignUnit& unit) {
    NamedTypeEnvironment result;
    for (const auto& alias : unit.type_aliases) {
        result.insert_or_assign(
            alias.name,
            NamedTypeBinding{
                alias.type,
                (unit.library.empty() ? std::string{"work"} : unit.library)
                    + "." + unit.name,
                false});
    }
    return result;
}

InterfaceTypeSpecialization specialize_vhdl_interface_types(
    const DesignUnit& source,
    const std::vector<frontend::ParameterOverride>& overrides,
    const NamedTypeEnvironment& parent_types,
    const frontend::Language association_language,
    std::vector<Diagnostic>& diagnostics) {
    InterfaceTypeSpecialization result;
    result.unit = source;
    const bool has_type_formals = std::ranges::any_of(
        source.parameters,
        [](const auto& parameter) {
            return !parameter.local
                && parameter.kind
                    == frontend::ParameterKind::Type;
        });
    if (!has_type_formals) {
        result.value_overrides = overrides;
        return result;
    }
    result.applied = true;

    std::vector<const frontend::ParameterDeclaration*> formals;
    for (const auto& parameter : source.parameters) {
        if (!parameter.local) {
            formals.push_back(&parameter);
        }
    }
    std::vector<std::optional<frontend::ParameterOverride>> actuals(
        formals.size());
    std::size_t next_positional = 0;
    bool saw_named = false;
    for (const auto& actual : overrides) {
        std::optional<std::size_t> index;
        if (actual.name) {
            saw_named = true;
            const auto found = std::ranges::find_if(
                formals,
                [&](const auto* formal) {
                    return parameter_name_matches(
                        formal->name,
                        *actual.name,
                        source.language,
                        association_language);
                });
            if (found == formals.end()) {
                diagnostics.push_back({
                    "FSIM-ELAB-GENERIC-001",
                    "unknown generic actual '" + *actual.name + "'",
                    actual.span});
                continue;
            }
            index = static_cast<std::size_t>(
                std::distance(formals.begin(), found));
        } else {
            if (saw_named) {
                diagnostics.push_back({
                    "FSIM-ELAB-GENERIC-003",
                    "a positional generic actual cannot follow a named "
                    "actual",
                    actual.span});
            }
            if (next_positional >= formals.size()) {
                diagnostics.push_back({
                    "FSIM-ELAB-GENERIC-001",
                    "too many positional generic actuals",
                    actual.span});
                continue;
            }
            index = next_positional++;
        }
        if (actuals[*index]) {
            diagnostics.push_back({
                "FSIM-ELAB-GENERIC-002",
                "duplicate generic actual for '"
                    + formals[*index]->name + "'",
                actual.span});
        } else {
            actuals[*index] = actual;
        }
    }

    for (std::size_t index = 0; index < formals.size(); ++index) {
        const auto& formal = *formals[index];
        if (formal.kind == frontend::ParameterKind::Value) {
            if (actuals[index]) {
                auto value_actual = std::move(*actuals[index]);
                value_actual.name = formal.name;
                result.value_overrides.push_back(
                    std::move(value_actual));
            }
            continue;
        }
        if (!actuals[index]) {
            diagnostics.push_back({
                "FSIM-ELAB-GENTYPE-001",
                "interface type generic '" + formal.name
                    + "' requires an actual type mark",
                formal.span});
            continue;
        }
        if (association_language
            != frontend::Language::Vhdl2008) {
            diagnostics.push_back({
                "FSIM-ELAB-GENTYPE-004",
                "interface type generic '" + formal.name
                    + "' requires a same-language VHDL type-mark actual",
                actuals[index]->span});
            continue;
        }
        const auto& expression = actuals[index]->value;
        if (expression.kind
                != frontend::ExpressionKind::Identifier
            || !expression.operands.empty()) {
            diagnostics.push_back({
                "FSIM-ELAB-GENTYPE-002",
                "actual for interface type generic '" + formal.name
                    + "' must be a type mark",
                actuals[index]->span});
            continue;
        }
        const auto actual_type =
            resolve_type_mark(expression, parent_types);
        if (!actual_type) {
            diagnostics.push_back({
                "FSIM-ELAB-GENTYPE-003",
                "VHDL type mark '" + expression.text
                    + "' is not visible at this generic association",
                actuals[index]->span});
            continue;
        }
        result.unit.type_aliases.push_back(
            frontend::TypeAliasDeclaration{
                formal.name,
                *actual_type,
                formal.span,
                {},
                frontend::TypeDeclarationKind::Alias});
        result.values.emplace_back(
            formal.name,
            canonical_type_identity(*actual_type));
    }

    std::erase_if(
        result.unit.parameters,
        [](const auto& parameter) {
            return parameter.kind == frontend::ParameterKind::Type;
        });
    return result;
}

} // namespace fsim::elaboration::elaboration_detail
