// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;

    void HierarchyBuilder::resolve_named_types(
        DesignUnit& unit,
        const NamedTypeEnvironment& imported_types,
        const bool vhdl,
        const bool resolve_ports) {
        std::unordered_map<std::string, std::size_t> local_types;
        const auto owner =
            (unit.library.empty() ? std::string{"work"} : unit.library)
            + "." + unit.name;
        for (std::size_t index = 0;
             index < unit.type_aliases.size(); ++index) {
            auto& alias = unit.type_aliases[index];
            if (!vhdl
                && alias.type.nominal_type.empty()
                && (alias.type.packed_aggregate
                        != frontend::PackedAggregateKind::None
                    || !alias.enum_literals.empty())) {
                alias.type.nominal_type =
                    "sv:" + owner + "." + alias.name;
            }
            local_types.emplace(
                alias.name, index);
        }
        std::vector<unsigned char> states(
            unit.type_aliases.size(), 0);
        std::unordered_map<std::string, frontend::Type*>
            generated_types;
        std::unordered_set<std::string> active_interface_type_formals;
        std::unordered_set<std::string> active_interface_package_formals;
        std::function<bool(frontend::Type&)> resolve_type;
        std::function<bool(std::size_t)> resolve_alias;
        const auto simple_type_name =
            [](const std::string_view spelling) {
                const auto separator =
                    spelling.find_last_of('.');
                return std::string{
                    spelling.substr(
                        separator == std::string_view::npos
                            ? 0
                            : separator + 1)};
            };
        const auto is_packed_array_type =
            [&](const frontend::Type& type) {
                const auto name =
                    simple_type_name(type.spelling);
                return type.vhdl_array.has_value()
                    || name == "bit_vector"
                    || name == "std_logic_vector"
                    || name == "std_ulogic_vector"
                    || name == "signed"
                    || name == "unsigned"
                    || name == "ufixed" || name == "sfixed"
                    || name == "unresolved_ufixed"
                    || name == "unresolved_sfixed"
                    || name == "float" || name == "unresolved_float"
                    || name == "u_float";
            };
        const auto constraint_span =
            [](const frontend::Type& type) {
                if (!type.vhdl_array_constraints.empty()) {
                    return type.vhdl_array_constraints.front().span;
                }
                if (type.discrete_range_expression) {
                    return type.discrete_range_expression->span;
                }
                if (type.enumeration_range_expression) {
                    return type.enumeration_range_expression->span;
                }
                if (type.integer_range_expression) {
                    return type.integer_range_expression->span;
                }
                if (type.packed_range_expression) {
                    return type.packed_range_expression->span;
                }
                return type.named_type_span;
            };
        const auto validate_direct_constraints =
            [&](const frontend::Type& type) {
                bool valid = true;
                if (type.discrete_range_expression) {
                    report(
                        "FSIM-ELAB-VHSUBTYPE-001",
                        "a VHDL discrete range constraint requires a "
                        "named integer-family or enumeration base subtype",
                        constraint_span(type));
                    valid = false;
                }
                if (type.integer_range_expression
                    && type.domain
                        != frontend::ValueDomain::Integer) {
                    report(
                        "FSIM-ELAB-VHSUBTYPE-001",
                        "a VHDL range constraint requires an "
                        "integer-family base subtype",
                        constraint_span(type));
                    valid = false;
                }
                if (type.packed_range_expression
                    && (!is_packed_array_type(type)
                        || !type.packed_members.empty())) {
                    report(
                        "FSIM-ELAB-VHSUBTYPE-003",
                        "a VHDL packed index constraint requires an "
                        "unconstrained one-dimensional packed-array base",
                        constraint_span(type));
                    valid = false;
                }
                return valid;
            };
        const auto apply_derived_constraints =
            [&](frontend::Type base,
                const frontend::Type& derived)
                -> std::optional<frontend::Type> {
                if (!derived.vhdl_type_declaration.empty()) {
                    base.vhdl_type_declaration =
                        derived.vhdl_type_declaration;
                }
                if (!derived.vhdl_resolution_function.empty()) {
                    base.vhdl_resolution_function =
                        derived.vhdl_resolution_function;
                }
                const bool has_integer_constraint =
                    derived.integer_range_expression.has_value();
                const bool has_discrete_constraint =
                    derived.discrete_range_expression.has_value();
                const bool has_packed_constraint =
                    derived.packed_range_expression.has_value()
                    && !(base.vhdl_array
                         && !derived.vhdl_array_constraints.empty());
                const bool has_array_constraints =
                    base.vhdl_array
                    && !derived.vhdl_array_constraints.empty();
                if (has_integer_constraint) {
                    if (base.domain
                        != frontend::ValueDomain::Integer) {
                        report(
                            "FSIM-ELAB-VHSUBTYPE-001",
                            "a derived VHDL range constraint requires an "
                            "integer-family base subtype",
                            constraint_span(derived));
                        return std::nullopt;
                    }
                    base.integer_base_range =
                        base.integer_range;
                    base.integer_base_range_expression =
                        base.integer_range_expression;
                    base.integer_range =
                        derived.integer_range;
                    base.integer_range_expression =
                        derived.integer_range_expression;
                }
                if (has_discrete_constraint) {
                    const auto& range =
                        *derived.discrete_range_expression;
                    if (base.domain
                        == frontend::ValueDomain::Integer) {
                        base.integer_base_range =
                            base.integer_range;
                        base.integer_base_range_expression =
                            base.integer_range_expression;
                        base.integer_range.reset();
                        base.integer_range_expression =
                            frontend::IntegerRangeExpression{
                                range.left,
                                range.right,
                                range.span,
                                range.descending};
                    } else if (
                        !base.enumeration_literals.empty()) {
                        base.enumeration_base_range =
                            base.enumeration_range;
                        base.enumeration_base_range_expression =
                            base.enumeration_range_expression;
                        base.enumeration_range.reset();
                        base.enumeration_range_expression =
                            range;
                    } else {
                        report(
                            "FSIM-ELAB-VHSUBTYPE-001",
                            "a derived VHDL discrete range constraint "
                            "requires an integer-family or enumeration "
                            "base subtype",
                            constraint_span(derived));
                        return std::nullopt;
                    }
                    base.discrete_range_expression.reset();
                }
                if (has_array_constraints) {
                    auto& array = *base.vhdl_array;
                    if (derived.vhdl_array_constraints.size()
                        != array.dimensions.size()) {
                        report(
                            "FSIM-ELAB-VHARRAY-008",
                            "a VHDL array subtype constraint has "
                            + std::to_string(
                                derived.vhdl_array_constraints.size())
                            + " dimensions but its base type has "
                            + std::to_string(array.dimensions.size()),
                            constraint_span(derived));
                        return std::nullopt;
                    }
                    for (std::size_t index = 0;
                         index < array.dimensions.size(); ++index) {
                        auto& dimension = array.dimensions[index];
                        if (!dimension.unconstrained
                            || dimension.constraint
                            || dimension.range) {
                            report(
                                "FSIM-ELAB-VHSUBTYPE-004",
                                "a constrained VHDL array dimension cannot "
                                "be constrained again",
                                derived.vhdl_array_constraints[index].span);
                            return std::nullopt;
                        }
                        dimension.constraint =
                            derived.vhdl_array_constraints[index];
                        dimension.range.reset();
                        dimension.null = false;
                        dimension.stride = 0;
                        dimension.unconstrained = false;
                    }
                    array.unconstrained = false;
                    array.flat_width.reset();
                    base.packed_range.reset();
                    base.packed_range_expression.reset();
                    if (array.dimensions.size() == 1) {
                        const auto& constraint =
                            derived.vhdl_array_constraints.front();
                        base.packed_range_expression =
                            frontend::PackedRangeExpression{
                                constraint.left,
                                constraint.right,
                                constraint.span,
                                constraint.descending};
                    }
                    base.vhdl_array_constraints.clear();
                }
                if (has_packed_constraint) {
                    if (!is_packed_array_type(base)
                        || !base.packed_members.empty()) {
                        report(
                            "FSIM-ELAB-VHSUBTYPE-003",
                            "a derived VHDL packed index constraint "
                            "requires an unconstrained one-dimensional "
                            "packed-array base",
                            constraint_span(derived));
                        return std::nullopt;
                    }
                    if (base.packed_range
                        || base.packed_range_expression) {
                        report(
                            "FSIM-ELAB-VHSUBTYPE-004",
                            "a constrained VHDL packed-array subtype cannot "
                            "be constrained again",
                            constraint_span(derived));
                        return std::nullopt;
                    }
                    base.packed_range =
                        derived.packed_range;
                    base.packed_range_expression =
                        derived.packed_range_expression;
                    if (base.vhdl_array) {
                        base.vhdl_array->unconstrained = false;
                    }
                }
                return base;
            };
        resolve_alias = [&](const std::size_t index) {
            if (states[index] == 2) {
                return true;
            }
            if (states[index] == 3) {
                return false;
            }
            if (states[index] == 1) {
                const bool access_cycle =
                    unit.type_aliases[index].type.vhdl_access
                    != std::nullopt;
                report(
                    access_cycle
                        ? "FSIM-ELAB-VHACCESS-001"
                    : vhdl
                        ? "FSIM-ELAB-VHTYPE-002"
                        : "FSIM-ELAB-SVTYPE-003",
                    std::string{
                        access_cycle
                            ? "cyclic VHDL access designated subtype involving '"
                        : vhdl
                            ? "cyclic VHDL type declaration involving '"
                            : "cyclic SystemVerilog typedef involving '"}
                        + unit.type_aliases[index].name + "'",
                    unit.type_aliases[index].span);
                return false;
            }
            states[index] = 1;
            const bool resolved =
                resolve_type(unit.type_aliases[index].type);
            states[index] = resolved ? 2 : 3;
            return resolved;
        };
        resolve_type = [&](frontend::Type& type) {
            if (!type.systemverilog_class_declaration.empty()) {
                return true;
            }
            for (auto& member : type.packed_members) {
                if (member.nested_types.empty()) {
                    continue;
                }
                auto& nested = member.nested_types.front();
                if (!resolve_type(nested)) {
                    return false;
                }
                member.domain = nested.domain;
                member.spelling = nested.spelling;
                member.packed_range = nested.packed_range;
                member.packed_range_expression =
                    nested.packed_range_expression;
                member.is_signed = nested.is_signed;
                if (vhdl
                    && type.packed_aggregate
                        == frontend::PackedAggregateKind::Struct) {
                    if (nested.domain
                        == frontend::ValueDomain::Logic9) {
                        type.domain = frontend::ValueDomain::Logic9;
                    } else if (
                        nested.domain
                            == frontend::ValueDomain::Logic4
                        && type.domain
                            != frontend::ValueDomain::Logic9) {
                        type.domain = frontend::ValueDomain::Logic4;
                    }
                }
            }
            if (type.systemverilog_container
                && type.systemverilog_container
                       ->associative_index_type
                && !resolve_type(
                    *type.systemverilog_container
                         ->associative_index_type)) {
                return false;
            }
            if (type.vhdl_access) {
                auto& access = *type.vhdl_access;
                if (access.designated_types.size() != 1) {
                    report(
                        "FSIM-ELAB-VHACCESS-002",
                        "a VHDL access declaration requires exactly one "
                        "designated subtype",
                        access.designated_span);
                    return false;
                }
                auto designated = access.designated_types.front();
                if (!resolve_type(designated)) {
                    return false;
                }
                if (designated.vhdl_protected) {
                    report(
                        "FSIM-ELAB-VHACCESS-003",
                        "a bounded VHDL access type cannot designate a "
                        "protected type",
                        access.designated_span);
                    return false;
                }
                const auto handle_capacity =
                    access.handle_width >= 32
                        ? std::numeric_limits<std::uint32_t>::max()
                        : (std::uint32_t{1} << access.handle_width) - 1U;
                if (access.handle_width == 0
                    || access.handle_width > 64
                    || access.maximum_objects == 0
                    || access.maximum_objects > handle_capacity) {
                    report(
                        "FSIM-ELAB-VHACCESS-004",
                        "a VHDL access type has an invalid bounded handle "
                        "representation",
                        access.designated_span);
                    return false;
                }
                access.designated_types.assign(
                    1, std::move(designated));
                type.domain = frontend::ValueDomain::Bit2;
                type.is_signed = false;
                type.packed_range = frontend::PackedRange{
                    static_cast<std::int64_t>(
                        access.handle_width - 1U),
                    0,
                    true};
                type.packed_range_expression.reset();
            }
            if (type.vhdl_physical) {
                auto& physical = *type.vhdl_physical;
                if (!physical.range || physical.units.empty()) {
                    report(
                        "FSIM-ELAB-VHPHYSICAL-001",
                        "a VHDL physical type requires a range and a "
                        "primary unit",
                        type.named_type_span);
                    return false;
                }
                std::string range_error;
                const auto left = evaluate_constant_expression(
                    physical.range->left, {}, range_error);
                const auto right = evaluate_constant_expression(
                    physical.range->right, {}, range_error);
                if (!left || !right
                    || *left < std::numeric_limits<std::int32_t>::min()
                    || *left > std::numeric_limits<std::int32_t>::max()
                    || *right < std::numeric_limits<std::int32_t>::min()
                    || *right > std::numeric_limits<std::int32_t>::max()) {
                    report(
                        "FSIM-ELAB-VHPHYSICAL-002",
                        "a bounded physical range must be locally static "
                        "and fit signed 32-bit primary-unit ticks",
                        physical.range->span);
                    return false;
                }
                physical.resolved_range = frontend::IntegerRange{
                    *left, *right, physical.range->descending};
                std::unordered_map<std::string, std::int64_t> scales;
                for (std::size_t index = 0;
                     index < physical.units.size(); ++index) {
                    auto& physical_unit = physical.units[index];
                    if (index == 0) {
                        if (physical_unit.scale) {
                            report(
                                "FSIM-ELAB-VHPHYSICAL-003",
                                "the primary physical unit cannot have a "
                                "secondary-unit scale",
                                physical_unit.span);
                            return false;
                        }
                        physical_unit.scale_factor = 1;
                        scales.emplace(physical_unit.name, 1);
                        continue;
                    }
                    constexpr std::string_view prefix{
                        "@vhdl-physical:"};
                    if (!physical_unit.scale
                        || physical_unit.scale->kind
                            != frontend::ExpressionKind::Call
                        || !physical_unit.scale->text.starts_with(prefix)
                        || physical_unit.scale->operands.size() != 1) {
                        report(
                            "FSIM-ELAB-VHPHYSICAL-004",
                            "a secondary physical unit requires a positive "
                            "locally static scale in an earlier unit",
                            physical_unit.span);
                        return false;
                    }
                    const auto reference = physical_unit.scale->text.substr(
                        prefix.size());
                    const auto prior = scales.find(reference);
                    std::string magnitude_error;
                    const auto magnitude = evaluate_constant_expression(
                        physical_unit.scale->operands.front(), {},
                        magnitude_error);
                    if (prior == scales.end() || !magnitude
                        || *magnitude <= 0
                        || prior->second
                            > std::numeric_limits<std::int32_t>::max()
                                / *magnitude) {
                        report(
                            "FSIM-ELAB-VHPHYSICAL-004",
                            "secondary unit '" + physical_unit.name
                                + "' has an invalid, forward, or overflowing "
                                "scale",
                            physical_unit.span);
                        return false;
                    }
                    physical_unit.scale_factor = prior->second * *magnitude;
                    scales.emplace(
                        physical_unit.name, *physical_unit.scale_factor);
                }
                type.domain = frontend::ValueDomain::Integer;
                type.is_signed = true;
                type.integer_range = *physical.resolved_range;
            }
            if (type.vhdl_protected) {
                auto& protected_info = *type.vhdl_protected;
                protected_info.variable_offsets.clear();
                protected_info.storage_width = 0;
                for (auto& variable : protected_info.variables) {
                    if (!resolve_type(variable.type)) {
                        return false;
                    }
                    const auto width = variable.type.width();
                    if (!width || *width == 0 || *width > 64
                        || variable.type.domain
                            == frontend::ValueDomain::String
                        || variable.type.domain
                            == frontend::ValueDomain::Unknown
                        || variable.type.domain
                            == frontend::ValueDomain::Logic9
                        || variable.type.vhdl_protected) {
                        report(
                            "FSIM-ELAB-VHPROTECTED-007",
                            "protected private variable '"
                                + variable.name
                                + "' requires a bounded scalar or packed "
                                  "value with width in 1..64",
                            variable.span);
                        return false;
                    }
                    protected_info.variable_offsets.push_back(
                        protected_info.storage_width);
                    protected_info.storage_width += *width;
                }
                for (auto& function : protected_info.functions) {
                    if (!resolve_type(function.return_type)) {
                        return false;
                    }
                    for (auto& argument : function.arguments) {
                        if (!resolve_type(argument.type)) {
                            return false;
                        }
                    }
                }
                for (auto& procedure : protected_info.procedures) {
                    for (auto& argument : procedure.arguments) {
                        if (!resolve_type(argument.type)) {
                            return false;
                        }
                    }
                }
            }
            if (type.vhdl_array) {
                frontend::Type element;
                if (!type.vhdl_array->element_types.empty()) {
                    element = type.vhdl_array->element_types.front();
                } else {
                    element.spelling =
                        type.vhdl_array->element_spelling;
                    element.named_type =
                        type.vhdl_array->element_named_type;
                    element.named_type_span =
                        type.vhdl_array->element_span;
                }
                if (!resolve_type(element)) {
                    return false;
                }
                const auto width = element.width();
                const bool deferred_composite_layout =
                    !element.packed_members.empty()
                    || element.vhdl_array.has_value();
                if (((!width || *width == 0)
                     && !deferred_composite_layout)
                    || element.domain
                        == frontend::ValueDomain::String
                    || element.domain
                        == frontend::ValueDomain::Unknown) {
                    report(
                        "FSIM-ELAB-VHARRAY-001",
                        "VHDL array type '" + type.spelling
                            + "' requires a concrete bounded scalar or "
                              "packed composite element subtype",
                        type.vhdl_array->element_span);
                    return false;
                }
                type.vhdl_array->element_domain =
                    element.domain;
                type.vhdl_array->element_spelling =
                    element.spelling;
                type.vhdl_array->element_named_type.clear();
                type.vhdl_array->element_types.assign(
                    1, element);
                type.domain = element.domain;
                // Resolve packed array elements independently.
                if (!element.vhdl_resolution_function.empty()) {
                    type.vhdl_resolution_function = element.vhdl_resolution_function;
                }
            }
            if (type.named_type.empty()) {
                return !vhdl
                    || validate_direct_constraints(type);
            }
            const auto name = type.named_type;
            const auto use_span = type.named_type_span;
            const auto derived = type;
            std::optional<frontend::Type> base;
            if (name.find("::") == std::string::npos) {
                if (const auto generated = generated_types.find(name);
                    generated != generated_types.end()) {
                    base = *generated->second;
                }
                if (const auto local = local_types.find(name);
                    !base && local != local_types.end()) {
                    if (!resolve_alias(local->second)) {
                        return false;
                    }
                    base =
                        unit.type_aliases[local->second].type;
                }
            }
            if (!base) {
                const auto imported =
                    imported_types.find(name);
                if (imported == imported_types.end()) {
                    const auto separator = name.find('.');
                    const auto deferred_component_type =
                        vhdl
                        && (active_interface_type_formals.contains(name)
                            || (separator != std::string::npos
                                && active_interface_package_formals.contains(
                                    name.substr(0, separator))));
                    if (deferred_component_type) {
                        return true;
                    }
                    const auto deferred_package_type =
                        vhdl
                        && separator != std::string::npos
                        && std::ranges::any_of(
                            unit.package_instances,
                            [&](const auto& instance) {
                                return instance.name
                                    == name.substr(0, separator);
                            });
                    if (deferred_package_type) {
                        return true;
                    }
                    report(
                        vhdl
                            ? "FSIM-ELAB-VHTYPE-001"
                            : "FSIM-ELAB-SVTYPE-001",
                        std::string{
                            vhdl
                                ? "VHDL type '"
                                : "SystemVerilog type alias '"}
                            + name + "' is not visible in this unit",
                        use_span);
                    return false;
                }
                if (imported->second.interface_formal) {
                    return true;
                }
                base = imported->second.type;
            }
            if (!vhdl) {
                const auto container =
                    type.systemverilog_container;
                type = std::move(*base);
                if (container) {
                    type.systemverilog_container = container;
                }
                return true;
            }
            auto constrained =
                apply_derived_constraints(
                    std::move(*base), derived);
            if (!constrained) {
                return false;
            }
            type = std::move(*constrained);
            return true;
        };
        for (std::size_t index = 0;
             index < unit.type_aliases.size(); ++index) {
            (void)resolve_alias(index);
        }
        const auto resolve_declaration =
            [&](auto& declaration) {
                (void)resolve_type(declaration.type);
            };
        const auto resolve_interface_parameter =
            [&](frontend::ParameterDeclaration& generic) {
              switch (generic.kind) {
              case frontend::ParameterKind::Type:
                  if (generic.default_type) {
                      (void)resolve_type(*generic.default_type);
                  }
                  active_interface_type_formals.insert(
                      generic.name);
                  break;
              case frontend::ParameterKind::Function:
                  if (generic.function_profile) {
                      (void)resolve_type(
                          generic.function_profile->return_type);
                      for (auto& argument :
                           generic.function_profile->arguments) {
                          (void)resolve_type(argument.type);
                      }
                  }
                  break;
              case frontend::ParameterKind::Procedure:
                  if (generic.procedure_profile) {
                      for (auto& argument :
                           generic.procedure_profile->arguments) {
                          (void)resolve_type(argument.type);
                      }
                  }
                  break;
              case frontend::ParameterKind::Package:
                  if (generic.package_profile) {
                      for (auto& actual :
                           generic.package_profile->generic_map) {
                          if (actual.type_value) {
                              (void)resolve_type(*actual.type_value);
                          }
                      }
                  }
                  active_interface_package_formals.insert(
                      generic.name);
                  break;
              case frontend::ParameterKind::Value:
                  (void)resolve_type(generic.type);
                  break;
              }
            };
        const auto resolve_component =
            [&](frontend::VhdlComponentDeclaration& component) {
                active_interface_type_formals.clear();
                active_interface_package_formals.clear();
                for (auto& generic : component.generics) {
                    resolve_interface_parameter(generic);
                }
                for (auto& port : component.ports) {
                    (void)resolve_type(port.type);
                }
                active_interface_type_formals.clear();
                active_interface_package_formals.clear();
            };
        std::function<void(std::vector<Statement>&)>
            resolve_statements;
            resolve_statements =
            [&](std::vector<Statement>& statements) {
                for (auto& statement : statements) {
                    for (auto& declaration :
                         statement.declarations) {
                        resolve_declaration(declaration);
                    }
                    resolve_statements(statement.statements);
                    resolve_statements(
                        statement.else_statements);
                    for (auto& alternative :
                         statement.case_alternatives) {
                        resolve_statements(
                            alternative.statements);
                    }
                }
            };
        const auto resolve_local_declarations =
            [&](auto&& self, auto& local_region) -> void {
              const auto saved_package_formals =
                  active_interface_package_formals;
              for (const auto& package :
                   local_region.package_instances) {
                  active_interface_package_formals.insert(
                      package.name);
              }
              struct PriorType {
                  std::string name;
                  frontend::Type* type{};
              };
              struct Declaration {
                  std::size_t offset{};
                  bool type{};
                  bool constant{};
                  bool alias{};
                  std::size_t index{};
              };
              std::vector<PriorType> prior_types;
              std::vector<Declaration> declarations;
              for (std::size_t index = 0;
                   index < local_region.type_aliases.size(); ++index) {
                  declarations.push_back({
                      local_region.type_aliases[index].span.begin.offset,
                      true, false, false, index});
              }
              for (std::size_t index = 0;
                   index < local_region.constants.size(); ++index) {
                  declarations.push_back({
                      local_region.constants[index].span.begin.offset,
                      false, true, false, index});
              }
              for (std::size_t index = 0;
                   index < local_region.signal_aliases.size(); ++index) {
                  declarations.push_back({
                      local_region.signal_aliases[index].span.begin.offset,
                      false, false, true, index});
              }
              for (std::size_t index = 0;
                   index < local_region.variables.size(); ++index) {
                  declarations.push_back({
                      local_region.variables[index].span.begin.offset,
                      false, false, false, index});
              }
              std::ranges::stable_sort(
                  declarations, {}, &Declaration::offset);
              for (const auto& declaration : declarations) {
                  if (declaration.type) {
                      auto& alias = local_region.type_aliases[
                          declaration.index];
                      (void)resolve_type(alias.type);
                      const auto prior =
                          generated_types.find(alias.name);
                      prior_types.push_back({
                          alias.name,
                          prior == generated_types.end()
                              ? nullptr
                              : prior->second});
                      generated_types[alias.name] = &alias.type;
                  } else if (declaration.constant) {
                      resolve_declaration(local_region.constants[
                          declaration.index]);
                  } else if (declaration.alias) {
                      (void)resolve_type(
                          local_region.signal_aliases[
                              declaration.index].type);
                  } else {
                      resolve_declaration(local_region.variables[
                          declaration.index]);
                  }
              }
              for (auto& function : local_region.functions) {
                  (void)resolve_type(function.return_type);
                  for (auto& argument : function.arguments) {
                      (void)resolve_type(argument.type);
                  }
                  self(self, function);
              }
              for (auto& procedure : local_region.procedures) {
                  for (auto& argument : procedure.arguments) {
                      (void)resolve_type(argument.type);
                  }
                  self(self, procedure);
              }
              resolve_statements(local_region.statements);
              for (auto prior = prior_types.rbegin();
                   prior != prior_types.rend(); ++prior) {
                  if (prior->type == nullptr) {
                      generated_types.erase(prior->name);
                  } else {
                      generated_types[prior->name] = prior->type;
                  }
              }
              active_interface_package_formals =
                  saved_package_formals;
            };
        std::function<void(frontend::GenerateBody&)>
            resolve_generate_body;
        std::function<void(
            std::vector<frontend::GenerateRegion>&)>
            resolve_generate_regions;
        resolve_generate_body =
            [&](frontend::GenerateBody& body) {
                struct PriorGeneratedType {
                    std::string name;
                    frontend::Type* type{};
                };
                enum class DeclarationKind {
                    Type,
                    Constant,
                    Signal,
                    SignalAlias,
                    Component,
                    Function,
                    Task,
                    Procedure,
                    GenericFunction,
                    GenericProcedure,
                    Package,
                };
                struct OrderedDeclaration {
                    std::size_t offset{};
                    DeclarationKind kind{};
                    std::size_t index{};
                };
                std::vector<PriorGeneratedType> prior_types;
                std::vector<OrderedDeclaration> declarations;
                const auto append_declarations =
                    [&](const auto& source,
                        const DeclarationKind kind) {
                      for (std::size_t index = 0;
                           index < source.size(); ++index) {
                          declarations.push_back({
                              source[index].span.begin.offset,
                              kind,
                              index});
                      }
                    };
                append_declarations(
                    body.type_aliases, DeclarationKind::Type);
                append_declarations(
                    body.constants, DeclarationKind::Constant);
                append_declarations(
                    body.signals, DeclarationKind::Signal);
                append_declarations(
                    body.signal_aliases,
                    DeclarationKind::SignalAlias);
                append_declarations(
                    body.vhdl_component_declarations,
                    DeclarationKind::Component);
                append_declarations(
                    body.functions, DeclarationKind::Function);
                append_declarations(
                    body.tasks, DeclarationKind::Task);
                append_declarations(
                    body.procedures, DeclarationKind::Procedure);
                append_declarations(
                    body.generic_function_templates,
                    DeclarationKind::GenericFunction);
                append_declarations(
                    body.generic_procedure_templates,
                    DeclarationKind::GenericProcedure);
                append_declarations(
                    body.package_instances,
                    DeclarationKind::Package);
                std::ranges::stable_sort(
                    declarations, {}, &OrderedDeclaration::offset);
                for (const auto& declaration : declarations) {
                    switch (declaration.kind) {
                    case DeclarationKind::Type: {
                        auto& alias =
                            body.type_aliases[declaration.index];
                        (void)resolve_type(alias.type);
                        const auto prior =
                            generated_types.find(alias.name);
                        prior_types.push_back({
                            alias.name,
                            prior == generated_types.end()
                                ? nullptr
                                : prior->second});
                        generated_types[alias.name] = &alias.type;
                        break;
                    }
                    case DeclarationKind::Constant:
                        resolve_declaration(
                            body.constants[declaration.index]);
                        break;
                    case DeclarationKind::Signal:
                        resolve_declaration(
                            body.signals[declaration.index]);
                        break;
                    case DeclarationKind::SignalAlias:
                        (void)resolve_type(
                            body.signal_aliases[declaration.index].type);
                        break;
                    case DeclarationKind::Component:
                        resolve_component(
                            body.vhdl_component_declarations[
                                declaration.index]);
                        break;
                    case DeclarationKind::Function: {
                        auto& function =
                            body.functions[declaration.index];
                        (void)resolve_type(function.return_type);
                        for (auto& argument : function.arguments) {
                            (void)resolve_type(argument.type);
                        }
                        resolve_local_declarations(
                            resolve_local_declarations, function);
                        break;
                    }
                    case DeclarationKind::Task: {
                        auto& task = body.tasks[declaration.index];
                        for (auto& argument : task.arguments) {
                            (void)resolve_type(argument.type);
                        }
                        for (auto& variable : task.variables) {
                            resolve_declaration(variable);
                        }
                        resolve_statements(task.statements);
                        break;
                    }
                    case DeclarationKind::Procedure: {
                        auto& procedure =
                            body.procedures[declaration.index];
                        for (auto& argument : procedure.arguments) {
                            (void)resolve_type(argument.type);
                        }
                        resolve_local_declarations(
                            resolve_local_declarations, procedure);
                        break;
                    }
                    case DeclarationKind::GenericFunction: {
                        auto& generic =
                            body.generic_function_templates[
                                declaration.index];
                        active_interface_type_formals.clear();
                        active_interface_package_formals.clear();
                        for (auto& parameter : generic.generic_parameters) {
                            resolve_interface_parameter(parameter);
                        }
                        auto& function = generic.function;
                        (void)resolve_type(function.return_type);
                        for (auto& argument : function.arguments) {
                            (void)resolve_type(argument.type);
                        }
                        resolve_local_declarations(
                            resolve_local_declarations, function);
                        active_interface_type_formals.clear();
                        active_interface_package_formals.clear();
                        break;
                    }
                    case DeclarationKind::GenericProcedure: {
                        auto& generic =
                            body.generic_procedure_templates[
                                declaration.index];
                        active_interface_type_formals.clear();
                        active_interface_package_formals.clear();
                        for (auto& parameter : generic.generic_parameters) {
                            resolve_interface_parameter(parameter);
                        }
                        auto& procedure = generic.procedure;
                        for (auto& argument : procedure.arguments) {
                            (void)resolve_type(argument.type);
                        }
                        resolve_local_declarations(
                            resolve_local_declarations, procedure);
                        active_interface_type_formals.clear();
                        active_interface_package_formals.clear();
                        break;
                    }
                    case DeclarationKind::Package:
                        for (auto& actual :
                             body.package_instances[
                                 declaration.index].generic_map) {
                            if (actual.type_value) {
                                (void)resolve_type(
                                    *actual.type_value);
                            }
                        }
                        break;
                    }
                }
                for (auto& process : body.processes) {
                    resolve_local_declarations(
                        resolve_local_declarations, process);
                }
                resolve_generate_regions(
                    body.generate_regions);
                for (auto prior = prior_types.rbegin();
                     prior != prior_types.rend(); ++prior) {
                    if (prior->type == nullptr) {
                        generated_types.erase(prior->name);
                    } else {
                        generated_types[prior->name] = prior->type;
                    }
                }
            };
        resolve_generate_regions =
            [&](std::vector<frontend::GenerateRegion>&
                    regions) {
                for (auto& region : regions) {
                    if (std::ranges::any_of(
                            region.block_generics,
                            [](const auto& generic) {
                              return generic.kind
                                  != frontend::ParameterKind::Value;
                            })) {
                        continue;
                    }
                    for (auto& generic : region.block_generics) {
                        resolve_declaration(generic);
                    }
                    for (auto& port : region.block_ports) {
                        resolve_declaration(port);
                    }
                    resolve_generate_body(region.then_body);
                    resolve_generate_body(region.else_body);
                    for (auto& alternative :
                         region.alternatives) {
                        resolve_generate_body(
                            alternative.body);
                    }
                }
            };
        for (auto& parameter : unit.parameters) {
            if (parameter.kind
                == frontend::ParameterKind::Type) {
                if (parameter.default_type) {
                    (void)resolve_type(*parameter.default_type);
                }
            } else if (
                parameter.kind
                    == frontend::ParameterKind::Function) {
                if (parameter.function_profile) {
                    (void)resolve_type(
                        parameter.function_profile->return_type);
                    for (auto& argument :
                         parameter.function_profile->arguments) {
                        (void)resolve_type(argument.type);
                    }
                }
            } else if (
                parameter.kind
                    == frontend::ParameterKind::Procedure) {
                if (parameter.procedure_profile) {
                    for (auto& argument :
                         parameter.procedure_profile->arguments) {
                        (void)resolve_type(argument.type);
                    }
                }
            } else {
                resolve_declaration(parameter);
            }
        }
        if (resolve_ports) {
            for (auto& port : unit.ports) {
                resolve_declaration(port);
            }
        }
        for (auto& signal : unit.signals) {
            resolve_declaration(signal);
        }
        for (auto& alias : unit.signal_aliases) {
            resolve_declaration(alias);
        }
        for (auto& variable : unit.variables) {
            resolve_declaration(variable);
        }
        for (auto& component :
             unit.vhdl_component_declarations) {
            resolve_component(component);
        }
        for (auto& function : unit.functions) {
            (void)resolve_type(function.return_type);
            for (auto& argument : function.arguments) {
                (void)resolve_type(argument.type);
            }
            resolve_local_declarations(
                resolve_local_declarations, function);
        }
        for (auto& task : unit.tasks) {
            for (auto& argument : task.arguments) {
                (void)resolve_type(argument.type);
            }
            for (auto& variable : task.variables) {
                resolve_declaration(variable);
            }
            resolve_statements(task.statements);
        }
        for (auto& procedure : unit.procedures) {
            for (auto& argument : procedure.arguments) {
                (void)resolve_type(argument.type);
            }
            resolve_local_declarations(
                resolve_local_declarations, procedure);
        }
        for (auto& process : unit.processes) {
            resolve_local_declarations(
                resolve_local_declarations, process);
        }
        resolve_generate_regions(unit.generate_regions);
    }

}  // namespace fsim::elaboration
