// SPDX-License-Identifier: Apache-2.0
HierarchyBuilder::PortAliases HierarchyBuilder::connect_ports(
    const frontend::Instance& instance,
    const std::vector<frontend::SignalDeclaration>& ports,
    const std::string& path,
    const SignalMap& parent_signals,
    const StringMap& parent_strings,
    const std::unordered_set<StringObjectId>&
        parent_read_only_strings,
    const ContainerMap& parent_containers,
    const std::unordered_set<std::string>&
        parent_read_only_containers,
    const Binding* binding,
    const bool cross_language,
    const bool require_input_connections,
    DesignUnit* dependency_owner)
{
    PortAliases result;
    auto& aliases = result.signals;
    auto& string_aliases = result.strings;
    auto& container_aliases = result.containers;
    auto connections = instance.connections;
    const auto wildcard = std::ranges::find_if(
        connections,
        [](const frontend::PortConnection& connection) {
            return connection.port
                && *connection.port == "*";
        });
    if (wildcard != connections.end()) {
        std::unordered_set<std::string> explicitly_connected;
        for (const auto& connection : connections) {
            if (connection.port && *connection.port != "*") {
                explicitly_connected.insert(*connection.port);
            }
        }
        std::vector<frontend::PortConnection> expanded;
        expanded.reserve(connections.size() + ports.size());
        for (auto& connection : connections) {
            if (!connection.port || *connection.port != "*") {
                expanded.push_back(std::move(connection));
                continue;
            }
            for (const auto& port : ports) {
                const bool matching_actual =
                    parent_signals.contains(port.name)
                    || parent_strings.contains(port.name)
                    || parent_containers.contains(port.name);
                if (explicitly_connected.contains(port.name)
                    || !matching_actual) {
                    continue;
                }
                frontend::PortConnection implicit;
                implicit.port = port.name;
                implicit.value.kind =
                    frontend::ExpressionKind::Identifier;
                implicit.value.text = port.name;
                implicit.value.span = connection.span;
                implicit.span = connection.span;
                expanded.push_back(std::move(implicit));
            }
        }
        connections = std::move(expanded);
    }
    std::vector<bool> connected(ports.size());
    std::size_t positional = 0;
    for (const auto& connection : connections) {
        std::size_t port_index = ports.size();
        if (connection.port) {
            const auto found = std::find_if(
                ports.begin(), ports.end(),
                [&](const frontend::SignalDeclaration& port) {
                    return port.name == *connection.port;
                });
            if (found != ports.end()) {
                port_index = static_cast<std::size_t>(
                    std::distance(ports.begin(), found));
            }
        } else {
            while (positional < ports.size() && connected[positional]) {
                ++positional;
            }
            port_index = positional++;
        }
        if (port_index >= ports.size()) {
            report(
                "FSIM-ELAB-BIND-025",
                connection.port
                    ? "unknown port '" + *connection.port
                        + "' on instance '" + path + "'"
                    : "too many positional connections on instance '"
                        + path + "'",
                connection.span);
            continue;
        }
        if (connected[port_index]) {
            report(
                "FSIM-ELAB-BIND-026",
                "port '" + ports[port_index].name
                    + "' is connected more than once on instance '"
                    + path + "'",
                connection.span);
            continue;
        }
        connected[port_index] = true;
        const auto& port = ports[port_index];
        if (!port.interface_type.empty()
            || port.type.spelling == "interface") {
            std::string actual_name;
            if (connection.value.kind
                == frontend::ExpressionKind::Identifier) {
                actual_name = connection.value.text;
            } else if (
                connection.value.kind
                    == frontend::ExpressionKind::Index
                && connection.value.operands.size() == 2
                && connection.value.operands[0].kind
                    == frontend::ExpressionKind::Identifier
                && connection.value.operands[1].kind
                    == frontend::ExpressionKind::IntegerLiteral) {
                actual_name = connection.value.operands[0].text
                    + "[" + connection.value.operands[1].text + "]";
            }
            if (actual_name.empty()) {
                report(
                    "FSIM-ELAB-SVIFACE-001",
                    "interface port '" + path + "." + port.name
                        + "' requires a whole interface-instance actual",
                    connection.value.span);
                continue;
            }
            const auto separator = path.rfind('.');
            const auto parent_path = separator == std::string::npos
                ? std::string { }
                : path.substr(0, separator);
            auto actual_path = parent_path.empty()
                ? actual_name
                : parent_path + "." + actual_name;
            auto actual_interface = systemverilog_interface_instances_.find(actual_path);
            auto lexical_path = parent_path;
            while (actual_interface
                    == systemverilog_interface_instances_.end()
                && lexical_path.find('.') != std::string::npos) {
                lexical_path.resize(lexical_path.rfind('.'));
                actual_path = lexical_path + "." + actual_name;
                actual_interface = systemverilog_interface_instances_.find(actual_path);
            }
            if (actual_interface
                == systemverilog_interface_instances_.end()) {
                report(
                    "FSIM-ELAB-SVIFACE-002",
                    "interface actual '" + actual_path
                        + "' must name an earlier interface instance",
                    connection.value.span);
                continue;
            }
            const auto interface_unit = actual_interface->second;
            if (dependency_owner != nullptr) {
                const auto source = std::string {
                    frontend::physical_source(interface_unit.span)
                };
                if (!source.empty()
                    && std::ranges::find(
                           dependency_owner->source_dependencies,
                           source)
                        == dependency_owner->source_dependencies.end()) {
                    dependency_owner->source_dependencies.push_back(source);
                }
            }
            if (!port.interface_type.empty()
                && port.interface_type != interface_unit.name) {
                report(
                    "FSIM-ELAB-SVIFACE-003",
                    "interface port '" + path + "." + port.name
                        + "' requires type '" + port.interface_type
                        + "' but actual '" + actual_path + "' has type '"
                        + interface_unit.name + "'",
                    connection.span);
                continue;
            }
            const auto actual_view = systemverilog_interface_modport_views_.find(actual_path);
            if (actual_view
                    != systemverilog_interface_modport_views_.end()
                && !actual_view->second.empty()
                && (port.modport.empty()
                    || port.modport != actual_view->second)) {
                report(
                    "FSIM-ELAB-SVIFACE-011",
                    "restricted interface actual '" + actual_path
                        + "' exposes modport '" + actual_view->second
                        + "' and cannot bind "
                        + (port.modport.empty()
                                ? "an unrestricted interface port"
                                : "different modport '" + port.modport + "'"),
                    connection.span);
                continue;
            }
            systemverilog_interface_instances_.insert_or_assign(
                path + "." + port.name, interface_unit);
            if (const auto handle = systemverilog_interface_handles_.find(actual_path);
                handle != systemverilog_interface_handles_.end()) {
                systemverilog_interface_handles_.insert_or_assign(
                    path + "." + port.name, handle->second);
            }
            if (const auto identity = systemverilog_interface_parameter_identities_.find(
                    actual_path);
                identity
                != systemverilog_interface_parameter_identities_.end()) {
                systemverilog_interface_parameter_identities_
                    .insert_or_assign(path + "." + port.name,
                        identity->second);
            }
            systemverilog_interface_port_paths_.insert(
                path + "." + port.name);
            systemverilog_interface_modport_views_.insert_or_assign(
                path + "." + port.name,
                !port.modport.empty()
                    ? port.modport
                    : actual_view
                        != systemverilog_interface_modport_views_.end()
                    ? actual_view->second
                    : std::string { });
            const bool forwarded_interface_port = systemverilog_interface_port_paths_.contains(
                actual_path);
            const frontend::SystemVerilogModport* modport = nullptr;
            if (!port.modport.empty()) {
                const auto found = std::ranges::find_if(
                    interface_unit.systemverilog_modports,
                    [&](const frontend::SystemVerilogModport& candidate) {
                        return candidate.name == port.modport;
                    });
                if (found
                    == interface_unit.systemverilog_modports.end()) {
                    report(
                        "FSIM-ELAB-SVIFACE-004",
                        "interface type '" + interface_unit.name
                            + "' has no modport '" + port.modport + "'",
                        port.span);
                    continue;
                }
                modport = &*found;
            }
            const auto connect_member =
                [&](const std::string& member,
                    const frontend::PortDirection direction,
                    const frontend::SourceSpan& member_span) {
                    const auto signal_name = actual_path + "." + member;
                    const auto signal = design_.signal_by_name_.find(signal_name);
                    const auto clocking_event =
                        systemverilog_clocking_event_signals_.find(signal_name);
                    if (signal == design_.signal_by_name_.end()
                        && clocking_event
                            == systemverilog_clocking_event_signals_.end()) {
                        report(
                            "FSIM-ELAB-SVIFACE-005",
                            "interface member signal '" + signal_name
                                + "' was not elaborated",
                            member_span);
                        return;
                    }
                    const auto signal_id =
                        signal != design_.signal_by_name_.end()
                        ? signal->second
                        : clocking_event->second;
                    const auto local_name = port.name + "." + member;
                    const auto qualified_name = path + "." + local_name;
                    aliases.emplace(local_name, signal_id);
                    aliases.emplace(qualified_name, signal_id);
                    const auto member_path = actual_path + "." + member;
                    const bool inherited_read_only = systemverilog_read_only_interface_member_paths_
                                                         .contains(member_path);
                    if (direction
                            == frontend::PortDirection::Input
                        || inherited_read_only) {
                        result.read_only_signals.insert(signal_id);
                        systemverilog_read_only_interface_member_paths_
                            .insert(qualified_name);
                    }
                    design_.signal_by_name_.emplace(
                        qualified_name, signal_id);
                    if (!forwarded_interface_port
                        && (direction
                                == frontend::PortDirection::Output
                            || direction == frontend::PortDirection::Inout
                            || direction == frontend::PortDirection::Ref
                            || direction
                                == frontend::PortDirection::Buffer)) {
                        note_boundary_driver(
                            signal_id,
                            binding,
                            qualified_name,
                            connection.span);
                    }
                };
            const auto connect_callable =
                [&](const std::string& member,
                    const bool function,
                    const bool imported,
                    const frontend::SourceSpan& member_span) {
                    if (dependency_owner == nullptr) {
                        report(
                            "FSIM-ELAB-SVIFACE-007",
                            "interface callable '" + member
                                + "' has no same-language module owner",
                            member_span);
                        return;
                    }
                    if (!imported) {
                        bool supplied { };
                        if (function) {
                            const auto profile = std::ranges::find(
                                interface_unit.functions, member,
                                &frontend::FunctionDeclaration::name);
                            supplied = profile != interface_unit.functions.end()
                                && std::ranges::any_of(
                                    dependency_owner->functions,
                                    [&](const auto& candidate) {
                                        return systemverilog_function_profile_matches(
                                            *profile, candidate);
                                    });
                        } else {
                            const auto profile = std::ranges::find(
                                interface_unit.tasks, member,
                                &frontend::TaskDeclaration::name);
                            supplied = profile != interface_unit.tasks.end()
                                && std::ranges::any_of(
                                    dependency_owner->tasks,
                                    [&](const auto& candidate) {
                                        return systemverilog_task_profile_matches(
                                            *profile, candidate);
                                    });
                        }
                        if (!supplied) {
                            report(
                                "FSIM-ELAB-SVIFACE-009",
                                "modport export '" + member
                                    + "' has no matching module callable",
                                member_span);
                        }
                        return;
                    }
                    const auto qualified = port.name + "." + member;
                    if (function) {
                        const auto found = std::ranges::find_if(
                            interface_unit.functions,
                            [&](const auto& candidate) {
                                return candidate.name == member;
                            });
                        if (found == interface_unit.functions.end()) {
                            report(
                                "FSIM-ELAB-SVIFACE-007",
                                "interface function '" + member
                                    + "' was not retained",
                                member_span);
                            return;
                        }
                        if (std::ranges::any_of(
                                dependency_owner->functions,
                                [&](const auto& candidate) {
                                    return candidate.name == qualified;
                                })) {
                            report(
                                "FSIM-ELAB-SVIFACE-008",
                                "interface callable '" + qualified
                                    + "' is visible more than once",
                                member_span);
                            return;
                        }
                        auto callable = *found;
                        qualify_interface_callable(
                            callable, port.name, interface_unit);
                        dependency_owner->functions.push_back(
                            std::move(callable));
                        return;
                    }
                    const auto found = std::ranges::find_if(
                        interface_unit.tasks,
                        [&](const auto& candidate) {
                            return candidate.name == member;
                        });
                    if (found == interface_unit.tasks.end()) {
                        report(
                            "FSIM-ELAB-SVIFACE-007",
                            "interface task '" + member
                                + "' was not retained",
                            member_span);
                        return;
                    }
                    if (std::ranges::any_of(
                            dependency_owner->tasks,
                            [&](const auto& candidate) {
                                return candidate.name == qualified;
                            })) {
                        report(
                            "FSIM-ELAB-SVIFACE-008",
                            "interface callable '" + qualified
                                + "' is visible more than once",
                            member_span);
                        return;
                    }
                    auto callable = *found;
                    qualify_interface_callable(
                        callable, port.name, interface_unit);
                    dependency_owner->tasks.push_back(
                        std::move(callable));
                };
            if (modport != nullptr) {
                for (const auto& member : modport->members) {
                    using Kind = frontend::SystemVerilogModportMemberKind;
                    if (member.kind == Kind::Signal) {
                        connect_member(
                            member.name, member.direction, member.span);
                    } else if (member.kind == Kind::Clocking) {
                        const auto block = std::ranges::find(
                            interface_unit.systemverilog_clocking_blocks,
                            member.name,
                            &frontend::SystemVerilogClockingBlock::name);
                        if (block == interface_unit.systemverilog_clocking_blocks.end()) {
                            report(
                                "FSIM-ELAB-CLOCK-007",
                                "modport clocking member '" + member.name
                                    + "' was not retained by interface '"
                                    + interface_unit.name + "'",
                                member.span);
                            continue;
                        }
                        connect_member(
                            block->name,
                            frontend::PortDirection::Input,
                            block->span);
                        for (const auto& clocking_signal : block->signals) {
                            connect_member(
                                block->name + "." + clocking_signal.name,
                                clocking_signal.direction,
                                clocking_signal.span);
                        }
                    } else {
                        connect_callable(
                            member.name,
                            member.kind == Kind::FunctionImport
                                || member.kind == Kind::FunctionExport,
                            member.kind == Kind::FunctionImport
                                || member.kind == Kind::TaskImport,
                            member.span);
                    }
                }
            } else {
                for (const auto& member : interface_unit.signals) {
                    connect_member(
                        member.name,
                        frontend::PortDirection::Unknown,
                        member.span);
                }
                for (const auto& function : interface_unit.functions) {
                    connect_callable(
                        function.name, true, true, function.span);
                }
                for (const auto& task : interface_unit.tasks) {
                    connect_callable(
                        task.name, false, true, task.span);
                }
            }
            continue;
        }
        if (connection.kind
            == frontend::PortActualKind::Open) {
            if (port.direction
                == frontend::PortDirection::Input) {
                if (!cross_language
                    && dependency_owner != nullptr
                    && port.default_value) {
                    auto default_connection = connection;
                    default_connection.kind = frontend::PortActualKind::Default;
                    default_connection.value = *port.default_value;
                    (void)connect_vhdl_expression_port(
                        port,
                        default_connection,
                        path,
                        parent_signals,
                        result,
                        *dependency_owner);
                    continue;
                }
                report(
                    "FSIM-ELAB-BIND-027",
                    "input port '" + path + "." + port.name
                        + "' cannot be open without a component "
                          "default",
                    connection.span);
            }
            continue;
        }
        if (connection.kind
            == frontend::PortActualKind::Default) {
            if (port.type.systemverilog_container) {
                report(
                    "FSIM-ELAB-SVPORT-003",
                    "container input ports do not support default "
                    "connection values",
                    connection.span);
                continue;
            }
            if (port.direction
                != frontend::PortDirection::Input) {
                report(
                    "FSIM-ELAB-VHCOMP-013",
                    "only an input component port can materialize "
                    "a default on '"
                        + path + "." + port.name + "'",
                    connection.span);
                continue;
            }
            const auto signal = add_owned_signal(port, path, aliases);
            if (!signal) {
                continue;
            }
            const auto width = static_cast<std::size_t>(
                port.type.width().value_or(1));
            std::string default_error;
            auto lowered = static_vhdl_value(
                connection.value,
                port.type,
                default_error);
            if (!lowered
                || lowered->width() != width) {
                report(
                    "FSIM-ELAB-VHCOMP-013",
                    "component input default for '" + path + "."
                        + port.name
                        + "' is not a statically foldable value "
                          "compatible with the selected port type: "
                        + default_error,
                    connection.value.span);
                continue;
            }
            design_.signals_.at(*signal).initial_value = std::move(*lowered);
            continue;
        }
        if (port.type.systemverilog_container) {
            const auto object = connect_container_port(
                port,
                connection,
                path,
                parent_signals,
                parent_containers,
                parent_read_only_containers,
                cross_language);
            if (object) {
                container_aliases.emplace(
                    port.name, *object);
                container_aliases.emplace(
                    path + "." + port.name, *object);
                design_.container_by_name_.emplace(
                    path + "." + port.name, *object);
            }
            continue;
        }
        if (port.type.domain
            == frontend::ValueDomain::String) {
            const auto object = connect_string_port(
                port,
                connection,
                path,
                parent_strings,
                parent_read_only_strings,
                cross_language);
            if (object) {
                string_aliases.emplace(port.name, *object);
                string_aliases.emplace(
                    path + "." + port.name, *object);
                design_.string_by_name_.emplace(
                    path + "." + port.name, *object);
                design_.string_object_info_.push_back(
                    StringObjectInfo {
                        *object,
                        path + "." + port.name,
                        port.span,
                        true,
                        port.direction });
                if (port.direction
                    == frontend::PortDirection::Input) {
                    result.read_only_strings.insert(*object);
                }
            }
            continue;
        }
        if (!cross_language
            && dependency_owner != nullptr
            && dependency_owner->language
                != frontend::Language::Vhdl2008
            && connect_verilog_memory_word_port(
                port,
                connection,
                path,
                parent_containers,
                result)) {
            continue;
        }
        const auto cross_language_verilog_constant =
            [&]() {
              if (!cross_language || dependency_owner == nullptr
                  || port.direction
                      != frontend::PortDirection::Input
                  || connection.kind
                      != frontend::PortActualKind::Expression) {
                return false;
              }
              std::string error;
              return evaluate_systemverilog_constant_expression(
                  connection.value, { }, { }, error).has_value();
            }();
        if (cross_language_verilog_constant
            && connect_verilog_expression_port(
                port,
                connection,
                path,
                parent_signals,
                result)) {
            continue;
        }
        if (!cross_language
            && dependency_owner != nullptr
            && dependency_owner->language
                != frontend::Language::Vhdl2008
            && connect_verilog_expression_port(
                port,
                connection,
                path,
                parent_signals,
                result)) {
            continue;
        }
        if (!cross_language
            && dependency_owner != nullptr
            && dependency_owner->language
                == frontend::Language::Vhdl2008
            && connect_vhdl_expression_port(
                port,
                connection,
                path,
                parent_signals,
                result,
                *dependency_owner)) {
            continue;
        }
        if (connection.value.kind != frontend::ExpressionKind::Identifier) {
            report(
                "FSIM-ELAB-BIND-027",
                "boundary connection actuals must be whole signals",
                connection.value.span);
            continue;
        }
        const auto actual = parent_signals.find(connection.value.text);
        if (actual == parent_signals.end()) {
            report(
                "FSIM-ELAB-BIND-028",
                "unknown connection signal '" + connection.value.text
                    + "' on instance '" + path + "'",
                connection.value.span);
            continue;
        }
        // Adapter creation may append an owned formal signal and reallocate
        // signal_info_.  Keep the actual metadata by value across that
        // append instead of retaining a reference into the vector.
        const auto actual_info = design_.signal_info_.at(actual->second);
        const auto diagnostics_before = diagnostics_.size();
        validate_boundary_type(
            port,
            actual_info,
            path,
            connection.span,
            cross_language);
        if (port.vhdl_mode_view
            && diagnostics_.size() == diagnostics_before) {
            VhdlModeViewBinding view_binding;
            view_binding.formal = path + "." + port.name;
            view_binding.view = port.vhdl_mode_view->view;
            view_binding.kind = port.vhdl_mode_view->kind;
            view_binding.source = connection.span;
            std::string endpoint_error;
            if (!materialize_vhdl_mode_view_endpoints(
                    port.type,
                    *port.vhdl_mode_view,
                    actual->second,
                    actual_info.width,
                    view_binding.formal,
                    actual_info.name,
                    view_binding.elements,
                    endpoint_error)) {
                report(
                    "FSIM-ELAB-VHVIEW-006",
                    "cannot materialize VHDL mode-view endpoints for '"
                        + view_binding.formal + "': " + endpoint_error,
                    connection.span);
                continue;
            }
            bool all_input = !port.vhdl_mode_view->elements.empty();
            bool has_writable_element = false;
            for (const auto& element : port.vhdl_mode_view->elements) {
                all_input = all_input
                    && element.direction
                        == frontend::PortDirection::Input;
                has_writable_element = has_writable_element
                    || element.direction
                        == frontend::PortDirection::Output
                    || element.direction
                        == frontend::PortDirection::Inout
                    || element.direction
                        == frontend::PortDirection::Buffer;
            }
            design_.signal_info_.at(actual->second)
                .vhdl_mode_view_bindings.push_back(
                    std::move(view_binding));
            if (all_input) {
                result.read_only_signals.insert(actual->second);
            }
            if (has_writable_element) {
                note_boundary_driver(
                    actual->second,
                    binding,
                    path + "." + port.name,
                    connection.span);
            }
        }
        const auto formal_width = static_cast<std::size_t>(
            port.type.width().value_or(1));
        auto formal_signal = actual->second;
        std::optional<ProcessId> conversion_process;
        const bool width_changed = formal_width != actual_info.width;
        const bool signedness_changed = formal_width > 1
            && port.type.is_signed != actual_info.is_signed;
        const bool boolean_boundary = formal_width == 1 && actual_info.width == 1
            && ((port.type.domain == frontend::ValueDomain::Boolean
                    && (actual_info.source_domain
                            == frontend::ValueDomain::Bit2
                        || actual_info.source_domain
                            == frontend::ValueDomain::Logic4))
                || (actual_info.source_domain
                        == frontend::ValueDomain::Boolean
                    && (port.type.domain
                            == frontend::ValueDomain::Bit2
                        || port.type.domain
                            == frontend::ValueDomain::Logic4)));
        const bool integer_boundary = formal_width == 32 && actual_info.width == 32
            && port.type.is_signed && actual_info.is_signed
            && ((port.type.domain
                        == frontend::ValueDomain::Integer
                    && (actual_info.source_domain
                            == frontend::ValueDomain::Bit2
                        || actual_info.source_domain
                            == frontend::ValueDomain::Logic4))
                || (actual_info.source_domain
                        == frontend::ValueDomain::Integer
                    && (port.type.domain
                            == frontend::ValueDomain::Bit2
                        || port.type.domain
                            == frontend::ValueDomain::Logic4)));
        const bool two_state_boundary = port.type.domain == frontend::ValueDomain::Bit2
            && actual_info.source_domain
                == frontend::ValueDomain::Bit2;
        const bool state_domain_boundary = (port.type.domain == frontend::ValueDomain::Logic4
                                               && actual_info.source_domain
                                                   == frontend::ValueDomain::Logic9)
            || (port.type.domain == frontend::ValueDomain::Logic9
                && actual_info.source_domain
                    == frontend::ValueDomain::Logic4);
        const bool needs_adapter = diagnostics_.size() == diagnostics_before
            && cross_language
            && (width_changed || signedness_changed
                || boolean_boundary || integer_boundary)
            && (port.direction == frontend::PortDirection::Input
                || port.direction == frontend::PortDirection::Output
                || port.direction == frontend::PortDirection::Buffer);
        if (needs_adapter) {
            const auto owned = add_owned_signal(port, path, aliases);
            if (!owned) {
                continue;
            }
            formal_signal = *owned;
            const bool input = port.direction == frontend::PortDirection::Input;
            const auto source = input ? actual->second : formal_signal;
            const auto destination = input ? formal_signal : actual->second;
            const auto source_width = input ? actual_info.width : formal_width;
            const auto destination_width = input ? formal_width : actual_info.width;
            const auto source_domain = input
                ? actual_info.source_domain
                : port.type.domain;
            const auto destination_domain = input
                ? port.type.domain
                : actual_info.source_domain;
            const bool sign_extend = input
                ? actual_info.is_signed
                : port.type.is_signed;
            Process adapter;
            adapter.id = static_cast<ProcessId>(
                design_.processes_.size());
            adapter.name = path + "." + port.name
                + (boolean_boundary
                        ? "$boundary_boolean"
                        : integer_boundary
                        ? "$boundary_integer"
                        : "$boundary_integral");
            adapter.static_sensitivity.push_back(
                Sensitivity { source, EdgeKind::any });
            InstructionIndex conversion_start = 0;
            if (destination_domain
                    == frontend::ValueDomain::Boolean
                || (destination_domain
                        == frontend::ValueDomain::Integer
                    && !is_two_state_domain(source_domain))) {
                adapter.operations.emplace_back(WaitSensitivity { });
                adapter.operations.emplace_back(Jump { 2 });
                conversion_start = 2;
            }
            adapter.operations.emplace_back(ReadSignal { 0, source });
            RegisterId converted = 1;
            if (destination_domain
                == frontend::ValueDomain::Boolean) {
                adapter.register_count = 3;
                converted = 0;
                adapter.register_value_kinds = {
                    value_kind(source_domain),
                    value_kind(source_domain),
                    value_kind(source_domain)
                };
                adapter.operations.emplace_back(
                    LoadConstant {
                        1,
                        PackedLogic4(31, Logic4::zero) });
                adapter.operations.emplace_back(
                    Concatenate { 2, { 1, 0 }, 32 });
                adapter.operations.emplace_back(
                    IntegerCheck { 2, 0, 1 });
            } else if (destination_domain
                == frontend::ValueDomain::Integer) {
                const auto range = input
                    ? port.type.integer_range
                    : actual_info.integer_range;
                const auto lower = range
                    ? static_cast<std::int32_t>(
                          std::min(range->left, range->right))
                    : std::numeric_limits<std::int32_t>::min();
                const auto upper = range
                    ? static_cast<std::int32_t>(
                          std::max(range->left, range->right))
                    : std::numeric_limits<std::int32_t>::max();
                adapter.register_count = 1;
                converted = 0;
                adapter.register_value_kinds = {
                    value_kind(source_domain)
                };
                adapter.operations.emplace_back(
                    IntegerCheck { 0, lower, upper });
            } else if (destination_width == source_width) {
                adapter.register_count = 2;
                adapter.register_value_kinds = {
                    value_kind(source_domain),
                    value_kind(destination_domain)
                };
                adapter.operations.emplace_back(
                    CopyRegister { converted, 0 });
            } else if (destination_width < source_width) {
                adapter.register_count = 2;
                adapter.register_value_kinds = {
                    value_kind(source_domain),
                    value_kind(destination_domain)
                };
                adapter.operations.emplace_back(
                    Extract {
                        converted,
                        0,
                        0,
                        static_cast<std::uint32_t>(destination_width) });
            } else {
                adapter.register_count = 3;
                converted = 2;
                std::vector<RegisterId> operands;
                if (sign_extend) {
                    adapter.operations.emplace_back(
                        Extract {
                            1,
                            0,
                            static_cast<std::uint32_t>(
                                source_width - 1U),
                            1 });
                    operands.assign(
                        destination_width - source_width, 1);
                } else {
                    adapter.operations.emplace_back(
                        LoadConstant {
                            1,
                            PackedLogic4(
                                destination_width - source_width,
                                Logic4::zero) });
                    operands.push_back(1);
                }
                operands.push_back(0);
                adapter.register_value_kinds = {
                    value_kind(source_domain),
                    value_kind(sign_extend
                            ? source_domain
                            : destination_domain),
                    value_kind(destination_domain)
                };
                adapter.operations.emplace_back(
                    Concatenate {
                        converted,
                        std::move(operands),
                        static_cast<std::uint32_t>(destination_width) });
            }
            adapter.operations.emplace_back(
                WriteUpdate { destination, converted });
            adapter.operations.emplace_back(WaitSensitivity { });
            adapter.operations.emplace_back(
                Jump { conversion_start });
            adapter.driver_regions.push_back(
                Process::DriverRegion {
                    destination,
                    0,
                    static_cast<std::uint32_t>(destination_width),
                    true });
            conversion_process = adapter.id;
            design_.specializations_.back().processes.push_back(adapter.id);
            design_.processes_.push_back(std::move(adapter));
        }
        if (diagnostics_.size() == diagnostics_before
            && cross_language
            && ((port.type.packed_range
                    && actual_info.packed_range)
                || boolean_boundary || integer_boundary
                || two_state_boundary || state_domain_boundary)) {
            design_.boundary_conversions_.push_back(
                BoundaryConversionInfo {
                    boolean_boundary
                        ? BoundaryConversionKind::boolean_adapter
                        : integer_boundary
                        ? BoundaryConversionKind::integer_adapter
                        : state_domain_boundary && !width_changed
                            && !signedness_changed
                        ? BoundaryConversionKind::state_domain_alias
                        : width_changed && signedness_changed
                        ? BoundaryConversionKind::width_signedness_adapter
                        : width_changed
                        ? BoundaryConversionKind::width_adapter
                        : signedness_changed
                        ? BoundaryConversionKind::signedness_adapter
                        : BoundaryConversionKind::ordinal_alias,
                    path + "." + port.name,
                    formal_signal,
                    actual_info.id,
                    conversion_process,
                    port.direction,
                    formal_width,
                    actual_info.width,
                    port.type.domain,
                    actual_info.source_domain,
                    port.type.is_signed,
                    actual_info.is_signed,
                    state_domain_boundary,
                    port.type.packed_range,
                    actual_info.packed_range,
                    port.type.integer_range,
                    actual_info.integer_range,
                    connection.span,
                    port.span,
                    frontend::physical_source(
                        actual_info.declaration_span)
                            .empty()
                        ? connection.value.span
                        : actual_info.declaration_span });
        }
        if (cross_language
            && port.direction == frontend::PortDirection::Inout) {
            if (binding == nullptr || !binding->resolver) {
                report(
                    "FSIM-ELAB-BIND-030",
                    "cross-language inout '" + path + "." + port.name
                        + "' requires resolver = \"std_logic\" or "
                          "\"sv_wire\"",
                    connection.span);
            }
        }
        aliases.emplace(port.name, formal_signal);
        aliases.emplace(path + "." + port.name, formal_signal);
        design_.signal_by_name_.emplace(
            path + "." + port.name, formal_signal);
        if (port.direction == frontend::PortDirection::Input) {
            result.read_only_signals.insert(formal_signal);
        }
        if (port.direction == frontend::PortDirection::Output
            || port.direction == frontend::PortDirection::Inout
            || port.direction == frontend::PortDirection::Buffer) {
            note_boundary_driver(
                actual->second,
                binding,
                path,
                connection.span);
        }
    }
    if (instance.unconnected_drive
        != frontend::VerilogUnconnectedDrive::None) {
        for (std::size_t port_index = 0;
            port_index < ports.size(); ++port_index) {
            const auto& port = ports[port_index];
            if (connected[port_index]
                || port.direction
                    != frontend::PortDirection::Input
                || port.type.domain
                    == frontend::ValueDomain::String
                || port.type.systemverilog_container) {
                continue;
            }
            auto pulled = port;
            pulled.type.spelling = instance.unconnected_drive
                    == frontend::VerilogUnconnectedDrive::Pull0
                ? "tri0"
                : "tri1";
            (void)add_owned_signal(pulled, path, aliases);
        }
    }
    if (require_input_connections
        || std::ranges::any_of(
            ports,
            [](const auto& port) {
                return (port.type.systemverilog_container
                           || port.type.domain
                               == frontend::ValueDomain::String)
                    && port.direction
                    == frontend::PortDirection::Input;
            })) {
        for (std::size_t port_index = 0;
            port_index < ports.size(); ++port_index) {
            if (!connected[port_index]
                && ports[port_index].direction
                    == frontend::PortDirection::Input) {
                if (cross_language
                    && !ports[port_index].type.systemverilog_container
                    && ports[port_index].type.domain
                        != frontend::ValueDomain::String) {
                    if (const auto signal = add_owned_signal(
                            ports[port_index], path, aliases)) {
                        result.read_only_signals.insert(*signal);
                    }
                    continue;
                }
                if (!cross_language
                    && dependency_owner != nullptr
                    && ports[port_index].default_value) {
                    frontend::PortConnection default_connection;
                    default_connection.port = ports[port_index].name;
                    default_connection.value = *ports[port_index].default_value;
                    default_connection.kind = frontend::PortActualKind::Default;
                    default_connection.span = ports[port_index].span;
                    (void)connect_vhdl_expression_port(
                        ports[port_index],
                        default_connection,
                        path,
                        parent_signals,
                        result,
                        *dependency_owner);
                    continue;
                }
                report(
                    ports[port_index].type.domain
                            == frontend::ValueDomain::String
                        ? "FSIM-ELAB-SVPORT-010"
                        : ports[port_index].type.systemverilog_container
                        ? "FSIM-ELAB-SVPORT-006"
                        : "FSIM-ELAB-BIND-027",
                    std::string {
                        ports[port_index].type.domain
                                == frontend::ValueDomain::String
                            ? "required mutable string input port '"
                            : ports[port_index].type.systemverilog_container
                            ? "required container input port '"
                            : "required VHDL input port '" }
                        + path + "." + ports[port_index].name
                        + "' is not associated",
                    ports[port_index].span);
            }
        }
    }
    return result;
}
