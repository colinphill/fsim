// SPDX-License-Identifier: Apache-2.0

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
    struct ClockingEventProcess {
        frontend::Process process;
        bool observed { };
    };
    std::vector<ClockingEventProcess> clocking_event_processes;
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
        const auto event = local.find(block.event.front().signal);
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
                ? member.expression->text
                : member.name;
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
            const auto member_name = block.name + "." + member.name;
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
                skew_delay = clocking_one_step_delay(member.span);
            }
            if (member.direction
                    == frontend::PortDirection::Output
                && skew_delay && skew_delay->magnitude != 0) {
                const frontend::SignalDeclaration request {
                    member_name,
                    *type->second,
                    frontend::PortDirection::Unknown,
                    false,
                    member.span
                };
                const auto request_signal = add_owned_signal(request, path, local);
                if (!request_signal)
                    continue;
                visible_types.insert_or_assign(
                    member_name, type->second);
                visible_types.insert_or_assign(
                    path + "." + member_name, type->second);

                frontend::Statement drive;
                drive.kind = frontend::StatementKind::Assignment;
                drive.assignment_kind = frontend::AssignmentKind::Blocking;
                drive.label = "$clocking$" + block.name + "$"
                    + member.name + "$drive";
                drive.target = frontend::Expression {
                    frontend::ExpressionKind::Identifier,
                    actual_name,
                    { },
                    member.span
                };
                drive.value = frontend::Expression {
                    frontend::ExpressionKind::Identifier,
                    member_name,
                    { },
                    member.span
                };
                drive.delay = std::move(skew_delay);
                drive.span = member.span;
                if (selected_skew->edge
                    != frontend::EdgeKind::Any) {
                    frontend::Process driver;
                    driver.kind = frontend::ProcessKind::VerilogAlways;
                    driver.name = "$clocking$" + block.name
                        + "$" + member.name + "$drive";
                    driver.sensitivities = block.event;
                    driver.sensitivities.front().edge = selected_skew->edge;
                    driver.statements.push_back(std::move(drive));
                    driver.span = member.span;
                    clocking_event_processes.push_back(
                        { std::move(driver), false });
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

            const frontend::SignalDeclaration sampled {
                member_name,
                *type->second,
                frontend::PortDirection::Unknown,
                false,
                member.span
            };
            const auto sample = add_owned_signal(sampled, path, local);
            if (!sample)
                continue;
            visible_types.insert_or_assign(
                member_name, type->second);
            visible_types.insert_or_assign(
                path + "." + member_name, type->second);

            std::string sample_source = actual_name;
            if (skew_delay && skew_delay->magnitude != 0) {
                const auto skew_name = "$clocking$"
                    + block.name + "$" + member.name + "$skew";
                const frontend::SignalDeclaration delayed {
                    skew_name,
                    *type->second,
                    frontend::PortDirection::Unknown,
                    false,
                    member.span
                };
                const auto delayed_signal = add_owned_signal(delayed, path, local);
                if (!delayed_signal)
                    continue;
                visible_types.insert_or_assign(
                    skew_name, type->second);
                visible_types.insert_or_assign(
                    path + "." + skew_name, type->second);
                frontend::Statement history;
                history.kind = frontend::StatementKind::Assignment;
                history.assignment_kind = frontend::AssignmentKind::Blocking;
                history.label = "$clocking$" + block.name + "$"
                    + member.name + "$history";
                history.target = frontend::Expression {
                    frontend::ExpressionKind::Identifier,
                    skew_name,
                    { },
                    member.span
                };
                history.value = member.expression.value_or(
                    frontend::Expression {
                        frontend::ExpressionKind::Identifier,
                        member.name,
                        { },
                        member.span });
                history.delay = std::move(skew_delay);
                history.span = member.span;
                clocking_skew_statements.push_back(
                    std::move(history));
                sample_source = skew_name;
            }
            frontend::Statement assignment;
            assignment.kind = frontend::StatementKind::Assignment;
            assignment.assignment_kind = frontend::AssignmentKind::Blocking;
            assignment.target = frontend::Expression {
                frontend::ExpressionKind::Identifier,
                member_name,
                { },
                member.span
            };
            assignment.value = frontend::Expression {
                frontend::ExpressionKind::Identifier,
                std::move(sample_source),
                { },
                member.span
            };
            assignment.span = member.span;
            frontend::Process sampler;
            sampler.kind = frontend::ProcessKind::VerilogAlways;
            sampler.name = "$clocking$" + block.name + "$"
                + member.name + "$sample";
            sampler.sensitivities = block.event;
            if (selected_skew
                && selected_skew->edge
                    != frontend::EdgeKind::Any) {
                sampler.sensitivities.front().edge = selected_skew->edge;
            }
            sampler.statements.push_back(std::move(assignment));
            sampler.span = member.span;
            clocking_event_processes.push_back(
                { std::move(sampler), true });
        }
    }

    const auto specialization_index = design_.specializations_.size();
    const auto specialization_id = static_cast<SpecializationId>(specialization_index);
    if (static_cast<std::size_t>(specialization_id)
        != specialization_index) {
        throw std::length_error(
            "too many elaborated design-unit specializations");
    }
    const auto program_owner
        = unit.kind == frontend::UnitKind::SystemVerilogProgram
        ? std::optional<std::uint32_t> { specialization_id }
        : std::nullopt;
    SpecializationInfo specialization;
    specialization.id = specialization_id;
    specialization.unit = identity;
    specialization.instance = path;
    specialization.source = std::string { frontend::physical_source(unit.span) };
    if (unit.kind == frontend::UnitKind::VhdlArchitecture) {
        if (const auto* entity = find_vhdl_entity(parsed_, unit);
            entity != nullptr
            && frontend::physical_source(entity->span)
                != specialization.source) {
            specialization.source_dependencies.push_back(
                std::string {
                    frontend::physical_source(entity->span) });
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
    specialization.library = unit.library.empty() ? "work" : unit.library;
    specialization.is_cell = unit.is_cell;
    specialization.parameter_values = std::move(parameter_values);
    specialization.parameter_identity_values = std::move(parameter_identity_values);
    add_systemverilog_alias_connections(
        systemverilog_alias_plan,
        path,
        local,
        specialization);

    const auto specify_path_begin = design_.verilog_specify_paths_.size();
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
                    statement.sensitivities = default_clocking->event;
                    statement.procedural_assignment_repeat = true;
                    statement.loop_limit = statement.clocking_cycle_count;
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

    Lowerer lowerer {
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
        systemverilog_scalar_evaluation_context(unit),
        diagnostics_
    };
    lowerer.set_systemverilog_program_owner(program_owner);
    lowerer.set_vhdl_standard(unit.vhdl_standard);
    const auto uses_synopsys_package = [&](const std::string_view package) {
        const auto prefix = "ieee." + std::string { package };
        return std::ranges::any_of(
            unit.vhdl_context,
            [&](const frontend::VhdlContextItem& item) {
                return item.kind == frontend::VhdlContextItemKind::UseClause
                    && std::ranges::any_of(
                        item.selected_names,
                        [&](const std::string& selected) {
                            return selected == prefix
                                || selected.starts_with(prefix + ".");
                        });
            });
    };
    lowerer.set_vhdl_synopsys_numeric_context(
        uses_synopsys_package("std_logic_signed"),
        uses_synopsys_package("std_logic_unsigned"));
    const auto append_profiled_process =
        [&](Process process) {
            if (unit.language == frontend::Language::Vhdl2008) {
                process.language_standard = frontend::to_string(unit.vhdl_standard);
                process.compatibility_profile = unit.vhdl_compatibility_profile;
            }
            canonicalize_process_operations(process);
            specialization.processes.push_back(process.id);
            design_.processes_.push_back(std::move(process));
        };
    for (std::size_t index = 0;
        index < clocking_skew_statements.size(); ++index) {
        auto process = lowerer.lower_concurrent(
            clocking_skew_statements[index],
            unit.language,
            path,
            unit.concurrent_statements.size() + index);
        append_profiled_process(std::move(process));
        for (auto& generated :
            lowerer.take_generated_processes()) {
            generated.reactive = program_owner.has_value();
            generated.program_owner = program_owner;
            append_profiled_process(std::move(generated));
        }
    }
    for (const auto& event_process : clocking_event_processes) {
        auto lowered = lowerer.lower_process(
            event_process.process, unit.language, path);
        lowered.observed = event_process.observed;
        append_profiled_process(std::move(lowered));
        for (auto& generated : lowerer.take_generated_processes()) {
            generated.reactive = program_owner.has_value();
            generated.program_owner = program_owner;
            append_profiled_process(std::move(generated));
        }
    }
    const auto append_generated_processes = [&] {
        for (auto& generated : lowerer.take_generated_processes()) {
            generated.reactive = program_owner.has_value();
            generated.program_owner = program_owner;
            append_profiled_process(std::move(generated));
        }
    };
    const auto fusion_eligible = [&](const frontend::Statement& statement) {
        return statement.kind == frontend::StatementKind::Assignment
            && statement.assignment_kind
                == frontend::AssignmentKind::Continuous
            && statement.label != "$port_input_driver"
            && !statement.delay
            && (unit.language != frontend::Language::Vhdl2008
                || (statement.vhdl_waveform.size() == 1U
                    && !statement.vhdl_waveform.front().disconnect
                    && !statement.vhdl_unaffected))
            && !statement.verilog_drive_strength
            && !statement.verilog_switch_driver
            && !statement.vhdl_guarded_assignment
            && !statement.vhdl_postponed;
    };
    const auto exact_conditional_fusion_eligible
        = [&](const auto& self, const frontend::Statement& statement)
        -> bool {
        if (fusion_eligible(statement)) {
            return true;
        }
        if (unit.language != frontend::Language::Vhdl2008
            || statement.kind != frontend::StatementKind::If
            || !statement.vhdl_conditional_assignment
            || statement.vhdl_guarded_assignment
            || statement.vhdl_postponed
            || statement.statements.empty()
            || statement.else_statements.empty()) {
            return false;
        }
        return std::ranges::all_of(
                   statement.statements,
                   [&](const frontend::Statement& nested) {
                       return self(self, nested);
                   })
            && std::ranges::all_of(
                statement.else_statements,
                [&](const frontend::Statement& nested) {
                    return self(self, nested);
                });
    };
    const auto fusion_assignment = [&](const auto& self,
                                       const frontend::Statement& statement)
        -> const frontend::Statement* {
        if (statement.kind == frontend::StatementKind::Assignment) {
            return &statement;
        }
        for (const auto& nested : statement.statements) {
            if (const auto* assignment = self(self, nested)) {
                return assignment;
            }
        }
        for (const auto& nested : statement.else_statements) {
            if (const auto* assignment = self(self, nested)) {
                return assignment;
            }
        }
        return nullptr;
    };
    const auto fusion_target = [&](const frontend::Statement& statement)
        -> std::optional<SignalId> {
        const auto* assignment = fusion_assignment(
            fusion_assignment, statement);
        if (assignment == nullptr) {
            return std::nullopt;
        }
        const auto* target = &assignment->target;
        while ((target->kind == frontend::ExpressionKind::Index
                   || target->kind == frontend::ExpressionKind::Slice)
            && !target->operands.empty()) {
            target = &target->operands.front();
        }
        if (target->kind != frontend::ExpressionKind::Identifier) {
            return std::nullopt;
        }
        const auto found = local.find(target->text);
        return found == local.end()
            ? std::nullopt
            : std::optional<SignalId> { found->second };
    };
    constexpr std::size_t minimum_fused_group_size = 8U;
    const std::span<const frontend::Statement> concurrent_statements {
        unit.concurrent_statements
    };

    struct VhdlFusionBank {
        std::vector<std::size_t> members;
        std::unordered_set<SignalId> targets;
        std::vector<SignalId> sensitivity;
        bool exact_sensitivity { };
    };
    std::vector<VhdlFusionBank> vhdl_fusion_banks;
    std::vector<std::optional<std::size_t>> vhdl_fusion_bank_by_statement(
        concurrent_statements.size());
    if (unit.language == frontend::Language::Vhdl2008
        && std::getenv("FSIM_DISABLE_CONTINUOUS_FUSION") == nullptr) {
        constexpr std::size_t maximum_bank_members = 64U;
        constexpr std::size_t maximum_bank_sensitivity = 63U;
        for (std::size_t statement = 0U;
             statement < concurrent_statements.size(); ++statement) {
            const auto& source = concurrent_statements[statement];
            const bool eligible = fusion_eligible(source);
            const bool exact_conditional
                = !eligible
                && exact_conditional_fusion_eligible(
                    exact_conditional_fusion_eligible, source);
            const bool trigger_safe = (eligible || exact_conditional)
                && lowerer.concurrent_trigger_fusion_safe(source);
            if ((!eligible && !exact_conditional) || !trigger_safe) {
                continue;
            }
            const auto target = fusion_target(source);
            const auto sensitivity = lowerer.concurrent_sensitivity(source);
            if (!target || sensitivity.empty()) {
                continue;
            }
            std::optional<std::size_t> selected_bank;
            std::vector<SignalId> selected_sensitivity;
            for (std::size_t bank = 0U;
                 bank < vhdl_fusion_banks.size(); ++bank) {
                const auto& candidate = vhdl_fusion_banks[bank];
                if (candidate.members.size() >= maximum_bank_members
                    || candidate.targets.contains(*target)
                    || candidate.exact_sensitivity
                        != exact_conditional) {
                    continue;
                }
                if (exact_conditional
                    && candidate.sensitivity != sensitivity) {
                    continue;
                }
                std::vector<SignalId> combined;
                if (exact_conditional) {
                    combined = sensitivity;
                } else {
                    std::ranges::set_union(
                        candidate.sensitivity, sensitivity,
                        std::back_inserter(combined));
                }
                if (combined.size() > maximum_bank_sensitivity) {
                    continue;
                }
                selected_bank = bank;
                selected_sensitivity = std::move(combined);
                break;
            }
            if (!selected_bank) {
                vhdl_fusion_banks.push_back(VhdlFusionBank {
                    { }, { }, sensitivity, exact_conditional
                });
                selected_bank = vhdl_fusion_banks.size() - 1U;
            } else {
                vhdl_fusion_banks[*selected_bank].sensitivity
                    = std::move(selected_sensitivity);
            }
            auto& bank = vhdl_fusion_banks[*selected_bank];
            bank.targets.insert(*target);
            bank.members.push_back(statement);
            vhdl_fusion_bank_by_statement[statement]
                = *selected_bank;
        }
        for (std::size_t bank = 0U; bank < vhdl_fusion_banks.size(); ++bank) {
            if (vhdl_fusion_banks[bank].members.size()
                >= minimum_fused_group_size) {
                continue;
            }
            for (const auto statement : vhdl_fusion_banks[bank].members) {
                vhdl_fusion_bank_by_statement[statement].reset();
            }
        }
    }

    if (unit.language == frontend::Language::Vhdl2008) {
        for (std::size_t index = 0U;
             index < concurrent_statements.size(); ++index) {
            const auto bank_id = vhdl_fusion_bank_by_statement[index];
            if (!bank_id) {
                append_profiled_process(lowerer.lower_concurrent(
                    concurrent_statements[index], unit.language, path, index));
                append_generated_processes();
                continue;
            }
            const auto& bank = vhdl_fusion_banks[*bank_id];
            if (bank.members.front() == index) {
                std::vector<frontend::Statement> statements;
                statements.reserve(bank.members.size());
                for (const auto member : bank.members) {
                    statements.push_back(concurrent_statements[member]);
                }
                auto lowered = lowerer.lower_concurrent_group(
                    statements, unit.language, path, index);
                if (bank.exact_sensitivity) {
                    lowered.static_trigger_regions.clear();
                }
                append_profiled_process(std::move(lowered));
                append_generated_processes();
                continue;
            }
            // Retain the vacated process identity so later process handles and
            // per-process random streams are invariant under banking.
            Process placeholder;
            placeholder.id = static_cast<ProcessId>(design_.processes_.size());
            placeholder.name = path + ".concurrent_fused_slot_"
                + std::to_string(index);
            placeholder.operations.emplace_back(Halt { });
            append_profiled_process(std::move(placeholder));
        }
    } else {

    for (std::size_t index = 0; index < concurrent_statements.size();) {
        std::size_t end = index;
        while (end < concurrent_statements.size()
            && fusion_eligible(concurrent_statements[end])) {
            ++end;
        }
        if (end - index >= minimum_fused_group_size
            && std::getenv("FSIM_DISABLE_CONTINUOUS_FUSION") == nullptr) {
            const auto run
                = concurrent_statements.subspan(index, end - index);
            append_profiled_process(lowerer.lower_concurrent_group(
                run, unit.language, path, index));
            append_generated_processes();
            index = end;
            continue;
        }
        const auto individual_end = end == index ? index + 1U : end;
        for (; index < individual_end; ++index) {
            auto process = lowerer.lower_concurrent(
                concurrent_statements[index],
                unit.language,
                path,
                index);
            append_profiled_process(std::move(process));
            append_generated_processes();
        }
    }
    }
    for (const auto& source_process : unit.processes) {
        auto process = source_process;
        const bool concurrent_assertion = process.statements.size() == 1U
            && process.statements.front().kind
                == frontend::StatementKind::Assert
            && process.statements.front().assertion_message.starts_with(
                "concurrent assertion '");
        prepare_clocking_cycle_waits(
            prepare_clocking_cycle_waits, process.statements);
        auto lowered = lowerer.lower_process(process, unit.language, path);
        lowered.observed = concurrent_assertion;
        lowered.reactive = !concurrent_assertion
            && unit.kind == frontend::UnitKind::SystemVerilogProgram;
        lowered.program_owner = program_owner;
        append_profiled_process(std::move(lowered));
        for (auto& generated : lowerer.take_generated_processes()) {
            generated.reactive = program_owner.has_value();
            generated.program_owner = program_owner;
            append_profiled_process(std::move(generated));
        }
    }
    // These frontend bodies have been fully lowered.  Child binding still
    // needs the unit's declarations, callables, configurations, and instance
    // inventory, but retaining consumed process syntax only overlaps it with
    // the growing runtime design.
    {
        decltype(unit.concurrent_statements) empty;
        unit.concurrent_statements.swap(empty);
    }
    {
        decltype(unit.processes) empty;
        unit.processes.swap(empty);
    }
    const auto regions_overlap = [](
                                     const Process::DriverRegion& region,
                                     const VerilogSpecifyTerminalInfo& terminal) {
        if (region.signal != terminal.signal)
            return false;
        if (region.whole)
            return true;
        const auto region_end = static_cast<std::uint64_t>(region.offset) + region.width;
        const auto terminal_end = static_cast<std::uint64_t>(terminal.offset)
            + terminal.width;
        return region.offset < terminal_end
            && terminal.offset < region_end;
    };
    for (auto path_index = specify_path_begin;
        path_index < design_.verilog_specify_paths_.size();
        ++path_index) {
        auto& specify_path = design_.verilog_specify_paths_[path_index];
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
    auto effective_instances = unit.instances;
    auto bound_instances = systemverilog_bound_instances(
        unit, path, parameter_environment, parent_domains);
    for (auto& bound_instance : bound_instances) {
        if (std::ranges::any_of(
                effective_instances,
                [&](const frontend::Instance& existing) {
                    return existing.name == bound_instance.name;
                })) {
            report(
                "FSIM-ELAB-SVBIND-001",
                "bound instance name '" + bound_instance.name
                    + "' collides in scope '" + path + "'",
                bound_instance.span);
            continue;
        }
        effective_instances.push_back(std::move(bound_instance));
    }
    for (const auto& instance : effective_instances) {
        const auto child_path = path + "." + instance.name;
        std::vector<std::pair<ResolvedVerilogDefparam*, std::size_t>>
            child_defparams;
        for (auto& declaration : resolved_defparams) {
            const auto prefix = defparam_instance_prefix(
                declaration.segments, instance.name);
            if (!prefix) {
                continue;
            }
            declaration.matched = true;
            child_defparams.emplace_back(
                &declaration, *prefix);
        }
        const auto checkpoint = hierarchy_checkpoint(child_path);
        const ScopeExit rollback_failed_child { [&, checkpoint] {
            if (diagnostics_.size() != checkpoint.diagnostics) {
                rollback_hierarchy(checkpoint);
            }
        } };
        const auto* binding = binding_for(child_path);
        const auto build_systemc =
            [&](const frontend::Instance& selected_instance,
                const std::string_view selected_target) {
                const auto* description = construct_systemc_description(
                    selected_instance,
                    child_path,
                    selected_target,
                    parameter_environment,
                    unit.language);
                if (description == nullptr) {
                    return;
                }
                auto [child_aliases, child_objects] = connect_systemc_instance(
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
                if (!child_defparams.empty()) {
                    report(
                        "FSIM-ELAB-DEFPARAM-004",
                        "defparam cannot target SystemC instance '"
                            + child_path + "'",
                        child_defparams.front()
                            .first->declaration->span);
                    continue;
                }
                build_systemc(instance, *binding->target);
                continue;
            }
        }
        ConfiguredVhdlInstance configured;
        ConfiguredSystemVerilogInstance systemverilog_configured;
        const frontend::Instance* selected_instance = &instance;
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
                    if (!child_defparams.empty()) {
                        report(
                            "FSIM-ELAB-DEFPARAM-004",
                            "defparam cannot target SystemC instance '"
                                + child_path + "'",
                            child_defparams.front()
                                .first->declaration->span);
                        continue;
                    }
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
        if (target == nullptr
            && (binding == nullptr || !binding->target.has_value())) {
            systemverilog_configured = configure_systemverilog_instance(
                unit, *selected_instance, child_path);
            if (!systemverilog_configured.valid) {
                continue;
            }
            if (systemverilog_configured.applied) {
                target = systemverilog_configured.target;
            }
        }
        if (target == nullptr) {
            if ((binding == nullptr
                    || !binding->target.has_value())
                && selected_instance->unit_name.find_first_of(".(")
                    == std::string::npos) {
                const auto bound_library = systemverilog_bound_instance_libraries_.find(child_path);
                const auto library = bound_library
                        != systemverilog_bound_instance_libraries_.end()
                    ? bound_library->second
                    : (unit.library.empty() ? std::string { "work" }
                                            : unit.library);
                const auto inferred = inferred_target(
                    library,
                    selected_instance->unit_name,
                    child_path,
                    selected_instance->span);
                if (!inferred.has_value()) {
                    continue;
                }
                if (inferred->systemc_target.has_value()) {
                    if (!child_defparams.empty()) {
                        report(
                            "FSIM-ELAB-DEFPARAM-004",
                            "defparam cannot target SystemC instance '"
                                + child_path + "'",
                            child_defparams.front()
                                .first->declaration->span);
                        continue;
                    }
                    build_systemc(
                        *selected_instance,
                        *inferred->systemc_target);
                    continue;
                }
                if (inferred->udp != nullptr) {
                    if (!child_defparams.empty()) {
                        report(
                            "FSIM-ELAB-DEFPARAM-004",
                            "defparam cannot target UDP instance '"
                                + child_path + "'",
                            child_defparams.front()
                                .first->declaration->span);
                        continue;
                    }
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
        if (target == nullptr)
            continue;
        if (selected_instance->anonymous) {
            report("FSIM-ELAB-BIND-062", "module instance '" + child_path + "' requires an explicit instance name", selected_instance->span);
            continue;
        }
        if (selected_instance->udp_delay
            || selected_instance->drive_strength) {
            const bool delay = selected_instance->udp_delay.has_value();
            report(delay ? "FSIM-ELAB-BIND-063" : "FSIM-ELAB-BIND-065",
                "module instance '" + child_path + "' cannot use UDP "
                    + (delay ? "propagation-delay" : "drive-strength")
                    + " syntax",
                selected_instance->span);
            continue;
        }
        if (!child_defparams.empty()
            && target->language != frontend::Language::Verilog2005
            && target->language
                != frontend::Language::SystemVerilog2017) {
            report(
                "FSIM-ELAB-DEFPARAM-004",
                "defparam cannot cross into non-Verilog instance '"
                    + child_path + "'",
                child_defparams.front().first->declaration->span);
            continue;
        }
        auto defparam_instance = *selected_instance;
        std::vector<frontend::VerilogDefparamDeclaration>
            descendant_defparams;
        bool valid_defparams = true;
        for (const auto& [resolved, consumed] : child_defparams) {
            if (consumed + 1U == resolved->segments.size()) {
                const auto& parameter_name = resolved->segments.back();
                const bool duplicate = std::ranges::any_of(
                    defparam_instance.parameter_overrides,
                    [&](const auto& override) {
                        return override.name
                            && *override.name == parameter_name;
                    });
                if (duplicate) {
                    report(
                        "FSIM-ELAB-DEFPARAM-003",
                        "duplicate or conflicting override for defparam "
                        "target '"
                            + instance.name + "." + parameter_name
                            + "'",
                        resolved->declaration->span);
                    valid_defparams = false;
                    continue;
                }
                defparam_instance.parameter_overrides.emplace_back(
                    parameter_name,
                    resolved->declaration->value,
                    resolved->declaration->span);
                continue;
            }
            frontend::VerilogDefparamDeclaration descendant;
            descendant.value = resolved->declaration->value;
            descendant.span = resolved->declaration->span;
            for (std::size_t index = consumed;
                index < resolved->segments.size(); ++index) {
                frontend::VerilogDefparamPathSegment segment;
                segment.name = resolved->segments[index];
                segment.span = resolved->declaration->span;
                descendant.path.push_back(std::move(segment));
            }
            descendant_defparams.push_back(std::move(descendant));
        }
        if (!valid_defparams) {
            continue;
        }
        selected_instance = &defparam_instance;
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
        child_specialized.unit.verilog_defparams.insert(
            child_specialized.unit.verilog_defparams.end(),
            std::make_move_iterator(descendant_defparams.begin()),
            std::make_move_iterator(descendant_defparams.end()));
        adapt_vhdl_unspecified_port_types(
            child_specialized.unit,
            *selected_instance,
            local,
            design_.signals(),
            child_specialized.identity_values,
            diagnostics_);
        adapt_vhdl_array_port_shapes(
            child_specialized.unit,
            *selected_instance,
            local,
            design_.signals(),
            child_specialized.identity_values);
        if (!configured.component_identity.empty()) {
            if (child_specialized.identity_values.empty()) {
                child_specialized.identity_values = child_specialized.values;
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
        if (!systemverilog_configured.configuration_identity.empty()) {
            child_specialized.identity_values.emplace_back(
                "__configuration",
                systemverilog_configured.configuration_identity);
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
        for (auto& alias : child_aliases.vhdl_input_aliases) {
            child_specialized.unit.signals.push_back(std::move(alias));
        }
        for (auto& driver : child_aliases.vhdl_input_drivers) {
            child_specialized.unit.concurrent_statements.push_back(
                std::move(driver));
        }
        if (configured.referenced_configuration != nullptr) {
            vhdl_configurations_by_path_[child_path] = configured.referenced_configuration;
        }
        if (systemverilog_configured.referenced_configuration != nullptr) {
            systemverilog_configurations_by_path_[child_path] = systemverilog_configured.referenced_configuration;
        }
        auto interface_parameter_identities = target->kind
                == frontend::UnitKind::SystemVerilogInterface
            ? child_specialized.identity_values
            : std::vector<
                  std::pair<std::string, std::string>> { };
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
