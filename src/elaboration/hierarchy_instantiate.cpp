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
        SystemVerilogConstantEnvironment
            parameter_integral_environment,
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
                || (parameter_environment.find(parameter.name)
                        == parameter_environment.end()
                    && parameter_integral_environment.find(
                           parameter.name)
                        == parameter_integral_environment.end())) {
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
        const auto builtin_systemverilog_scalar = [](
            const std::string_view spelling,
            const frontend::SystemVerilogScalarKind kind,
            const frontend::ValueDomain domain,
            const bool is_signed) {
          frontend::Type type;
          type.spelling = spelling;
          type.systemverilog_scalar = kind;
          type.domain = domain;
          type.is_signed = is_signed;
          return type;
        };
        static const auto builtin_shortreal =
            builtin_systemverilog_scalar(
                "shortreal",
                frontend::SystemVerilogScalarKind::ShortReal,
                frontend::ValueDomain::Unknown,
                true);
        static const auto builtin_real = builtin_systemverilog_scalar(
            "real",
            frontend::SystemVerilogScalarKind::Real,
            frontend::ValueDomain::Unknown,
            true);
        static const auto builtin_realtime =
            builtin_systemverilog_scalar(
                "realtime",
                frontend::SystemVerilogScalarKind::Realtime,
                frontend::ValueDomain::Unknown,
                true);
        static const auto builtin_time = builtin_systemverilog_scalar(
            "time",
            frontend::SystemVerilogScalarKind::Time,
            frontend::ValueDomain::Bit2,
            false);
        static const auto builtin_chandle = builtin_systemverilog_scalar(
            "chandle",
            frontend::SystemVerilogScalarKind::Chandle,
            frontend::ValueDomain::Unknown,
            false);
        if (unit.language == frontend::Language::Vhdl2008) {
          visible_type_marks.emplace("integer", &builtin_integer);
          visible_type_marks.emplace("natural", &builtin_natural);
          visible_type_marks.emplace("positive", &builtin_positive);
          visible_type_marks.emplace("boolean", &builtin_boolean);
          visible_type_marks.emplace("bit", &builtin_bit);
        } else if (
            unit.language
            == frontend::Language::SystemVerilog2017) {
          visible_type_marks.emplace("shortreal", &builtin_shortreal);
          visible_type_marks.emplace("real", &builtin_real);
          visible_type_marks.emplace("realtime", &builtin_realtime);
          visible_type_marks.emplace("time", &builtin_time);
          visible_type_marks.emplace("chandle", &builtin_chandle);
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
                const auto evaluate_container_constant =
                    [&](const frontend::Expression& expression) {
                      std::string error;
                      const auto value =
                          evaluate_systemverilog_constant_expression(
                              expression,
                              parameter_integral_environment,
                              parameter_environment,
                              error);
                      return value ? value->integer_value()
                                   : std::optional<std::int64_t>{};
                    };
                auto materialized =
                    materialize_systemverilog_container_type(
                        variable.type,
                        variable.span,
                        evaluate_container_constant,
                        [&](std::string code,
                            std::string message,
                            frontend::SourceSpan source) {
                          report(
                              std::move(code),
                              std::move(message),
                              std::move(source));
                        });
                if (!materialized) continue;
                auto type = std::move(*materialized);
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
            if (variable.type.systemverilog_virtual_interface) {
                const auto interface = std::ranges::find_if(
                    parsed_.units,
                    [&](const frontend::DesignUnit& candidate) {
                      return candidate.kind
                              == frontend::UnitKind::SystemVerilogInterface
                          && candidate.name
                              == variable.type
                                     .systemverilog_interface_type;
                    });
                if (interface == parsed_.units.end()) {
                    report(
                        "FSIM-ELAB-SVIFACE-003",
                        "virtual interface '" + path + "."
                            + variable.name + "' requires unknown type '"
                            + variable.type.systemverilog_interface_type
                            + "'",
                        variable.span);
                    continue;
                }
                if (!variable.type.systemverilog_interface_modport.empty()
                    && std::ranges::none_of(
                        interface->systemverilog_modports,
                        [&](const frontend::SystemVerilogModport& modport) {
                          return modport.name
                              == variable.type
                                     .systemverilog_interface_modport;
                        })) {
                    report(
                        "FSIM-ELAB-SVIFACE-004",
                        "virtual interface '" + path + "."
                            + variable.name + "' selects unknown modport '"
                            + variable.type.systemverilog_interface_modport
                            + "' on interface type '"
                            + variable.type.systemverilog_interface_type
                            + "'",
                        variable.span);
                    continue;
                }
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
            if (variable.type.systemverilog_scalar
                != frontend::SystemVerilogScalarKind::None) {
                const frontend::SignalDeclaration declaration{
                    variable.name,
                    variable.type,
                    frontend::PortDirection::Unknown,
                    false,
                    variable.span};
                (void)add_owned_signal(declaration, path, local);
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
                            parameter_integral_environment,
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

        const auto clocking_one_step_delay =
            [&](const frontend::SourceSpan& span)
                -> std::optional<frontend::Delay> {
              std::uint64_t magnitude = 0;
              std::size_t split = 0;
              while (split < unit.time_precision.size()
                     && unit.time_precision[split] >= '0'
                     && unit.time_precision[split] <= '9') {
                  magnitude = magnitude * 10
                      + static_cast<std::uint64_t>(
                          unit.time_precision[split] - '0');
                  ++split;
              }
              if (magnitude == 0
                  || split == unit.time_precision.size()) {
                  report(
                      "FSIM-ELAB-CLOCK-006",
                      "clocking #1step requires a concrete design-unit "
                      "time precision",
                      span);
                  return std::nullopt;
              }
              frontend::Delay delay;
              delay.magnitude = magnitude;
              delay.unit = unit.time_precision.substr(split);
              delay.span = span;
              return delay;
            };
        std::vector<frontend::Statement> clocking_skew_statements;
        std::vector<frontend::Process> clocking_sample_processes;
        for (const auto& block : unit.systemverilog_clocking_blocks) {
            if (block.event.size() != 1
                || block.event.front().signal.empty()) {
                report(
                    "FSIM-ELAB-CLOCK-001",
                    "clocking block '" + path + "." + block.name
                        + "' requires one signal event",
                    block.span);
                continue;
            }
            const auto event =
                local.find(block.event.front().signal);
            if (event == local.end()) {
                report(
                    "FSIM-ELAB-CLOCK-002",
                    "unknown event signal '"
                        + block.event.front().signal
                        + "' for clocking block '" + path + "."
                        + block.name + "'",
                    block.event.front().span);
                continue;
            }
            local.insert_or_assign(block.name, event->second);
            local.insert_or_assign(
                path + "." + block.name, event->second);
            design_.signal_by_name_.emplace(
                path + "." + block.name, event->second);

            for (const auto& member : block.signals) {
                const auto actual_name = member.expression
                    ? member.expression->text : member.name;
                if (member.expression
                    && member.expression->kind
                        != frontend::ExpressionKind::Identifier) {
                    report(
                        "FSIM-ELAB-CLOCK-003",
                        "clocking member '" + block.name + "."
                            + member.name
                            + "' requires a signal identifier expression",
                        member.expression->span);
                    continue;
                }
                const auto actual = local.find(actual_name);
                const auto type = visible_types.find(actual_name);
                if (actual == local.end()
                    || type == visible_types.end()) {
                    report(
                        "FSIM-ELAB-CLOCK-004",
                        "unknown signal '" + actual_name
                            + "' for clocking member '" + block.name + "."
                            + member.name + "'",
                        member.span);
                    continue;
                }
                const auto member_name =
                    block.name + "." + member.name;
                const auto* selected_skew = member.skew
                    ? &*member.skew
                    : member.direction
                              == frontend::PortDirection::Input
                          ? block.default_input_skew
                                ? &*block.default_input_skew
                                : nullptr
                      : member.direction
                                == frontend::PortDirection::Output
                          && block.default_output_skew
                          ? &*block.default_output_skew
                          : nullptr;
                std::optional<frontend::Delay> skew_delay;
                if (selected_skew && selected_skew->delay) {
                    skew_delay = selected_skew->delay;
                }
                if (member.direction
                        == frontend::PortDirection::Input
                    && ((!selected_skew)
                        || selected_skew->one_step
                        || !selected_skew->delay)) {
                    skew_delay =
                        clocking_one_step_delay(member.span);
                }
                if (member.direction
                        == frontend::PortDirection::Output
                    && skew_delay && skew_delay->magnitude != 0) {
                    const frontend::SignalDeclaration request{
                        member_name,
                        *type->second,
                        frontend::PortDirection::Unknown,
                        false,
                        member.span};
                    const auto request_signal =
                        add_owned_signal(request, path, local);
                    if (!request_signal) continue;
                    visible_types.insert_or_assign(
                        member_name, type->second);
                    visible_types.insert_or_assign(
                        path + "." + member_name, type->second);

                    frontend::Statement drive;
                    drive.kind = frontend::StatementKind::Assignment;
                    drive.assignment_kind =
                        frontend::AssignmentKind::Blocking;
                    drive.label = "$clocking$" + block.name + "$"
                        + member.name + "$drive";
                    drive.target = frontend::Expression{
                        frontend::ExpressionKind::Identifier,
                        actual_name,
                        {},
                        member.span};
                    drive.value = frontend::Expression{
                        frontend::ExpressionKind::Identifier,
                        member_name,
                        {},
                        member.span};
                    drive.delay = std::move(skew_delay);
                    drive.span = member.span;
                    if (selected_skew->edge
                        != frontend::EdgeKind::Any) {
                        frontend::Process driver;
                        driver.kind =
                            frontend::ProcessKind::VerilogAlways;
                        driver.name = "$clocking$" + block.name
                            + "$" + member.name + "$drive";
                        driver.sensitivities = block.event;
                        driver.sensitivities.front().edge =
                            selected_skew->edge;
                        driver.statements.push_back(std::move(drive));
                        driver.span = member.span;
                        clocking_sample_processes.push_back(
                            std::move(driver));
                    } else {
                        clocking_skew_statements.push_back(
                            std::move(drive));
                    }
                    continue;
                }
                if (member.direction
                    != frontend::PortDirection::Input) {
                    local.insert_or_assign(
                        member_name, actual->second);
                    local.insert_or_assign(
                        path + "." + member_name, actual->second);
                    visible_types.insert_or_assign(
                        member_name, type->second);
                    visible_types.insert_or_assign(
                        path + "." + member_name, type->second);
                    design_.signal_by_name_.emplace(
                        path + "." + member_name, actual->second);
                }
                if (member.direction
                    == frontend::PortDirection::Output) {
                    continue;
                }

                const frontend::SignalDeclaration sampled{
                    member_name,
                    *type->second,
                    frontend::PortDirection::Unknown,
                    false,
                    member.span};
                const auto sample =
                    add_owned_signal(sampled, path, local);
                if (!sample) continue;
                visible_types.insert_or_assign(
                    member_name, type->second);
                visible_types.insert_or_assign(
                    path + "." + member_name, type->second);

                std::string sample_source = actual_name;
                if (skew_delay && skew_delay->magnitude != 0) {
                    const auto skew_name = "$clocking$"
                        + block.name + "$" + member.name + "$skew";
                    const frontend::SignalDeclaration delayed{
                        skew_name,
                        *type->second,
                        frontend::PortDirection::Unknown,
                        false,
                        member.span};
                    const auto delayed_signal =
                        add_owned_signal(delayed, path, local);
                    if (!delayed_signal) continue;
                    visible_types.insert_or_assign(
                        skew_name, type->second);
                    visible_types.insert_or_assign(
                        path + "." + skew_name, type->second);
                    frontend::Statement history;
                    history.kind =
                        frontend::StatementKind::Assignment;
                    history.assignment_kind =
                        frontend::AssignmentKind::Blocking;
                    history.label = "$clocking$" + block.name + "$"
                        + member.name + "$history";
                    history.target = frontend::Expression{
                        frontend::ExpressionKind::Identifier,
                        skew_name,
                        {},
                        member.span};
                    history.value = member.expression.value_or(
                        frontend::Expression{
                            frontend::ExpressionKind::Identifier,
                            member.name,
                            {},
                            member.span});
                    history.delay = std::move(skew_delay);
                    history.span = member.span;
                    clocking_skew_statements.push_back(
                        std::move(history));
                    sample_source = skew_name;
                }
                frontend::Statement assignment;
                assignment.kind = frontend::StatementKind::Assignment;
                assignment.assignment_kind =
                    frontend::AssignmentKind::Blocking;
                assignment.target = frontend::Expression{
                    frontend::ExpressionKind::Identifier,
                    member_name,
                    {},
                    member.span};
                assignment.value = frontend::Expression{
                    frontend::ExpressionKind::Identifier,
                    std::move(sample_source),
                    {},
                    member.span};
                assignment.span = member.span;
                frontend::Process sampler;
                sampler.kind =
                    frontend::ProcessKind::VerilogAlways;
                sampler.name = "$clocking$" + block.name + "$"
                    + member.name + "$sample";
                sampler.sensitivities = block.event;
                if (selected_skew
                    && selected_skew->edge
                        != frontend::EdgeKind::Any) {
                    sampler.sensitivities.front().edge =
                        selected_skew->edge;
                }
                sampler.statements.push_back(std::move(assignment));
                sampler.span = member.span;
                clocking_sample_processes.push_back(std::move(sampler));
            }
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

        const frontend::SystemVerilogClockingBlock*
            default_clocking = nullptr;
        if (unit.systemverilog_default_clocking_block) {
            const auto selected = std::ranges::find(
                unit.systemverilog_clocking_blocks,
                *unit.systemverilog_default_clocking_block,
                &frontend::SystemVerilogClockingBlock::name);
            if (selected
                != unit.systemverilog_clocking_blocks.end()) {
                default_clocking = &*selected;
            }
        }
        const auto prepare_clocking_cycle_waits =
            [&](auto&& self,
                std::vector<frontend::Statement>& statements) -> void {
              for (auto& statement : statements) {
                if (statement.clocking_cycle_delay) {
                    if (default_clocking == nullptr) {
                        report(
                            "FSIM-ELAB-CLOCK-005",
                            "a ## cycle delay requires a default "
                            "clocking block",
                            statement.span);
                    } else {
                        statement.sensitivities =
                            default_clocking->event;
                        statement.procedural_assignment_repeat = true;
                        statement.loop_limit =
                            statement.clocking_cycle_count;
                    }
                }
                self(self, statement.statements);
                self(self, statement.else_statements);
                for (auto& alternative :
                     statement.case_alternatives) {
                    self(self, alternative.statements);
                }
              }
            };

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
             index < clocking_skew_statements.size(); ++index) {
            auto process = lowerer.lower_concurrent(
                clocking_skew_statements[index],
                unit.language,
                path,
                unit.concurrent_statements.size() + index);
            specialization.processes.push_back(process.id);
            design_.processes_.push_back(std::move(process));
            for (auto& generated :
                 lowerer.take_generated_processes()) {
                specialization.processes.push_back(generated.id);
                design_.processes_.push_back(std::move(generated));
            }
        }
        for (const auto& process : clocking_sample_processes) {
            auto lowered = lowerer.lower_process(
                process, unit.language, path);
            specialization.processes.push_back(lowered.id);
            design_.processes_.push_back(std::move(lowered));
            for (auto& generated : lowerer.take_generated_processes()) {
                specialization.processes.push_back(generated.id);
                design_.processes_.push_back(std::move(generated));
            }
        }
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
        for (const auto& source_process : unit.processes) {
            auto process = source_process;
            prepare_clocking_cycle_waits(
                prepare_clocking_cycle_waits, process.statements);
            auto lowered =
                lowerer.lower_process(process, unit.language, path);
            lowered.reactive = unit.kind
                == frontend::UnitKind::SystemVerilogProgram;
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
                parameter_integral_environment,
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
            auto interface_parameter_identities =
                target->kind
                        == frontend::UnitKind::SystemVerilogInterface
                    ? child_specialized.identity_values
                    : std::vector<
                          std::pair<std::string, std::string>>{};
            instantiate(
                child_specialized.unit,
                child_path,
                std::move(child_aliases.signals),
                std::move(child_aliases.strings),
                std::move(child_aliases.containers),
                std::move(child_aliases.read_only_signals),
                std::move(child_aliases.read_only_strings),
                std::move(child_specialized.environment),
                std::move(child_specialized.integral_environment),
                std::move(child_specialized.values),
                std::move(child_specialized.identity_values),
                std::move(child_specialized.packages));
            if (target->kind
                == frontend::UnitKind::SystemVerilogInterface) {
                systemverilog_interface_instances_.insert_or_assign(
                    child_path, child_specialized.unit);
                systemverilog_interface_handles_.try_emplace(
                    child_path,
                    next_systemverilog_interface_handle_++);
                systemverilog_interface_parameter_identities_
                    .insert_or_assign(
                        child_path, std::move(interface_parameter_identities));
            }
        }
        for (const auto& variable : unit.variables) {
          if (!variable.type.systemverilog_virtual_interface
              || !variable.initializer
              || (variable.initializer->kind
                      == frontend::ExpressionKind::Call
                  && variable.initializer->text == "@sv-null")) {
            continue;
          }
          const auto signal = local.find(variable.name);
          if (signal == local.end()) continue;
          std::string actual_name;
          if (variable.initializer->kind
              == frontend::ExpressionKind::Identifier) {
            actual_name = variable.initializer->text;
          } else if (variable.initializer->kind
                         == frontend::ExpressionKind::Index
                     && variable.initializer->operands.size() == 2
                     && variable.initializer->operands[0].kind
                         == frontend::ExpressionKind::Identifier
                     && variable.initializer->operands[1].kind
                         == frontend::ExpressionKind::IntegerLiteral) {
            actual_name = variable.initializer->operands[0].text
                + "[" + variable.initializer->operands[1].text + "]";
          }
          if (actual_name.empty()) {
            report(
                "FSIM-ELAB-SVIFACE-002",
                "virtual-interface initializer for '" + path + "."
                    + variable.name
                    + "' must name a scalar or statically selected "
                      "interface instance, or null",
                variable.initializer->span);
            continue;
          }
          auto actual_path = path.empty()
              ? actual_name
              : path + "." + actual_name;
          auto actual =
              systemverilog_interface_instances_.find(actual_path);
          auto lexical_path = path;
          while (actual == systemverilog_interface_instances_.end()
                 && lexical_path.find('.') != std::string::npos) {
            lexical_path.resize(lexical_path.rfind('.'));
            actual_path = lexical_path + "." + actual_name;
            actual = systemverilog_interface_instances_.find(actual_path);
          }
          if (actual == systemverilog_interface_instances_.end()) {
            report(
                "FSIM-ELAB-SVIFACE-002",
                "virtual-interface initializer '" + actual_path
                    + "' must name an elaborated interface instance",
                variable.initializer->span);
            continue;
          }
          if (actual->second.name
              != variable.type.systemverilog_interface_type) {
            report(
                "FSIM-ELAB-SVIFACE-003",
                "virtual interface '" + path + "." + variable.name
                    + "' requires type '"
                    + variable.type.systemverilog_interface_type
                    + "' but initializer '" + actual_path
                    + "' has type '" + actual->second.name + "'",
                variable.initializer->span);
            continue;
          }
          if (!variable.type.systemverilog_class_parameter_actuals.empty()) {
            const auto declaration = std::ranges::find_if(
                parsed_.units,
                [&](const DesignUnit& candidate) {
                  return candidate.kind
                          == frontend::UnitKind::SystemVerilogInterface
                      && candidate.name
                          == variable.type.systemverilog_interface_type
                      && candidate.library == actual->second.library;
                });
            const auto actual_identity =
                systemverilog_interface_parameter_identities_.find(
                    actual_path);
            if (declaration == parsed_.units.end()
                || actual_identity
                    == systemverilog_interface_parameter_identities_.end()) {
              report(
                  "FSIM-ELAB-SVIFACE-010",
                  "virtual interface '" + path + "." + variable.name
                      + "' cannot resolve the specialization identity of '"
                      + actual_path + "'",
                  variable.initializer->span);
              continue;
            }
            std::vector<frontend::ParameterOverride> overrides;
            overrides.reserve(
                variable.type.systemverilog_class_parameter_actuals.size());
            for (const auto& retained :
                 variable.type.systemverilog_class_parameter_actuals) {
              frontend::ParameterOverride override;
              override.name = retained.name;
              override.value = retained.value;
              if (retained.type_actual) {
                override.type_value = *retained.type_actual;
              }
              override.span = retained.span;
              overrides.push_back(std::move(override));
            }
            auto required = specialize_selected_unit(
                *declaration,
                overrides,
                parameter_environment,
                parameter_integral_environment,
                parent_domains,
                parent_types,
                unit.functions,
                unit.procedures,
                package_environment,
                unit.language);
            if (required.identity_values
                != actual_identity->second) {
              report(
                  "FSIM-ELAB-SVIFACE-010",
                  "virtual interface '" + path + "." + variable.name
                      + "' requires a different specialization of interface '"
                      + variable.type.systemverilog_interface_type
                      + "' than initializer '" + actual_path + "'",
                  variable.initializer->span);
              continue;
            }
          }
          const auto handle =
              systemverilog_interface_handles_.find(actual_path);
          if (handle == systemverilog_interface_handles_.end()) {
            report(
                "FSIM-ELAB-SVIFACE-002",
                "virtual-interface initializer '" + actual_path
                    + "' has no owned interface identity",
                variable.initializer->span);
            continue;
          }
          design_.signals_[signal->second].initial_value =
              PackedLogic4::from_aval_bval(64, handle->second, 0);
        }
        stack_.pop_back();
    }

} // namespace fsim::elaboration
