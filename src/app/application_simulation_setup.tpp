// SPDX-License-Identifier: Apache-2.0

    Impl(
        BuiltProject project,
        const std::uint64_t max_deltas,
        const SimulationEngine engine)
        : built(std::move(project))
        , class_heap({ }, built.seed)
        , class_methods(class_heap, { }, &class_static_store)
        , uvm_objects(
              class_heap,
              [this](const std::string_view specialization,
                  const std::string_view declared,
                  const std::string_view) {
                  return construct_class(
                      specialization,
                      declared,
                      std::span<const runtime::PackedLogic4> { },
                      std::span<const std::string> { },
                      std::span<const std::string> { },
                      "$uvm-clone");
              })
        , uvm_components(class_heap, uvm_objects)
        , uvm_activity()
        , uvm_phases(uvm_components)
        , uvm_objections(uvm_objects, uvm_components, uvm_phases)
        , uvm_tlm1(class_heap, uvm_components)
        , uvm_tlm2(uvm_components)
        , uvm_sequences(
              class_heap, uvm_objects, uvm_components, uvm_phases,
              uvm_objections)
        , uvm_callbacks(uvm_objects, uvm_components)
        , uvm_transactions(uvm_objects, uvm_components, uvm_callbacks)
        , uvm_register_model(uvm_components)
        , uvm_foreign(
              uvm_phases, uvm_objections, uvm_tlm1, uvm_tlm2, uvm_activity)
        , uvm_registry(
              class_heap,
              uvm_objects,
              uvm_components,
              [this](const std::string_view specialization,
                  const std::string_view name) {
                  const auto handle = construct_class(
                      specialization,
                      class_specialization(specialization).declaration_identity,
                      std::span<const runtime::PackedLogic4> { },
                      std::span<const std::string> { },
                      std::span<const std::string> { },
                      "$uvm-registry");
                  uvm_objects.set_name(handle, std::string { name });
                  return handle;
              },
              [this](const std::string_view specialization,
                  const std::string_view name,
                  const runtime::SystemVerilogClassHandle parent,
                  const runtime::SystemVerilogUvmRootHandle root) {
                  std::array actuals {
                      runtime::PackedLogic4(64),
                      runtime::PackedLogic4::from_aval_bval(64, parent, 0)
                  };
                  std::array string_actuals {
                      std::string { name }, std::string { }
                  };
                  return construct_class(
                      specialization,
                      class_specialization(specialization).declaration_identity,
                      actuals,
                      string_actuals,
                      std::span<const std::string> { },
                      "$uvm-registry",
                      root);
              })
        , uvm_factory(uvm_registry)
        , uvm_resources(&class_heap)
        , uvm_synchronization(uvm_objects)
        , uvm_config_db(uvm_resources)
        , uvm_command_line(uvm_factory, uvm_resources, uvm_config_db)
        , uvm_test_runner(
              uvm_objects, uvm_components, uvm_factory, uvm_command_line)
        , uvm_reports(uvm_objects, uvm_components)
        , interpreter(built.design.create_interpreter(
              runtime::SchedulerOptions { max_deltas, 32 },
              built.seed))
    {
        uvm_phases.set_scheduler(interpreter->scheduler());
        uvm_synchronization.set_scheduler(interpreter->scheduler());
        uvm_activity.set_scheduler(interpreter->scheduler());
        uvm_phases.set_activity_service(uvm_activity);
        uvm_objections.set_scheduler(interpreter->scheduler());
        uvm_objections.set_activity_service(uvm_activity);
        uvm_phases.set_objection_service(uvm_objections);
        uvm_tlm1.set_scheduler(interpreter->scheduler());
        uvm_tlm1.set_activity_service(uvm_activity);
        uvm_tlm1.set_phase_service(uvm_phases);
        uvm_tlm2.set_scheduler(interpreter->scheduler());
        uvm_tlm2.set_activity_service(uvm_activity);
        uvm_sequences.set_activity_service(uvm_activity);
        uvm_sequences.set_role_services(uvm_tlm1, uvm_config_db);
        uvm_register_model.set_frontdoor_services(uvm_sequences, uvm_tlm1,
            uvm_phases);
        uvm_register_model.set_activity_service(uvm_activity);
        uvm_register_model.set_backdoor_transport({ [this](const auto, const std::string_view path)
                                                        -> std::optional<std::size_t> {
                                                       const auto signal = backdoor_signal(path);
                                                       return signal ? std::optional<std::size_t> {
                                                           interpreter->signal_value(*signal).width()
                                                       }
                                                                     : std::nullopt;
                                                   },
            [this](const auto, const std::string_view path) {
                const auto signal = backdoor_signal(path);
                if (!signal)
                    throw std::invalid_argument { "UVM HDL path does not resolve" };
                return interpreter->signal_value(*signal);
            },
            [this](const auto, const std::string_view path, const auto kind,
                const runtime::PackedLogic4& value) {
                const auto signal = backdoor_signal(path);
                if (!signal)
                    throw std::invalid_argument { "UVM HDL path does not resolve" };
                switch (kind) {
                case runtime::SystemVerilogUvmRegisterBackdoorKind::Deposit:
                    interpreter->deposit_signal(*signal, value);
                    return;
                case runtime::SystemVerilogUvmRegisterBackdoorKind::Force:
                    interpreter->force_signal(*signal, value);
                    return;
                case runtime::SystemVerilogUvmRegisterBackdoorKind::Release:
                    interpreter->release_signal(*signal);
                    return;
                case runtime::SystemVerilogUvmRegisterBackdoorKind::Read:
                    break;
                }
                throw std::invalid_argument { "invalid UVM HDL write operation" };
            } });
        uvm_callbacks.set_activity_service(uvm_activity);
        uvm_transactions.set_scheduler(interpreter->scheduler());
        uvm_transactions.set_activity_service(uvm_activity);
        uvm_foreign.set_scheduler(interpreter->scheduler());
        uvm_foreign.set_integrated_services(
            uvm_sequences, uvm_callbacks, uvm_transactions, uvm_register_model);
        (void)uvm_phases.create_standard_schedule();
        if (built.systemverilog_uvm_checkpoint) {
            const auto& saved = *built.systemverilog_uvm_checkpoint;
            const auto error = runtime::verify_systemverilog_uvm_checkpoint(
                saved, uvm_foreign, saved.provenance);
            if (error != runtime::SystemVerilogUvmCheckpointError::None) {
                throw std::logic_error {
                    "portable SystemVerilog UVM bootstrap state does not match "
                    "the fresh simulation"
                };
            }
        }
        register_systemverilog_uvm_object_types(
            built.systemverilog_class_specializations, uvm_objects);
        register_systemverilog_uvm_registry_types(
            built.systemverilog_class_specializations, uvm_registry);
        interpreter->set_class_allocate_hook(
            [this](const std::string_view scope,
                const std::string_view specialization,
                const std::string_view declared_type,
                const std::span<const runtime::PackedLogic4> actuals,
                const std::span<const std::string> string_actuals,
                const std::span<const std::string> actual_names) {
                return construct_class(
                    specialization, declared_type, actuals, string_actuals,
                    actual_names, scope);
            });
        interpreter->set_class_property_read_hook(
            [this](const std::uint64_t handle, const std::string_view property) {
                return packed_property_value(class_heap.property(handle, property));
            });
        interpreter->set_class_property_write_hook(
            [this](const std::uint64_t handle,
                const std::string_view property,
                const runtime::PackedLogic4& value) {
                const auto before = packed_class_snapshot();
                assign_property_value(
                    class_heap.property(handle, property), property, value);
                notify_class_changes(before);
            });
        interpreter->set_class_method_call_hook(
            [this](const std::uint64_t handle,
                const std::string_view method,
                std::vector<runtime::PackedLogic4>& actuals,
                std::vector<std::string>& string_actuals,
                const std::span<const std::string> names,
                const std::span<const std::uint8_t> directions,
                const bool virtual_dispatch) {
                const auto before = packed_class_snapshot();
                const auto static_before = packed_static_snapshot();
                auto result = method.starts_with("@container-")
                    ? invoke_class_container(handle, method, actuals)
                    : method.starts_with("@checked-cast:")
                    ? invoke_checked_class_cast(handle, method)
                    : method == "@builtin-randomize"
                    ? invoke_source_randomize(handle, names)
                    : method.starts_with("@builtin-rand-mode:")
                        || method.starts_with("@builtin-constraint-mode:")
                    ? invoke_source_randomization_mode(
                          handle, method, actuals)
                    : invoke_source_function(
                          handle, method, actuals, string_actuals,
                          names, directions,
                          virtual_dispatch);
                notify_class_changes(before);
                notify_static_changes(static_before);
                return result;
            });
        interpreter->set_class_static_property_read_hook(
            [this](const std::string_view property) {
                const auto [owner, name] = static_property_parts(property);
                return packed_property_value(
                    class_static_store.property(owner, name));
            });
        interpreter->set_class_static_property_write_hook(
            [this](const std::string_view property,
                const runtime::PackedLogic4& value) {
                const auto before = packed_static_snapshot();
                const auto [owner, name] = static_property_parts(property);
                assign_property_value(
                    class_static_store.property(owner, name), property, value);
                notify_static_changes(before);
            });
        interpreter->set_class_static_method_call_hook(
            [this](const std::string_view method,
                std::vector<runtime::PackedLogic4>& actuals,
                std::vector<std::string>& string_actuals,
                const std::span<const std::string> names,
                const std::span<const std::uint8_t> directions) {
                const auto class_before = packed_class_snapshot();
                const auto static_before = packed_static_snapshot();
                auto result = invoke_source_static_function(
                    method, actuals, string_actuals, names, directions);
                notify_class_changes(class_before);
                notify_static_changes(static_before);
                return result;
            });
        std::map<std::string, std::size_t> declaration_counts;
        for (const auto& specialization :
            built.systemverilog_class_specializations) {
            ++declaration_counts[specialization.declaration_identity];
        }
        for (const auto& specialization :
            built.systemverilog_class_specializations) {
            runtime::SystemVerilogClassStaticDescriptor descriptor;
            descriptor.specialization_identity = specialization.specialization_identity;
            descriptor.base_specialization_identity = specialization.base_specialization_identity;
            if (declaration_counts[specialization.declaration_identity] == 1
                && specialization.declaration_identity
                    != specialization.specialization_identity) {
                descriptor.aliases.push_back(specialization.declaration_identity);
            }
            for (const auto& property : specialization.properties) {
                if (property.is_static) {
                    descriptor.properties.push_back(
                        class_property_descriptor(property));
                }
            }
            class_static_store.register_specialization(std::move(descriptor));
        }
        class_static_store.initialize_all();
        interpreter->set_file_root(built.file_root);
        auto published_vpi
            = make_systemverilog_vpi_design(built, *interpreter);
        vpi_registry = std::move(published_vpi.registry);
        vpi_signal_handles = std::move(published_vpi.signals);
        vpi_handle_signals = std::move(published_vpi.handles);
        vpi_driver_bindings = std::move(published_vpi.drivers);
        vpi_event_handles = std::move(published_vpi.events);
        vpi_scalar_kinds = std::move(published_vpi.scalar_kinds);
        vpi_categories = std::move(published_vpi.categories);
        vpi_container_words = std::move(published_vpi.container_words);
        vpi_word_handles = std::move(published_vpi.word_handles);
        vpi_container_scalar_kinds
            = std::move(published_vpi.container_scalar_kinds);
        vpi_container_categories
            = std::move(published_vpi.container_categories);
        vpi_time = std::make_unique<runtime::SystemVerilogVpiTimeService>(
            interpreter->scheduler(),
            systemverilog_vpi_time_profile(built.time_resolution));
        constexpr runtime::StableOrder vpi_callback_order
            = runtime::StableOrder { 1 } << 60U;
        constexpr runtime::StableOrder vpi_value_order
            = runtime::StableOrder { 2 } << 60U;
        constexpr runtime::StableOrder vpi_control_order
            = runtime::StableOrder { 3 } << 60U;
        vpi_callbacks
            = std::make_unique<runtime::SystemVerilogVpiCallbackManager>(
                *vpi_registry,
                interpreter->scheduler(),
                *vpi_time,
                vpi_callback_order);
        vpi_values = std::make_unique<runtime::SystemVerilogVpiValueControl>(
            *vpi_registry, interpreter->scheduler(), vpi_value_order);
        vpi_control
            = std::make_unique<runtime::SystemVerilogVpiControlService>(
                *vpi_registry,
                interpreter->scheduler(),
                *vpi_callbacks,
                vpi_control_order,
                false,
                false);
        vpi_systems
            = std::make_unique<runtime::SystemVerilogVpiSystemRegistry>(
                *vpi_registry);
        vpi_value_state_observer = vpi_registry->add_value_state_observer(
            [this](const runtime::SystemVerilogVpiValueStateUpdate& update) {
                return apply_vpi_value_state(update);
            });
        if (!vpi_value_state_observer) {
            throw std::logic_error {
                "failed to connect the live VPI value bridge"
            };
        }
        vhdl_vhpi_registry = make_vhdl_debug_registry(built);
        vhdl_psl = std::make_unique<VhdlPslExecution>(built.vhdl_hir, built.design,
            built.design_ir,
            [this](const runtime::simir::SignalId signal) {
                return interpreter->signal_value(signal);
            });
        vhdl_psl->set_completion_hook(
            [this](const runtime::VhdlPslAttemptSnapshot& attempt) {
                if (vhdl_psl_attempt_hook) {
                    try {
                        vhdl_psl_attempt_hook(attempt);
                    } catch (...) {
                    }
                }
                ConcurrentAssertionEvent event;
                event.name = attempt.monitor;
                event.process = attempt.instance_identity;
                event.instance_identity = attempt.instance_identity;
                event.source_span = attempt.source_span;
                event.slot = attempt.slot;
                event.time = attempt.end_time;
                event.delta = attempt.end_delta;
                switch (attempt.directive_kind) {
                case runtime::VhdlPslDirectiveKind::assumption:
                    event.kind = ConcurrentAssertionCoverageKind::assumption;
                    break;
                case runtime::VhdlPslDirectiveKind::restriction:
                    event.kind = ConcurrentAssertionCoverageKind::restriction;
                    break;
                case runtime::VhdlPslDirectiveKind::cover:
                    event.kind = ConcurrentAssertionCoverageKind::cover;
                    break;
                case runtime::VhdlPslDirectiveKind::assertion:
                    event.kind = ConcurrentAssertionCoverageKind::assertion;
                    break;
                }
                switch (attempt.outcome) {
                case runtime::VhdlPslAttemptOutcome::pass:
                    event.outcome = ConcurrentAssertionOutcome::pass;
                    break;
                case runtime::VhdlPslAttemptOutcome::failure:
                    event.outcome = ConcurrentAssertionOutcome::failure;
                    break;
                case runtime::VhdlPslAttemptOutcome::vacuous:
                    event.outcome = ConcurrentAssertionOutcome::vacuous;
                    break;
                case runtime::VhdlPslAttemptOutcome::aborted:
                    event.outcome = ConcurrentAssertionOutcome::aborted;
                    break;
                case runtime::VhdlPslAttemptOutcome::pending:
                    return;
                }
                if (concurrent_assertion_hook) {
                    try {
                        concurrent_assertion_hook(event);
                    } catch (...) {
                    }
                }
                if (attempt.outcome != runtime::VhdlPslAttemptOutcome::failure
                    || attempt.directive_kind
                        == runtime::VhdlPslDirectiveKind::cover
                    || !report_hook) {
                    return;
                }
                auto kind = std::string_view { "assert" };
                auto severity = runtime::simir::AssertionSeverity::error;
                if (attempt.directive_kind
                    == runtime::VhdlPslDirectiveKind::assumption) {
                    kind = "assume";
                } else if (attempt.directive_kind
                    == runtime::VhdlPslDirectiveKind::restriction) {
                    kind = "restrict";
                    severity = runtime::simir::AssertionSeverity::warning;
                }
                try {
                    report_hook(std::numeric_limits<runtime::simir::ProcessId>::max(),
                        "VHDL PSL " + std::string { kind } + " '"
                            + attempt.monitor + "' failed",
                        severity,
                        runtime::simir::SourceLocation {
                            "vhdl-span:" + std::to_string(attempt.source_span),
                            1U, 1U },
                        attempt.end_time, attempt.end_delta);
                } catch (...) {
                }
            });
        const auto has_systemc_process = std::ranges::any_of(
            built.design_ir.boundaries(), [](const auto& boundary) {
                return boundary.kind
                    == semantic::design::BoundaryKind::systemc_process;
            });
        if (has_systemc_process && !built.systemc_hierarchy
            && built.systemc_hierarchies.empty()) {
            throw std::logic_error {
                "SystemC processes require their native hierarchy registry"
            };
        }
        for (const auto& boundary : built.design_ir.boundaries()) {
            if (boundary.kind
                    != semantic::design::BoundaryKind::systemc_process
                || !boundary.process) {
                continue;
            }
            const auto process = built.design_ir.processes()[boundary.process->value()].runtime_index;
            auto hierarchy = std::ranges::find_if(
                built.systemc_hierarchies,
                [&](const auto& candidate) {
                    return candidate->owns_handle(boundary.native_handle);
                });
            auto owner = hierarchy != built.systemc_hierarchies.end()
                ? *hierarchy
                : built.systemc_hierarchy;
            interpreter->set_process_executor(
                process,
                std::make_unique<SystemCProcessExecutor>(
                    std::move(owner),
                    boundary.native_handle));
        }
#if defined(FSIM_HAS_LLVM)
        if (engine != SimulationEngine::interpreter) {
            compiler::LlvmJitOptions options;
            options.optimization = engine == SimulationEngine::debug
                ? compiler::JitOptimizationLevel::o0
                : jit_optimization(built.optimization);
            if (!built.cache_path.empty()) {
                options.cache_directory = built.cache_path / "llvm-native";
            }
            jit = std::make_unique<compiler::LlvmJit>(std::move(options));

            signal_widths.reserve(built.design.signals().size());
            signal_value_kinds.reserve(
                built.design.signals().size());
            for (const auto& signal : built.design.signals()) {
                if (signal.width
                    > std::numeric_limits<std::uint32_t>::max()) {
                    signal_widths.push_back(
                        std::numeric_limits<std::uint32_t>::max());
                } else {
                    signal_widths.push_back(
                        static_cast<std::uint32_t>(signal.width));
                }
                signal_value_kinds.push_back(
                    signal.source_domain
                            == frontend::ValueDomain::Logic9
                        ? runtime::simir::ValueKind::logic9
                        : runtime::simir::ValueKind::logic4);
            }
            const auto& processes = built.design.processes();
            for (const auto& specialization : built.design_ir.specializations()) {
                if (specialization.language == semantic::Language::systemc) {
                    continue;
                }
                std::vector<const runtime::simir::Process*> selected;
                std::vector<std::string> symbols;
                selected.reserve(specialization.processes.size());
                symbols.reserve(specialization.processes.size());
                for (const auto process_id : specialization.processes) {
                    const auto runtime_id = built.design_ir.processes()[process_id.value()].runtime_index;
                    const auto& process = processes.at(runtime_id);
                    if (!jit->supports_process(
                            process,
                            signal_widths,
                            signal_value_kinds)) {
                        continue;
                    }
                    selected.push_back(&process);
                    symbols.push_back(
                        "fsim_process_" + std::to_string(process.id));
                }
                if (selected.empty()) {
                    continue;
                }

                std::vector<compiler::JitProcessModuleEntry> entries;
                entries.reserve(selected.size());
                for (std::size_t index = 0; index < selected.size(); ++index) {
                    entries.push_back({ symbols[index], selected[index] });
                }
                const auto module_identity = "fsim-specialization:" + std::to_string(specialization.id.value()) + ":" + specialization.name + "@" + built.design_ir.instances()[specialization.instance.value()].path + "#provenance=" + built.specialization_cache_keys.at(specialization.id.value())
                    + (built.artifact_identity.empty()
                            ? std::string { }
                            : "#artifact=" + built.artifact_identity);
                jit->add_process_module(
                    module_identity,
                    entries,
                    signal_widths,
                    signal_value_kinds);
                ++compiled_modules;
                for (std::size_t index = 0; index < selected.size(); ++index) {
                    const auto handle = jit->lookup(symbols[index]);
                    interpreter->set_process_executor(
                        selected[index]->id,
                        std::make_unique<LlvmProcessExecutor>(
                            *jit,
                            handle,
                            *selected[index],
                            signal_widths,
                            signal_value_kinds));
                    ++compiled_processes;
                }
            }
        }
#else
        (void)engine;
#endif
        interpreter->set_signal_change_hook(
            [this](
                const SignalId signal,
                const PackedLogic4& value,
                const SimulationTick time) {
                publish_vpi_signal(signal, value);
                if (signal_change_hook) {
                    signal_change_hook(
                        signal, value, time, interpreter->scheduler().delta());
                }
                if (!signal_observers.empty()) {
                    // Copy callbacks so observers may safely remove themselves while
                    // receiving a synchronous simulation-thread notification.
                    std::vector<SignalChangeHook> callbacks;
                    callbacks.reserve(signal_observers.size());
                    for (const auto& [token, callback] : signal_observers) {
                        (void)token;
                        callbacks.push_back(callback);
                    }
                    for (const auto& callback : callbacks) {
                        callback(signal, value, time, interpreter->scheduler().delta());
                    }
                }
            });
        interpreter->set_stored_signal_change_hook(
            [this](const SignalId signal, const SimulationTick) {
                publish_vpi_stored_signal(signal);
            });
        interpreter->set_driver_change_hook(
            [this](const runtime::simir::ProcessId process, const SignalId signal,
                const SimulationTick) {
                publish_vpi_driver(process, signal);
            });
        interpreter->set_event_trigger_hook(
            [this](const SignalId event, const SimulationTick) {
                publish_vpi_event(event);
            });
        interpreter->set_container_object_change_hook(
            [this](const runtime::simir::ContainerObjectId object,
                const SimulationTick) {
                publish_vpi_container(object);
            });
        interpreter->set_scalar_signal_change_hook(
            [this](
                const SignalId signal,
                const runtime::SystemVerilogScalarValue& value,
                const SimulationTick time) {
                if (scalar_signal_change_hook) {
                    scalar_signal_change_hook(
                        signal, value, time, interpreter->scheduler().delta());
                }
                std::vector<ScalarSignalChangeHook> callbacks;
                callbacks.reserve(scalar_signal_observers.size());
                for (const auto& [token, callback] : scalar_signal_observers) {
                    (void)token;
                    callbacks.push_back(callback);
                }
                for (const auto& callback : callbacks) {
                    callback(
                        signal, value, time, interpreter->scheduler().delta());
                }
            });
        interpreter->set_output_hook(
            [this](
                const runtime::simir::ProcessId process,
                const std::string_view text,
                const bool newline,
                const SimulationTick time,
                const std::uint64_t delta) {
                constexpr std::string_view assertion_marker {
                    "\x1f"
                    "fsim.concurrent-assertion|"
                };
                constexpr std::string_view control_marker {
                    "\x1f"
                    "fsim.assertion-control|"
                };
                if (text.starts_with(control_marker)) {
                    auto control = text.substr(control_marker.size());
                    if (control.starts_with("assertcontrol|")) {
                        std::uint32_t control_type { };
                        const auto value = control.substr(
                            std::string_view { "assertcontrol|" }.size());
                        const auto parsed = std::from_chars(
                            value.data(), value.data() + value.size(), control_type);
                        if (parsed.ec == std::errc { }) {
                            switch (control_type) {
                            case 3:
                                control = "asserton";
                                break;
                            case 4:
                                control = "assertoff";
                                break;
                            case 5:
                                control = "assertkill";
                                break;
                            case 6:
                                control = "assertpasson";
                                break;
                            case 7:
                                control = "assertpassoff";
                                break;
                            case 8:
                                control = "assertfailon";
                                break;
                            case 9:
                                control = "assertfailoff";
                                break;
                            case 10:
                                control = "assertnonvacuouson";
                                break;
                            case 11:
                                control = "assertvacuousoff";
                                break;
                            default:
                                break;
                            }
                        }
                    }
                    if (control == "asserton") {
                        concurrent_assertions_enabled = true;
                    } else if (control == "assertoff"
                        || control == "assertkill") {
                        concurrent_assertions_enabled = false;
                    } else if (control == "assertpasson"
                        || control == "assertnonvacuouson") {
                        concurrent_assertion_pass_actions_enabled = true;
                    } else if (control == "assertpassoff"
                        || control == "assertvacuousoff") {
                        concurrent_assertion_pass_actions_enabled = false;
                    } else if (control == "assertfailon") {
                        concurrent_assertion_failure_actions_enabled = true;
                    } else if (control == "assertfailoff") {
                        concurrent_assertion_failure_actions_enabled = false;
                    }
                    return;
                }
                if (text.starts_with(assertion_marker)) {
                    const auto payload = text.substr(assertion_marker.size());
                    const auto slot_end = payload.find('|');
                    const auto kind_end = slot_end == std::string_view::npos
                        ? std::string_view::npos
                        : payload.find('|', slot_end + 1U);
                    const auto outcome_end = kind_end == std::string_view::npos
                        ? std::string_view::npos
                        : payload.find('|', kind_end + 1U);
                    if (slot_end != std::string_view::npos
                        && kind_end != std::string_view::npos
                        && outcome_end != std::string_view::npos) {
                        const auto key = process;
                        const auto outcome = payload.substr(
                            kind_end + 1U,
                            outcome_end - kind_end - 1U);
                        concurrent_assertion_actions_suppressed[key] = !concurrent_assertions_enabled
                            || (outcome == "pass"
                                    ? !concurrent_assertion_pass_actions_enabled
                                    : !concurrent_assertion_failure_actions_enabled);
                        auto found = concurrent_assertion_indices.find(key);
                        if (found == concurrent_assertion_indices.end()) {
                            ConcurrentAssertionCoverage coverage;
                            const auto slot_text = payload.substr(0, slot_end);
                            std::from_chars(
                                slot_text.data(),
                                slot_text.data() + slot_text.size(),
                                coverage.slot);
                            const auto kind = payload.substr(
                                slot_end + 1U, kind_end - slot_end - 1U);
                            coverage.kind = kind == "assumption"
                                ? ConcurrentAssertionCoverageKind::assumption
                                : kind == "cover"
                                ? ConcurrentAssertionCoverageKind::cover
                                : kind == "restriction"
                                ? ConcurrentAssertionCoverageKind::restriction
                                : ConcurrentAssertionCoverageKind::assertion;
                            coverage.name = payload.substr(outcome_end + 1U);
                            const auto occurrence = std::ranges::find_if(
                                built.design_ir.processes(),
                                [&](const auto& item) {
                                    return item.runtime_index == key;
                                });
                            coverage.process = occurrence != built.design_ir.processes().end()
                                ? occurrence->name
                                : coverage.name;
                            concurrent_assertion_coverage.push_back(
                                std::move(coverage));
                            const auto inserted = concurrent_assertion_coverage.size() - 1U;
                            concurrent_assertion_indices.emplace(key, inserted);
                            found = concurrent_assertion_indices.find(key);
                        }
                        auto& coverage = concurrent_assertion_coverage[found->second];
                        ConcurrentAssertionEvent event;
                        event.name = coverage.name;
                        event.process = coverage.process;
                        event.kind = coverage.kind;
                        event.slot = coverage.slot;
                        event.time = time;
                        event.delta = delta;
                        event.action_suppressed = concurrent_assertion_actions_suppressed[key];
                        if (!concurrent_assertions_enabled) {
                            event.outcome = ConcurrentAssertionOutcome::disabled;
                        } else {
                            ++coverage.attempts;
                            if (outcome == "pass") {
                                event.outcome = ConcurrentAssertionOutcome::pass;
                                ++coverage.passes;
                            } else {
                                event.outcome = ConcurrentAssertionOutcome::failure;
                                ++coverage.failures;
                            }
                        }
                        concurrent_assertion_events.push_back(std::move(event));
                        if (concurrent_assertion_hook) {
                            concurrent_assertion_hook(
                                concurrent_assertion_events.back());
                        }
                    }
                    return;
                }
                if (concurrent_assertion_actions_suppressed[process]) {
                    return;
                }
                if (output_hook) {
                    output_hook(process, text, newline, time, delta);
                }
            });
        interpreter->set_report_hook(
            [this](
                const runtime::simir::ProcessId process,
                const std::string_view message,
                const runtime::simir::AssertionSeverity severity,
                const runtime::simir::SourceLocation& source,
                const SimulationTick time,
                const std::uint64_t delta) {
                if (concurrent_assertion_actions_suppressed[process]) {
                    return;
                }
                if (report_hook) {
                    report_hook(
                        process,
                        message,
                        severity,
                        source,
                        time,
                        delta);
                }
            });
        interpreter->scheduler().set_safe_point_hook(
            [this](
                runtime::Scheduler& scheduler,
                const runtime::SchedulerPhase phase) {
                if (phase == runtime::SchedulerPhase::update) {
                    vhdl_psl->observe(scheduler.now(), scheduler.delta());
                }
                if (safe_point_hook) {
                    safe_point_hook(scheduler, phase);
                }
                if (safe_point_observers.empty()) {
                    return;
                }
                // Observers may synchronously remove themselves.
                std::vector<SafePointHook> callbacks;
                callbacks.reserve(safe_point_observers.size());
                for (const auto& [token, callback] : safe_point_observers) {
                    (void)token;
                    callbacks.push_back(callback);
                }
                for (const auto& callback : callbacks) {
                    callback(scheduler, phase);
                }
            });
    }
