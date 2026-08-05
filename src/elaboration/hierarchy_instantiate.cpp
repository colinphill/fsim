// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"
#include "vhdl_array_boundary.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;

namespace {

template <typename SignalMap>
void adapt_vhdl_array_port_shapes(
    DesignUnit& unit,
    const frontend::Instance& instance,
    const SignalMap& parent_signals,
    const std::span<const SignalInfo> signal_info,
    std::vector<std::pair<std::string, std::string>>& identities) {
  if (unit.language != frontend::Language::Vhdl2008
      || unit.ports.empty()) {
    return;
  }
  std::vector<bool> connected(unit.ports.size());
  std::size_t positional = 0;
  for (const auto& connection : instance.connections) {
    std::size_t port_index = unit.ports.size();
    if (connection.port) {
      const auto found = std::ranges::find_if(
          unit.ports,
          [&](const auto& port) {
            return port.name == *connection.port;
          });
      if (found != unit.ports.end()) {
        port_index = static_cast<std::size_t>(
            std::distance(unit.ports.begin(), found));
      }
    } else {
      while (positional < unit.ports.size()
             && connected[positional]) {
        ++positional;
      }
      port_index = positional++;
    }
    if (port_index >= unit.ports.size()
        || connected[port_index]
        || connection.kind != frontend::PortActualKind::Expression
        || connection.value.kind
            != frontend::ExpressionKind::Identifier) {
      continue;
    }
    connected[port_index] = true;
    auto& formal = unit.ports[port_index];
    if (!formal.type.vhdl_array) {
      continue;
    }
    const auto actual = parent_signals.find(connection.value.text);
    if (actual == parent_signals.end()
        || actual->second >= signal_info.size()) {
      continue;
    }
    const auto& info = signal_info[actual->second];
    const bool indefinite =
        !formal.type.vhdl_array->flat_width
        || std::ranges::any_of(
            formal.type.vhdl_array->dimensions,
            [](const auto& dimension) {
              return dimension.unconstrained;
            });
    if (!indefinite || !info.vhdl_array
        || formal.type.nominal_type != info.nominal_type) {
      continue;
    }
    formal.type.vhdl_array = info.vhdl_array;
    formal.type.vhdl_array_constraints.clear();
    formal.type.packed_range = info.packed_range;
    formal.type.packed_range_expression.reset();
    formal.type.domain = info.source_domain;
    formal.type.is_signed = info.is_signed;
    identities.emplace_back(
        "__vhdl_port_shape." + formal.name,
        vhdl_array_shape_identity(formal.type));
  }
}

}  // namespace

    void HierarchyBuilder::instantiate(
        const DesignUnit& unit,
        const std::string& path,
        SignalMap aliases,
        StringMap string_aliases,
        ContainerMap container_aliases,
        std::unordered_set<SignalId> read_only_signals,
        std::unordered_set<StringObjectId> read_only_strings,
        ConstantEnvironment parameter_environment,
        std::vector<std::pair<std::string, std::string>>
            parameter_values,
        std::vector<std::pair<std::string, std::string>>
            parameter_identity_values,
        PackageEnvironment package_environment) {
        const auto parent_types =
            unit.language == frontend::Language::Vhdl2008
                ? local_vhdl_type_environment(unit)
            : unit.language
                    == frontend::Language::SystemVerilog2017
                ? local_systemverilog_type_environment(unit)
                : NamedTypeEnvironment{};
        ConstantDomainEnvironment parent_domains;
        for (const auto& parameter : unit.parameters) {
            if (parameter.kind
                    != frontend::ParameterKind::Value
                || parameter_environment.find(parameter.name)
                    == parameter_environment.end()) {
                continue;
            }
            parent_domains.insert_or_assign(
                parameter.name,
                ConstantTypeInfo{
                    parameter.type.domain,
                    unit.language
                            == frontend::Language::Vhdl2008
                        && !parameter.type
                                .enumeration_literals.empty(),
                    parameter.type.nominal_type});
        }
        if (!instance_paths_.insert(path).second) {
            report(
                "FSIM-ELAB-HIER-001",
                "duplicate instance path '" + path + "'",
                unit.span);
            return;
        }
        const auto identity = unit_identity(unit);
        if (std::find(stack_.begin(), stack_.end(), identity) != stack_.end()) {
            report(
                "FSIM-ELAB-HIER-002",
                "recursive instantiation of '" + identity + "' at '" + path
                    + "'",
                unit.span);
            return;
        }
        stack_.push_back(identity);
        register_vhdl_resolution_functions(unit);

        SignalMap local = std::move(aliases);
        if (unit.language
            == frontend::Language::SystemVerilog2017) {
            for (const auto& [name, signal] : global_root_signals_) {
                local.try_emplace(name, signal);
            }
        }
        StringMap local_string_objects =
            std::move(string_aliases);
        ContainerMap local_container_objects =
            std::move(container_aliases);
        std::unordered_set<std::string>
            read_only_container_objects;
        std::unordered_map<
            std::string, const frontend::Type*> visible_types;
        std::unordered_map<
            std::string, const frontend::Type*> visible_type_marks;
        const auto builtin_scalar = [](
            const frontend::ValueDomain domain,
            const std::string_view spelling,
            const std::optional<frontend::IntegerRange> range = {}) {
          frontend::Type type;
          type.domain = domain;
          type.spelling = spelling;
          type.is_signed = domain == frontend::ValueDomain::Integer;
          type.integer_range = range;
          return type;
        };
        static const auto builtin_integer = builtin_scalar(
            frontend::ValueDomain::Integer,
            "integer",
            frontend::IntegerRange{
                std::numeric_limits<std::int32_t>::min(),
                std::numeric_limits<std::int32_t>::max(), false});
        static const auto builtin_natural = builtin_scalar(
            frontend::ValueDomain::Integer,
            "natural",
            frontend::IntegerRange{
                0, std::numeric_limits<std::int32_t>::max(), false});
        static const auto builtin_positive = builtin_scalar(
            frontend::ValueDomain::Integer,
            "positive",
            frontend::IntegerRange{
                1, std::numeric_limits<std::int32_t>::max(), false});
        static const auto builtin_boolean = builtin_scalar(
            frontend::ValueDomain::Boolean, "boolean");
        static const auto builtin_bit = builtin_scalar(
            frontend::ValueDomain::Bit2, "bit");
        if (unit.language == frontend::Language::Vhdl2008) {
          visible_type_marks.emplace("integer", &builtin_integer);
          visible_type_marks.emplace("natural", &builtin_natural);
          visible_type_marks.emplace("positive", &builtin_positive);
          visible_type_marks.emplace("boolean", &builtin_boolean);
          visible_type_marks.emplace("bit", &builtin_bit);
        }
        const auto expose_type_mark =
            [&](const std::string_view name,
                const frontend::Type& type) {
              if (unit.language
                      == frontend::Language::SystemVerilog2017
                  || unit.language
                      == frontend::Language::Vhdl2008) {
                  visible_type_marks.try_emplace(
                      std::string{name}, &type);
              }
            };
        for (const auto& alias : unit.type_aliases) {
            expose_type_mark(alias.name, alias.type);
        }
        for (const auto& parameter : unit.parameters) {
            if (unit.language
                    == frontend::Language::SystemVerilog2017
                && parameter.kind
                    == frontend::ParameterKind::Type) {
                expose_type_mark(
                    parameter.name, parameter.type);
            } else if (unit.language
                       != frontend::Language::SystemVerilog2017) {
                expose_type_mark(
                    parameter.type.spelling, parameter.type);
            }
        }
        const auto* ports = unit_ports(parsed_, unit);
        if (ports == nullptr) {
            report(
                "FSIM-ELAB-002",
                "architecture '" + unit.name + "' has no matching entity",
                unit.span);
            stack_.pop_back();
            return;
        }
        for (const auto& port : *ports) {
          expose_type_mark(port.type.spelling, port.type);
          visible_types.emplace(port.name, &port.type);
          visible_types.emplace(
              path + "." + port.name, &port.type);
          if (!port.interface_type.empty()
              || port.type.spelling == "interface") {
            continue;
          }
            if (port.type.domain
                == frontend::ValueDomain::String) {
                const auto object =
                    add_owned_string_port(
                        port, path, local_string_objects);
                if (object
                    && port.direction
                        == frontend::PortDirection::Input) {
                    read_only_strings.insert(*object);
                }
                continue;
            }
            if (port.type.systemverilog_container) {
                if (!local_container_objects.contains(
                        port.name)) {
                    (void)add_owned_container_port(
                        port,
                        path,
                        local_container_objects,
                        parameter_environment);
                }
                if (port.direction
                    == frontend::PortDirection::Input) {
                    read_only_container_objects.insert(
                        port.name);
                    read_only_container_objects.insert(
                        path + "." + port.name);
                }
                continue;
            }
            if (!local.contains(port.name)) {
                (void)add_owned_signal(port, path, local);
            }
        }
        for (const auto& signal : unit.signals) {
            expose_type_mark(signal.type.spelling, signal.type);
            visible_types.emplace(signal.name, &signal.type);
            visible_types.emplace(
                path + "." + signal.name, &signal.type);
            if (!local.contains(signal.name)) {
                (void)add_owned_signal(signal, path, local);
            }
        }
        for (const auto& alias : unit.signal_aliases) {
            expose_type_mark(alias.type.spelling, alias.type);
            visible_types.emplace(alias.name, &alias.type);
            visible_types.emplace(path + "." + alias.name, &alias.type);
            const auto actual = local.find(alias.actual);
            if (actual == local.end()) {
                report(
                    "FSIM-ELAB-VHBLOCK-003",
                    "unknown signal target '" + alias.actual
                        + "' for object alias '" + alias.name + "'",
                    alias.span);
                continue;
            }
            const auto& info = design_.signal_info_.at(actual->second);
            const auto width = alias.type.width();
            const auto same_packed_range = [&]() {
              if (alias.type.packed_range.has_value()
                  != info.packed_range.has_value()) {
                  return false;
              }
              if (!alias.type.packed_range) {
                  return true;
              }
              return alias.type.packed_range->left
                         == info.packed_range->left
                  && alias.type.packed_range->right
                         == info.packed_range->right
                  && alias.type.packed_range->descending
                         == info.packed_range->descending;
            };
            if (!width || *width != info.width
                || alias.type.domain != info.source_domain
                || alias.type.is_signed != info.is_signed
                || !same_packed_range()
                || (!alias.type.nominal_type.empty()
                    && alias.type.nominal_type
                        != info.nominal_type)) {
                report(
                    "FSIM-ELAB-VHBLOCK-003",
                    "object alias '" + alias.name
                        + "' does not match signal target '"
                        + alias.actual + "' (alias width="
                        + (width ? std::to_string(*width) : "unknown")
                        + ", signal width="
                        + std::to_string(info.width)
                        + ", alias nominal='" + alias.type.nominal_type
                        + "', signal nominal='" + info.nominal_type
                        + "')",
                    alias.span);
                continue;
            }
            const frontend::SignalDeclaration formal{
                alias.name,
                alias.type,
                alias.direction,
                true,
                alias.span};
            const auto diagnostics_before = diagnostics_.size();
            validate_boundary_type(
                formal, info, path, alias.span, false);
            if (diagnostics_.size() != diagnostics_before) {
                continue;
            }
            local.emplace(alias.name, actual->second);
            if (!path.empty()) {
                local.emplace(
                    path + "." + alias.name, actual->second);
            }
            design_.signal_by_name_.emplace(
                alias.name, actual->second);
            if (!path.empty()) {
                design_.signal_by_name_.emplace(
                    path + "." + alias.name,
                    actual->second);
            }
        }
        SystemVerilogStringEnvironment string_values;
        for (const auto& variable : unit.variables) {
            visible_types.emplace(
                variable.name, &variable.type);
            visible_types.emplace(
                path + "." + variable.name, &variable.type);
            if (variable.vhdl_shared) {
                if (unit.language
                        != frontend::Language::Vhdl2008
                    || !variable.type.vhdl_protected) {
                    report(
                        "FSIM-ELAB-VHPROTECTED-008",
                        "shared variable '" + path + "."
                            + variable.name
                            + "' must have a protected type",
                        variable.span);
                    continue;
                }
                const auto& protected_info =
                    *variable.type.vhdl_protected;
                if (!protected_info.has_body
                    || !protected_info.body_conformant) {
                    report(
                        "FSIM-ELAB-VHPROTECTED-009",
                        "shared protected object '" + path + "."
                            + variable.name
                            + "' requires one conforming protected body",
                        variable.span);
                    continue;
                }
                if (variable.initializer) {
                    report(
                        "FSIM-ELAB-VHPROTECTED-010",
                        "a protected shared variable is constructed from "
                        "its private member defaults and cannot have an "
                        "object initializer",
                        variable.initializer->span);
                    continue;
                }
                const auto object_index =
                    design_.vhdl_protected_object_info_.size();
                const auto object_id = static_cast<
                    VhdlProtectedObjectId>(object_index);
                if (static_cast<std::size_t>(object_id)
                    != object_index) {
                    throw std::length_error{
                        "too many elaborated VHDL protected objects"};
                }
                VhdlProtectedObjectInfo object;
                object.id = object_id;
                object.name = path + "." + variable.name;
                object.type_name = variable.type.spelling;
                object.nominal_type = variable.type.nominal_type;
                object.declaration_span = variable.span;
                for (std::size_t member_index = 0;
                     member_index < protected_info.variables.size();
                     ++member_index) {
                    const auto& member =
                        protected_info.variables[member_index];
                    const auto width = member.type.width();
                    if (!width || *width == 0 || *width > 64) {
                        continue;
                    }
                    ContainerType storage_type;
                    storage_type.element_width =
                        static_cast<std::uint32_t>(*width);
                    storage_type.element_nominal_type =
                        member.type.nominal_type;
                    storage_type.two_state =
                        is_two_state_domain(member.type.domain);
                    storage_type.signed_elements =
                        member.type.is_signed;
                    storage_type.fixed = true;
                    storage_type.index_left = 0;
                    storage_type.index_right = 0;
                    storage_type.dimensions.push_back({0, 0});
                    auto initial =
                        default_container_value(storage_type);
                    initial.elements[0] = default_packed_value(
                        member.type,
                        static_cast<std::size_t>(*width));
                    if (member.initializer) {
                        std::string error;
                        const auto value = static_vhdl_value(
                            *member.initializer,
                            member.type,
                            error);
                        if (!value || value->width() != *width) {
                            report(
                                "FSIM-ELAB-VHPROTECTED-011",
                                "protected private initializer for '"
                                    + object.name + "." + member.name
                                    + "' is not a static value compatible "
                                      "with its declared subtype: "
                                    + error,
                                member.initializer->span);
                            continue;
                        }
                        initial.elements[0] = *value;
                    }
                    const auto storage_index =
                        design_.container_objects_.size();
                    const auto storage_id = static_cast<
                        ContainerObjectId>(storage_index);
                    if (static_cast<std::size_t>(storage_id)
                        != storage_index) {
                        throw std::length_error{
                            "too many elaborated container objects"};
                    }
                    const auto member_name =
                        object.name + "." + member.name;
                    design_.container_object_info_.push_back(
                        ContainerObjectInfo{
                            storage_id,
                            member_name,
                            storage_type,
                            member.span,
                            false,
                            frontend::PortDirection::Unknown,
                            std::nullopt});
                    design_.container_objects_.push_back(
                        ContainerObject{
                            member_name,
                            std::move(initial),
                            std::nullopt});
                    local_container_objects.emplace(
                        variable.name + "." + member.name,
                        storage_id);
                    local_container_objects.emplace(
                        member_name, storage_id);
                    design_.container_by_name_.emplace(
                        member_name, storage_id);
                    if (design_.roots_.size() == 1 && path == active_root_) {
                        design_.container_by_name_.emplace(
                            variable.name + "." + member.name,
                            storage_id);
                    }
                    object.members.push_back(
                        VhdlProtectedMemberInfo{
                            member.name,
                            member.type,
                            protected_info.variable_offsets[
                                member_index],
                            static_cast<std::size_t>(*width),
                            storage_id,
                            member.span});
                }
                if (object.members.size()
                    == protected_info.variables.size()) {
                    design_.vhdl_protected_object_info_.push_back(
                        std::move(object));
                }
                continue;
            }
            if (variable.type.systemverilog_container) {
                const auto width = variable.type.width();
                if (!width || *width == 0 || *width > 64
                    || variable.type.packed_aggregate
                        == frontend::PackedAggregateKind::UnpackedStruct
                    || variable.type.domain
                        == frontend::ValueDomain::String) {
                    report(
                        "FSIM-ELAB-SVCONTAINER-003",
                        "container elements must be bounded integral, enum, "
                        "or packed aggregate values with an executable "
                        "width in 1..64",
                        variable.span);
                    continue;
                }
                ContainerType type;
                type.element_width =
                    static_cast<std::uint32_t>(*width);
                type.element_nominal_type =
                    variable.type.nominal_type;
                type.two_state =
                    is_two_state_domain(variable.type.domain);
                type.signed_elements = variable.type.is_signed;
                type.queue =
                    variable.type.systemverilog_container->kind
                    == frontend::SystemVerilogContainerKind::Queue;
                type.associative =
                    variable.type.systemverilog_container->kind
                    == frontend::SystemVerilogContainerKind::
                        AssociativeArray;
                type.fixed =
                    variable.type.systemverilog_container->kind
                    == frontend::SystemVerilogContainerKind::
                        StaticArray;
                if (type.associative) {
                    const auto& index_type =
                        variable.type.systemverilog_container
                            ->associative_index_type;
                    const auto index_width =
                        index_type ? index_type->width()
                                   : std::nullopt;
                    if (!index_type || !index_width
                        || *index_width == 0
                        || *index_width > 64
                        || index_type->domain
                            == frontend::ValueDomain::String
                        || index_type->domain
                            == frontend::ValueDomain::Unknown
                        || !index_type->packed_members.empty()
                        || index_type->vhdl_array) {
                        report(
                            "FSIM-ELAB-SVCONTAINER-013",
                            "associative-array indices require a resolved "
                            "integral scalar type with width in 1..64",
                            variable.type.systemverilog_container->span);
                        continue;
                    }
                    type.index_width =
                        static_cast<std::uint32_t>(*index_width);
                    type.two_state_indices =
                        is_two_state_domain(index_type->domain);
                    type.signed_indices = index_type->is_signed;
                }
                if (variable.type.systemverilog_container
                        ->queue_maximum) {
                    std::string error;
                    const auto maximum =
                        evaluate_systemverilog_constant_expression(
                            *variable.type.systemverilog_container
                                 ->queue_maximum,
                            {},
                            parameter_environment,
                            error);
                    const auto maximum_index =
                        maximum
                            ? maximum->integer_value()
                            : std::nullopt;
                    if (!maximum_index
                        || *maximum_index < 0) {
                        report(
                            "FSIM-ELAB-SVCONTAINER-004",
                            "bounded queue maximum index must specialize "
                            "to a known nonnegative value",
                            variable.type.systemverilog_container
                                ->queue_maximum->span);
                        continue;
                    }
                    type.maximum_elements =
                        static_cast<std::uint64_t>(
                            *maximum_index) + 1U;
                }
                if (type.fixed) {
                    const auto& ranges =
                        variable.type.systemverilog_container
                            ->static_range_expressions;
                    const auto in_int32 =
                        [](const std::int64_t value) {
                          return value
                                  >= std::numeric_limits<
                                      std::int32_t>::min()
                              && value
                                  <= std::numeric_limits<
                                      std::int32_t>::max();
                        };
                    std::uint64_t total = 1;
                    bool valid_dimensions = !ranges.empty();
                    for (const auto& range : ranges) {
                        std::string left_error;
                        std::string right_error;
                        const auto left_value =
                            evaluate_systemverilog_constant_expression(
                                range.left, {}, parameter_environment,
                                left_error);
                        const auto right_value =
                            evaluate_systemverilog_constant_expression(
                                range.right, {}, parameter_environment,
                                right_error);
                        std::int64_t left{};
                        std::int64_t right{};
                        bool has_left{};
                        bool has_right{};
                        if (left_value) {
                            if (const auto converted =
                                    left_value->integer_value()) {
                                left = *converted;
                                has_left = true;
                            }
                        }
                        if (right_value) {
                            if (const auto converted =
                                    right_value->integer_value()) {
                                right = *converted;
                                has_right = true;
                            }
                        }
                        if (!has_left || !has_right || !in_int32(left)
                            || !in_int32(right)) {
                            valid_dimensions = false;
                            break;
                        }
                        const auto count = static_cast<std::uint64_t>(
                            left >= right
                                ? left - right
                                : right - left) + 1U;
                        const auto storage_limit =
                            maximum_container_elements(type);
                        if (count > storage_limit
                            || total > storage_limit / count) {
                            valid_dimensions = false;
                            break;
                        }
                        total *= count;
                        type.dimensions.push_back(ContainerDimension{
                            static_cast<std::int32_t>(left),
                            static_cast<std::int32_t>(right)});
                    }
                    if (!valid_dimensions) {
                        report(
                            "FSIM-ELAB-SVCONTAINER-020",
                            "static unpacked-array dimensions must "
                            "specialize to 32-bit ranges within the "
                            "per-container owning-storage budget",
                            variable.type.systemverilog_container->span);
                        continue;
                    }
                    type.index_left = type.dimensions.front().first;
                    type.index_right = type.dimensions.front().second;
                }
                if (variable.initializer) {
                    report(
                        "FSIM-ELAB-SVCONTAINER-012",
                        "module container declaration initializers are not "
                        "executable; use an initial block",
                        variable.initializer->span);
                    continue;
                }
                const auto index =
                    design_.container_objects_.size();
                const auto id =
                    static_cast<ContainerObjectId>(index);
                if (static_cast<std::size_t>(id) != index) {
                    throw std::length_error{
                        "too many elaborated container objects"};
                }
                const auto full_name =
                    path + "." + variable.name;
                design_.container_object_info_.push_back(
                    ContainerObjectInfo{
                        id,
                        full_name,
                        type,
                        variable.span,
                        false,
                        frontend::PortDirection::Unknown,
                        std::nullopt});
                design_.container_objects_.push_back(
                    ContainerObject{
                        full_name,
                        default_container_value(type),
                        std::nullopt});
                local_container_objects.emplace(
                    variable.name, id);
                local_container_objects.emplace(
                    full_name, id);
                design_.container_by_name_.emplace(
                    full_name, id);
                if (design_.roots_.size() == 1 && path == active_root_) {
                    design_.container_by_name_.emplace(
                        variable.name, id);
                }
                continue;
            }
            if (!variable.type.systemverilog_class_declaration.empty()) {
                const frontend::SignalDeclaration declaration{
                    variable.name,
                    variable.type,
                    frontend::PortDirection::Unknown,
                    false,
                    variable.span};
                const auto signal =
                    add_owned_signal(declaration, path, local);
                if (signal) {
                    design_.signals_[*signal].initial_value =
                        PackedLogic4::from_aval_bval(64, 0, 0);
                }
                continue;
            }
            if (variable.type.domain
                == frontend::ValueDomain::Integer) {
                const frontend::SignalDeclaration declaration{
                    variable.name,
                    variable.type,
                    frontend::PortDirection::Unknown,
                    false,
                    variable.span};
                const auto signal =
                    add_owned_signal(declaration, path, local);
                if (signal && variable.initializer) {
                    std::string error;
                    const auto value =
                        evaluate_systemverilog_constant_expression(
                            *variable.initializer,
                            {},
                            parameter_environment,
                            error);
                    const auto converted =
                        value
                            ? convert_systemverilog_parameter_value(
                                  *value, variable.type, error)
                            : std::nullopt;
                    if (!converted || !converted->known()) {
                        report(
                            "FSIM-ELAB-SVFILE-008",
                            "module integer initializer for '"
                                + path + "." + variable.name
                                + "' is not a known 32-bit constant: "
                                + error,
                            variable.span);
                    } else {
                        design_.signals_[*signal].initial_value =
                            PackedLogic4::from_aval_bval(
                                converted->width,
                                converted->bits,
                                0);
                    }
                }
                continue;
            }
            if (variable.type.domain
                != frontend::ValueDomain::String) {
                report(
                    "FSIM-ELAB-SVSTRING-016",
                    "module variable '" + path + "."
                        + variable.name
                        + "' is not a supported string object",
                    variable.span);
                continue;
            }
            SystemVerilogStringValue initial;
            initial.source = variable.span;
            if (variable.initializer) {
                std::string error;
                const auto value =
                    evaluate_systemverilog_string_expression(
                        *variable.initializer,
                        string_values,
                        {},
                        error);
                if (!value) {
                    report(
                        "FSIM-ELAB-SVSTRING-017",
                        "cannot evaluate module string initializer for '"
                            + path + "." + variable.name
                            + "': " + error,
                        variable.span);
                    continue;
                }
                initial = *value;
            }
            if (initial.bytes.size()
                > maximum_string_bytes) {
                report(
                    "FSIM-ELAB-SVSTRING-007",
                    "module string initializer exceeds the 4096-byte "
                    "limit",
                    variable.span);
                continue;
            }
            const auto index =
                design_.string_objects_.size();
            const auto id =
                static_cast<StringObjectId>(index);
            if (static_cast<std::size_t>(id) != index) {
                throw std::length_error{
                    "too many elaborated string objects"};
            }
            const auto full_name =
                path + "." + variable.name;
            design_.string_object_info_.push_back(
                StringObjectInfo{
                    id,
                    full_name,
                    variable.span,
                    false,
                    frontend::PortDirection::Unknown});
            design_.string_objects_.push_back(
                StringObject{full_name, initial.bytes});
            local_string_objects.emplace(
                variable.name, id);
            local_string_objects.emplace(
                full_name, id);
            design_.string_by_name_.emplace(full_name, id);
            if (design_.roots_.size() == 1 && path == active_root_) {
                design_.string_by_name_.emplace(
                    variable.name, id);
            }
            string_values.emplace(
                variable.name, std::move(initial));
        }

        const auto specialization_index = design_.specializations_.size();
        const auto specialization_id =
            static_cast<SpecializationId>(specialization_index);
        if (static_cast<std::size_t>(specialization_id)
            != specialization_index) {
            throw std::length_error(
                "too many elaborated design-unit specializations");
        }
        SpecializationInfo specialization;
        specialization.id = specialization_id;
        specialization.unit = identity;
        specialization.instance = path;
        specialization.source =
            std::string{frontend::physical_source(unit.span)};
        if (unit.kind == frontend::UnitKind::VhdlArchitecture) {
            if (const auto* entity = find_vhdl_entity(parsed_, unit);
                entity != nullptr
                && frontend::physical_source(entity->span)
                    != specialization.source) {
                specialization.source_dependencies.push_back(
                    std::string{
                        frontend::physical_source(entity->span)});
            }
        }
        for (const auto& dependency : unit.source_dependencies) {
            if (dependency != specialization.source
                && std::find(
                       specialization.source_dependencies.begin(),
                       specialization.source_dependencies.end(),
                       dependency)
                    == specialization.source_dependencies.end()) {
                specialization.source_dependencies.push_back(
                    dependency);
            }
        }
        specialization.language = unit.language;
        specialization.library =
            unit.library.empty() ? "work" : unit.library;
        specialization.is_cell = unit.is_cell;
        specialization.parameter_values = std::move(parameter_values);
        specialization.parameter_identity_values =
            std::move(parameter_identity_values);

        const auto specify_path_begin =
            design_.verilog_specify_paths_.size();
        validate_verilog_specify(
            unit, path, local, parameter_environment);

        Lowerer lowerer{
            design_,
            local,
            read_only_signals,
            local_string_objects,
            read_only_strings,
            local_container_objects,
            read_only_container_objects,
            visible_types,
            visible_type_marks,
            unit.functions,
            unit.tasks,
            unit.procedures,
            diagnostics_};
        for (std::size_t index = 0;
             index < unit.concurrent_statements.size(); ++index) {
            auto process = lowerer.lower_concurrent(
                unit.concurrent_statements[index],
                unit.language,
                path,
                index);
            specialization.processes.push_back(process.id);
            design_.processes_.push_back(std::move(process));
            for (auto& generated : lowerer.take_generated_processes()) {
                specialization.processes.push_back(generated.id);
                design_.processes_.push_back(std::move(generated));
            }
        }
        for (const auto& process : unit.processes) {
            auto lowered =
                lowerer.lower_process(process, unit.language, path);
            specialization.processes.push_back(lowered.id);
            design_.processes_.push_back(std::move(lowered));
            for (auto& generated : lowerer.take_generated_processes()) {
                specialization.processes.push_back(generated.id);
                design_.processes_.push_back(std::move(generated));
            }
        }
        const auto regions_overlap = [](
            const Process::DriverRegion& region,
            const VerilogSpecifyTerminalInfo& terminal) {
          if (region.signal != terminal.signal) return false;
          if (region.whole) return true;
          const auto region_end =
              static_cast<std::uint64_t>(region.offset) + region.width;
          const auto terminal_end =
              static_cast<std::uint64_t>(terminal.offset)
              + terminal.width;
          return region.offset < terminal_end
              && terminal.offset < region_end;
        };
        for (auto path_index = specify_path_begin;
             path_index < design_.verilog_specify_paths_.size();
             ++path_index) {
          auto& specify_path =
              design_.verilog_specify_paths_[path_index];
          for (const auto process_id : specialization.processes) {
            const auto& process = design_.processes_.at(process_id);
            if (std::ranges::any_of(
                    process.driver_regions,
                    [&](const auto& region) {
                      return std::ranges::any_of(
                          specify_path.destinations,
                          [&](const auto& terminal) {
                            return regions_overlap(region, terminal);
                          });
                    })) {
              specify_path.drivers.push_back(process_id);
            }
          }
        }
        design_.specializations_.push_back(std::move(specialization));

        validate_vhdl_component_configurations(unit, path);
        for (const auto& instance : unit.instances) {
            const auto child_path = path + "." + instance.name;
            const auto* binding = binding_for(child_path);
            const auto build_systemc =
                [&](const frontend::Instance& selected_instance,
                    const std::string_view selected_target) {
                  const auto* description =
                      construct_systemc_description(
                          selected_instance,
                          child_path,
                          selected_target,
                          parameter_environment,
                          unit.language);
                  if (description == nullptr) {
                      return;
                  }
                  auto [child_aliases, child_objects] =
                      connect_systemc_instance(
                          selected_instance,
                          *description,
                          child_path,
                          local,
                          binding);
                  instantiate_systemc(
                      *description,
                      child_path,
                      std::move(child_aliases),
                      std::move(child_objects));
                };
            if (binding != nullptr && binding->target.has_value()) {
                const auto target = parse_target(*binding->target);
                if (target
                    && target->language == "systemc") {
                    build_systemc(instance, *binding->target);
                    continue;
                }
            }
            ConfiguredVhdlInstance configured;
            const frontend::Instance* selected_instance =
                &instance;
            const DesignUnit* target = nullptr;
            if (binding == nullptr || !binding->target.has_value()) {
                configured = instance.vhdl_configuration_instance
                    ? bind_vhdl_direct_configuration_instance(
                        unit, instance, child_path)
                    : bind_vhdl_component_instance(
                        unit,
                        instance,
                        child_path,
                        parameter_environment,
                        parent_domains,
                        parent_types,
                        unit.functions,
                        unit.procedures,
                        package_environment);
                if (!configured.valid) {
                    continue;
                }
                if (configured.applied) {
                    selected_instance = &configured.instance;
                    if (configured.systemc_target.has_value()) {
                        build_systemc(
                            *selected_instance,
                            *configured.systemc_target);
                        continue;
                    }
                    target = configured.target
                        ? &*configured.target
                        : nullptr;
                    if (target == nullptr) {
                        continue;
                    }
                }
            }
            if (target == nullptr) {
                if ((binding == nullptr
                     || !binding->target.has_value())
                    && selected_instance->unit_name.find_first_of(".(")
                        == std::string::npos) {
                    const auto library = unit.library.empty()
                        ? std::string{"work"} : unit.library;
                    const auto inferred = inferred_target(
                        library,
                        selected_instance->unit_name,
                        child_path,
                        selected_instance->span);
                    if (!inferred.has_value()) {
                        continue;
                    }
                    if (inferred->systemc_target.has_value()) {
                        build_systemc(
                            *selected_instance,
                            *inferred->systemc_target);
                        continue;
                    }
                    if (inferred->udp != nullptr) {
                        instantiate_udp(
                            *inferred->udp,
                            *selected_instance,
                            child_path,
                            local,
                            local_string_objects,
                            read_only_strings,
                            local_container_objects,
                            read_only_container_objects,
                            binding);
                        continue;
                    }
                    target = inferred->unit;
                } else {
                    target = bound_target(
                        *selected_instance,
                        unit,
                        child_path,
                        binding);
                }
            }
            if (target == nullptr) continue;
            if (selected_instance->anonymous) {
                report("FSIM-ELAB-BIND-062", "module instance '" + child_path
                    + "' requires an explicit instance name", selected_instance->span);
                continue;
            }
            if (selected_instance->udp_delay
                || selected_instance->drive_strength) {
                const bool delay = selected_instance->udp_delay.has_value();
                report(delay ? "FSIM-ELAB-BIND-063" : "FSIM-ELAB-BIND-065",
                    "module instance '" + child_path + "' cannot use UDP "
                        + (delay ? "propagation-delay" : "drive-strength")
                        + " syntax", selected_instance->span);
                continue;
            }
            auto child_specialized = specialize_selected_unit(
                *target,
                selected_instance->parameter_overrides,
                parameter_environment,
                parent_domains,
                parent_types,
                unit.functions,
                unit.procedures,
                package_environment,
                unit.language);
            adapt_vhdl_array_port_shapes(
                child_specialized.unit,
                *selected_instance,
                local,
                design_.signals(),
                child_specialized.identity_values);
            if (!configured.component_identity.empty()) {
                if (child_specialized.identity_values.empty()) {
                    child_specialized.identity_values =
                        child_specialized.values;
                }
                child_specialized.values.emplace_back(
                    "__component",
                    configured.component_name);
                child_specialized.identity_values.emplace_back(
                    "__component",
                    configured.component_identity);
            }
            if (!configured.configuration_identity.empty()
                && configured.component_identity.empty()) {
                child_specialized.identity_values.emplace_back(
                    "__configuration",
                    configured.configuration_identity);
            }
            auto child_aliases = connect_instance(
                *selected_instance,
                child_specialized.unit,
                child_path,
                local,
                local_string_objects,
                read_only_strings,
                local_container_objects,
                read_only_container_objects,
                binding,
                target->language != unit.language);
            for (auto& driver : child_aliases.vhdl_input_drivers) {
                child_specialized.unit.concurrent_statements.push_back(
                    std::move(driver));
            }
            if (configured.referenced_configuration != nullptr) {
                vhdl_configurations_by_path_[child_path] =
                    configured.referenced_configuration;
            }
            instantiate(
                child_specialized.unit,
                child_path,
                std::move(child_aliases.signals),
                std::move(child_aliases.strings),
                std::move(child_aliases.containers),
                std::move(child_aliases.read_only_signals),
                std::move(child_aliases.read_only_strings),
                std::move(child_specialized.environment),
                std::move(child_specialized.values),
                std::move(child_specialized.identity_values),
                std::move(child_specialized.packages));
            if (target->kind
                == frontend::UnitKind::SystemVerilogInterface) {
                systemverilog_interface_instances_.insert_or_assign(
                    child_path, child_specialized.unit);
            }
        }
        stack_.pop_back();
    }

} // namespace fsim::elaboration
