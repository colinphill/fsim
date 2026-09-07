// SPDX-License-Identifier: Apache-2.0

Impl(
    BuiltProject project,
    const std::uint64_t max_deltas,
    const SimulationEngine engine,
    const SystemVerilogVpiRuntimeUpdates vpi_runtime_updates)
    : built(std::move(project))
    , vpi_runtime_updates_enabled(
          vpi_runtime_updates == SystemVerilogVpiRuntimeUpdates::enabled)
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
    const auto resolution = application_detail::magnitude_and_unit(
        built.time_resolution);
    const auto factor = resolution
        ? application_detail::unit_femtoseconds(resolution->unit)
        : std::nullopt;
    if (!resolution || !factor || resolution->magnitude == 0
        || resolution->magnitude
            > std::numeric_limits<std::uint64_t>::max() / *factor) {
        throw std::logic_error {
            "built project has an invalid time resolution"
        };
    }
    interpreter->set_time_resolution_femtoseconds(
        resolution->magnitude * *factor);
    interpreter->set_system_command_hook(
        [](const std::optional<std::string_view> command) {
            if (!command) {
                return static_cast<std::int32_t>(std::system(nullptr));
            }
            const auto owned = std::string { *command };
            return static_cast<std::int32_t>(
                std::system(owned.c_str()));
        });
    if (built.code_coverage_enabled) {
        const auto& inventory = built.design.code_coverage_inventory();
        std::size_t counter_count
            = inventory ? inventory->total_points : 0U;
        if (!inventory) {
            for (const auto& process_info : built.design_ir.processes()) {
                const auto& process = interpreter->process_program(
                    process_info.runtime_index);
                for (std::size_t instruction = 0U;
                    instruction < process.operations.size(); ++instruction) {
                    const auto* hit = runtime::simir::operation_get_if<
                        runtime::simir::CodeCoverageHit>(
                        &process.operations[instruction]);
                    if (hit == nullptr) {
                        continue;
                    }
                    const auto counter
                        = process.operations.code_coverage_counter(
                            instruction, hit->counter);
                    counter_count = std::max(
                        counter_count,
                        static_cast<std::size_t>(counter.value) + 1U);
                }
            }
        }
        interpreter->set_code_coverage_counters(
            std::vector<std::uint64_t>(counter_count));
    }
    coverage_execution_mode = engine == SimulationEngine::interpreter
        ? frontend::SystemVerilogCoverageExecutionMode::Interpreter
        : built.optimization == fsim::project::Optimization::o0
        ? frontend::SystemVerilogCoverageExecutionMode::LlvmO0
        : frontend::SystemVerilogCoverageExecutionMode::LlvmO2;
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
            const std::span<const runtime::SystemVerilogConstraintTemplate>
                inline_constraints,
            const bool virtual_dispatch) {
            const auto before = packed_class_snapshot();
            const auto static_before = packed_static_snapshot();
            auto result = method.starts_with("@container-")
                ? invoke_class_container(handle, method, actuals)
                : method.starts_with("@checked-cast:")
                ? invoke_checked_class_cast(handle, method)
                : method == "@builtin-randomize"
                ? invoke_source_randomize(
                      handle, names, inline_constraints)
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
    interpreter->set_coverage_sample_hook(
        [this](const std::string_view identity,
            const std::span<const runtime::PackedLogic4> actuals,
            const std::span<const std::uint8_t> signed_actuals,
            const runtime::simir::CoverageSampleTrigger trigger) {
            auto& coverage = built.systemverilog_coverage;
            auto instance = std::ranges::find(
                coverage.instances, identity,
                &frontend::SystemVerilogCovergroupInstance::runtime_identity);
            if (instance == coverage.instances.end()) {
                throw std::out_of_range {
                    "SystemVerilog covergroup instance '"
                    + std::string { identity } + "' is unavailable"
                };
            }
            if (stopped_covergroups.contains(std::string { identity })) {
                return;
            }
            const auto declaration = std::ranges::find(
                coverage.declarations, instance->declaration_identity,
                &frontend::SystemVerilogCovergroupDeclaration::canonical_identity);
            if (declaration == coverage.declarations.end()) {
                throw std::out_of_range {
                    "SystemVerilog covergroup declaration '"
                    + instance->declaration_identity + "' is unavailable"
                };
            }
            const auto procedural = trigger
                == runtime::simir::CoverageSampleTrigger::procedural;
            if (procedural
                && (!declaration->sampling
                    || declaration->sampling->kind
                        != frontend::SystemVerilogCovergroupSamplingKind::WithFunctionSample)) {
                throw std::invalid_argument {
                    "procedural covergroup sampling requires a resolved sample profile"
                };
            }
            const auto expected_actuals = procedural
                ? declaration->sampling->formals.size()
                : static_cast<std::size_t>(std::ranges::count_if(
                      declaration->coverage_declarations,
                      [](const auto& item) {
                          return item.kind
                              == frontend::SystemVerilogCoverageDeclarationKind::Coverpoint;
                      }));
            if (actuals.size() != expected_actuals
                || signed_actuals.size() != actuals.size()) {
                throw std::invalid_argument {
                    "covergroup sample actuals do not match the resolved "
                    "sample profile"
                };
            }
            std::vector<frontend::SystemVerilogCovergroupSampleInput> inputs;
            inputs.reserve(declaration->coverage_declarations.size());
            std::size_t coverpoint_index { };
            for (const auto& item : declaration->coverage_declarations) {
                if (item.kind
                    != frontend::SystemVerilogCoverageDeclarationKind::Coverpoint) {
                    continue;
                }
                if (procedural
                    && (item.expression_tokens.size() != 1U
                        || item.expression_tokens.front().kind
                            != frontend::TokenKind::Identifier)) {
                    throw std::invalid_argument {
                        "executable covergroup sampling currently requires "
                        "each coverpoint to be one direct sample formal"
                    };
                }
                std::size_t actual_index { coverpoint_index++ };
                if (procedural) {
                    const auto formal = std::ranges::find(
                        declaration->sampling->formals,
                        item.expression_tokens.front().text,
                        &frontend::SystemVerilogCovergroupFormal::name);
                    if (formal == declaration->sampling->formals.end()) {
                        throw std::invalid_argument {
                            "coverpoint expression does not resolve to a sample formal"
                        };
                    }
                    actual_index = static_cast<std::size_t>(std::distance(
                        declaration->sampling->formals.begin(), formal));
                }
                const auto& value = actuals[actual_index];
                frontend::SystemVerilogCoverageSampleValue sample;
                sample.width = static_cast<std::uint32_t>(value.width());
                sample.signed_value = signed_actuals[actual_index] != 0U;
                if (value.width() <= 64U && !value.is_logic9()) {
                    const auto scalar = value.low_word();
                    sample.value = static_cast<std::int64_t>(scalar.aval);
                    sample.unknown_mask = scalar.bval;
                }
                sample.value_bits.reserve(value.width());
                sample.unknown_bits.reserve(value.width());
                for (std::size_t bit = value.width(); bit > 0U; --bit) {
                    const auto state = runtime::to_logic4(
                        value.get_logic9(bit - 1U));
                    sample.value_bits.push_back(
                        state == runtime::Logic4::one
                                || state == runtime::Logic4::x
                            ? '1'
                            : '0');
                    sample.unknown_bits.push_back(
                        state == runtime::Logic4::x
                                || state == runtime::Logic4::z
                            ? '1'
                            : '0');
                }
                inputs.push_back({ item.declaration_index,
                    std::move(sample) });
            }
            const auto result
                = frontend::execute_systemverilog_covergroup_sample(
                    *instance, *declaration,
                    trigger == runtime::simir::CoverageSampleTrigger::procedural
                        ? frontend::SystemVerilogCoverageSampleTrigger::Procedural
                        : trigger == runtime::simir::CoverageSampleTrigger::event
                        ? frontend::SystemVerilogCoverageSampleTrigger::Event
                        : frontend::SystemVerilogCoverageSampleTrigger::Explicit,
                    coverage_execution_mode, inputs,
                    std::span<const frontend::SystemVerilogCoverageCallback> { },
                    coverage_execution_state, coverage_diagnostics);
            coverage.callback_events.insert(
                coverage.callback_events.end(),
                result.events.begin(), result.events.end());
            auto trace_events
                = frontend::build_systemverilog_coverage_trace_events(
                    result, interpreter->scheduler().now(),
                    interpreter->scheduler().delta(), coverage.aliases);
            coverage.trace_events.insert(
                coverage.trace_events.end(),
                std::make_move_iterator(trace_events.begin()),
                std::make_move_iterator(trace_events.end()));
            frontend::refresh_systemverilog_coverage_reports(coverage);
            if (!result.accepted) {
                throw std::runtime_error {
                    coverage_diagnostics.empty()
                        ? "SystemVerilog covergroup sample was rejected"
                        : coverage_diagnostics.back().message
                };
            }
        });
    interpreter->set_coverage_query_hook([this](const auto kind) {
        const auto& coverage = built.systemverilog_coverage;
        const auto percentage = kind
                == runtime::simir::CoverageQueryKind::overall_instance
            ? frontend::calculate_systemverilog_overall_instance_coverage_percentage(
                  coverage.declarations, coverage.instances)
            : frontend::calculate_systemverilog_overall_coverage_percentage(
                  coverage.declarations, coverage.instances);
        const auto encoded = runtime::encode_systemverilog_scalar_payload(
            runtime::SystemVerilogScalarValue::real(
                static_cast<double>(percentage.basis_points) / 100.0));
        if (!encoded) {
            throw std::logic_error {
                "SystemVerilog overall coverage cannot be encoded as real"
            };
        }
        return encoded.value;
    });
    interpreter->set_coverage_database_control_hook(
        [this](const auto& event) {
            control_coverage_database(event);
        });
    interpreter->set_coverage_access_hook(
        [this](const auto& event) {
            return access_standard_coverage(event);
        });
    interpreter->set_coverage_control_hook(
        [this](const auto& event) {
            return control_standard_coverage(event);
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
    if (vpi_runtime_updates
        == SystemVerilogVpiRuntimeUpdates::omitted) {
        vpi_registry = make_empty_systemverilog_vpi_registry();
    } else {
        auto published_vpi
            = make_systemverilog_vpi_design(built, *interpreter);
        vpi_registry = std::move(published_vpi.registry);
        vpi_signal_handles = std::move(published_vpi.signals);
        vpi_handle_signals = std::move(published_vpi.handles);
        vpi_driver_bindings = std::move(published_vpi.drivers);
        vpi_event_handles = std::move(published_vpi.events);
        vpi_assertion_handles = std::move(published_vpi.assertions);
        vpi_scalar_kinds = std::move(published_vpi.scalar_kinds);
        vpi_categories = std::move(published_vpi.categories);
        vpi_container_words = std::move(published_vpi.container_words);
        vpi_word_handles = std::move(published_vpi.word_handles);
        vpi_container_scalar_kinds
            = std::move(published_vpi.container_scalar_kinds);
        vpi_container_categories
            = std::move(published_vpi.container_categories);
    }
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
    configure_standard_vpi_coverage();
    vpi_systems
        = std::make_unique<runtime::SystemVerilogVpiSystemRegistry>(
            *vpi_registry);
    if (vpi_runtime_updates
        != SystemVerilogVpiRuntimeUpdates::omitted) {
        vpi_value_state_observer = vpi_registry->add_value_state_observer(
            [this](const runtime::SystemVerilogVpiValueStateUpdate& update) {
                return apply_vpi_value_state(update);
            });
        if (!vpi_value_state_observer) {
            throw std::logic_error {
                "failed to connect the live VPI value bridge"
            };
        }
    }
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
            publish_vpi_assertion(event);
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
#include "application_simulation_setup_execution.tpp"
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
    interpreter->set_native_signal_observation_required_hook(
        [this](const SignalId signal) {
            return signal_change_hook || !signal_observers.empty()
                || (vpi_runtime_updates_enabled
                    && vpi_callbacks->has_registrations()
                    && (vpi_signal_handles.contains(signal)
                        || vpi_driver_bindings.contains(signal)
                        || vpi_event_handles.contains(signal)));
        });
    interpreter->set_native_signal_observation_any_hook(
        [this] {
            return signal_change_hook || !signal_observers.empty()
                || (vpi_runtime_updates_enabled
                    && vpi_callbacks->has_registrations()
                    && (!vpi_signal_handles.empty()
                        || !vpi_driver_bindings.empty()
                        || !vpi_event_handles.empty()));
        });
    interpreter->set_fork_spawn_filter(
        [this](const runtime::simir::ProcessId process) {
            return !concurrent_assertion_design_processes.contains(process)
                || concurrent_assertions_enabled;
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
            constexpr std::string_view pending_assertion_marker {
                "\x1f"
                "fsim.concurrent-assertion-pending|"
            };
            constexpr std::string_view control_marker {
                "\x1f"
                "fsim.assertion-control|"
            };
            constexpr std::string_view coverage_control_marker {
                "\x1f"
                "fsim.coverage-control|"
            };
            constexpr std::string_view print_timescale_marker {
                "\x1f"
                "fsim.printtimescale|"
            };
            if (text.starts_with(print_timescale_marker)) {
                const auto runtime_process = interpreter->design_process(process);
                const auto process_occurrence = std::ranges::find(
                    built.design_ir.processes(),
                    runtime_process,
                    &semantic::design::ProcessOccurrence::runtime_index);
                if (process_occurrence == built.design_ir.processes().end()) {
                    throw std::runtime_error {
                        "$printtimescale process has no DesignIR occurrence"
                    };
                }
                const auto& current_specialization
                    = built.design_ir.specializations().at(
                        process_occurrence->specialization.value());
                const auto& current_instance = built.design_ir.instances().at(
                    current_specialization.instance.value());
                const auto requested = text.substr(
                    print_timescale_marker.size());
                const auto find_instance = [&](const std::string_view path) {
                    return std::ranges::find(
                        built.design_ir.instances(),
                        path,
                        &semantic::design::InstanceOccurrence::path);
                };
                auto selected = built.design_ir.instances().end();
                if (requested.empty()) {
                    selected = built.design_ir.instances().begin()
                        + static_cast<std::ptrdiff_t>(
                            current_specialization.instance.value());
                } else {
                    auto target = requested;
                    constexpr std::string_view root_prefix { "$root." };
                    if (target.starts_with(root_prefix)) {
                        target.remove_prefix(root_prefix.size());
                    }
                    selected = find_instance(target);
                    auto prefix = current_instance.path;
                    while (selected == built.design_ir.instances().end()
                        && !prefix.empty()) {
                        selected = find_instance(prefix + "."
                            + std::string { target });
                        const auto separator = prefix.rfind('.');
                        if (separator == std::string::npos) {
                            prefix.clear();
                        } else {
                            prefix.erase(separator);
                        }
                    }
                }
                if (selected == built.design_ir.instances().end()) {
                    throw std::runtime_error {
                        "$printtimescale references unavailable instance '"
                        + std::string { requested } + "'"
                    };
                }
                const auto& specialization
                    = built.design_ir.specializations().at(
                        selected->specialization.value());
                if (!specialization.unit.valid()
                    || specialization.unit.value()
                        >= built.semantics.units().size()) {
                    throw std::runtime_error {
                        "$printtimescale instance has no semantic unit"
                    };
                }
                const auto& semantic_unit = built.semantics.units().at(
                    specialization.unit.value());
                const auto unit = std::ranges::find_if(
                    built.systemverilog_hir.units(),
                    [&](const semantic::sv::Unit& candidate) {
                        return candidate.library == semantic_unit.library
                            && candidate.name == semantic_unit.name;
                    });
                if (unit == built.systemverilog_hir.units().end()) {
                    throw std::runtime_error {
                        "$printtimescale instance has no SystemVerilog HIR unit"
                    };
                }
                const auto time_unit = unit->compilation.time_unit.empty()
                    ? std::string_view { "1s" }
                    : std::string_view { unit->compilation.time_unit };
                const auto time_precision
                    = unit->compilation.time_precision.empty()
                    ? std::string_view { "1s" }
                    : std::string_view { unit->compilation.time_precision };
                if (output_hook) {
                    output_hook(
                        process,
                        "Time scale of (" + selected->path + ") is "
                            + std::string { time_unit } + " / "
                            + std::string { time_precision },
                        true,
                        time,
                        delta);
                }
                return;
            }
            if (text.starts_with(pending_assertion_marker)) {
                const auto payload = text.substr(
                    pending_assertion_marker.size());
                std::vector<std::string_view> fields;
                std::size_t begin { };
                while (begin <= payload.size()) {
                    const auto end = payload.find('|', begin);
                    fields.push_back(payload.substr(
                        begin, end == std::string_view::npos ? payload.size() - begin : end - begin));
                    if (end == std::string_view::npos)
                        break;
                    begin = end + 1U;
                }
                if (fields.size() < 4U
                    || (fields[2] != "failure"
                        && fields[2] != "vacuous")) {
                    throw std::runtime_error {
                        "malformed pending concurrent-assertion marker"
                    };
                }
                PendingConcurrentAssertion pending;
                pending.process = interpreter->dynamic_process_root(process);
                pending.design_process = interpreter->design_process(process);
                pending.kind = fields[1];
                pending.vacuous = fields[2] == "vacuous";
                pending.name = fields[3];
                const auto parsed_slot = std::from_chars(
                    fields[0].data(),
                    fields[0].data() + fields[0].size(), pending.slot);
                if (parsed_slot.ec != std::errc { }
                    || parsed_slot.ptr
                        != fields[0].data() + fields[0].size()) {
                    throw std::runtime_error {
                        "malformed pending concurrent-assertion slot"
                    };
                }
                const auto decode = [](const std::string_view encoded) {
                    const auto nibble = [](const char character)
                        -> std::optional<unsigned> {
                        if (character >= '0' && character <= '9')
                            return static_cast<unsigned>(character - '0');
                        if (character >= 'a' && character <= 'f')
                            return static_cast<unsigned>(
                                character - 'a' + 10);
                        return std::nullopt;
                    };
                    if (encoded.size() % 2U != 0U)
                        return std::optional<std::string> { };
                    std::string result;
                    result.reserve(encoded.size() / 2U);
                    for (std::size_t index = 0U;
                        index < encoded.size(); index += 2U) {
                        const auto high = nibble(encoded[index]);
                        const auto low = nibble(encoded[index + 1U]);
                        if (!high || !low)
                            return std::optional<std::string> { };
                        result.push_back(static_cast<char>(
                            (*high << 4U) | *low));
                    }
                    return std::optional<std::string> { std::move(result) };
                };
                for (std::size_t index = 4U; index < fields.size(); ++index) {
                    std::array<std::string_view, 7> action_fields;
                    std::size_t action_begin { };
                    for (std::size_t field = 0U;
                        field < action_fields.size(); ++field) {
                        const auto end = fields[index].find(',', action_begin);
                        action_fields[field] = fields[index].substr(
                            action_begin,
                            end == std::string_view::npos
                                ? fields[index].size() - action_begin
                                : end - action_begin);
                        action_begin = end == std::string_view::npos
                            ? fields[index].size() + 1U
                            : end + 1U;
                    }
                    PendingConcurrentAssertionAction action;
                    action.report = action_fields[0] == "R";
                    std::uint32_t severity { };
                    std::uint32_t newline_value { };
                    const auto severity_result = std::from_chars(
                        action_fields[1].data(),
                        action_fields[1].data() + action_fields[1].size(),
                        severity);
                    const auto newline_result = std::from_chars(
                        action_fields[2].data(),
                        action_fields[2].data() + action_fields[2].size(),
                        newline_value);
                    auto message = decode(action_fields[3]);
                    auto path = decode(action_fields[4]);
                    const auto line_result = std::from_chars(
                        action_fields[5].data(),
                        action_fields[5].data() + action_fields[5].size(),
                        action.source.line);
                    const auto column_result = std::from_chars(
                        action_fields[6].data(),
                        action_fields[6].data() + action_fields[6].size(),
                        action.source.column);
                    if ((action_fields[0] != "R" && action_fields[0] != "D")
                        || severity_result.ec != std::errc { }
                        || severity > static_cast<std::uint32_t>(
                               runtime::simir::AssertionSeverity::failure)
                        || newline_result.ec != std::errc { }
                        || newline_value > 1U || !message || !path
                        || line_result.ec != std::errc { }
                        || column_result.ec != std::errc { }) {
                        throw std::runtime_error {
                            "malformed pending concurrent-assertion action"
                        };
                    }
                    action.severity = static_cast<
                        runtime::simir::AssertionSeverity>(severity);
                    action.newline = newline_value != 0U;
                    action.text = std::move(*message);
                    action.source.path = std::move(*path);
                    pending.actions.push_back(std::move(action));
                }
                const auto attempt = pending.process;
                pending_concurrent_assertions[attempt] = std::move(pending);
                return;
            }
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
                    if (control == "assertkill") {
                        const std::vector<runtime::simir::ProcessId>
                            processes {
                                concurrent_assertion_design_processes.begin(),
                                concurrent_assertion_design_processes.end()
                            };
                        interpreter->kill_dynamic_processes(processes);
                        pending_concurrent_assertions.clear();
                    }
                } else if (control == "assertpasson") {
                    concurrent_assertion_pass_actions_enabled = true;
                    concurrent_assertion_vacuous_actions_enabled = true;
                } else if (control == "assertpassoff") {
                    concurrent_assertion_pass_actions_enabled = false;
                    concurrent_assertion_vacuous_actions_enabled = false;
                } else if (control == "assertnonvacuouson") {
                    concurrent_assertion_pass_actions_enabled = true;
                } else if (control == "assertvacuousoff") {
                    concurrent_assertion_vacuous_actions_enabled = false;
                } else if (control == "assertfailon") {
                    concurrent_assertion_failure_actions_enabled = true;
                } else if (control == "assertfailoff") {
                    concurrent_assertion_failure_actions_enabled = false;
                }
                return;
            }
            if (text.starts_with(coverage_control_marker)) {
                const auto control = text.substr(
                    coverage_control_marker.size());
                const auto separator = control.find('|');
                if (separator == std::string_view::npos
                    || separator + 1U == control.size()) {
                    throw std::runtime_error {
                        "malformed covergroup control marker"
                    };
                }
                const auto action = control.substr(0U, separator);
                const auto identity = control.substr(separator + 1U);
                const auto& instances
                    = built.systemverilog_coverage.instances;
                if (std::ranges::find(
                        instances, identity,
                        &frontend::SystemVerilogCovergroupInstance::runtime_identity)
                    == instances.end()) {
                    throw std::runtime_error {
                        "covergroup control references unavailable instance '"
                        + std::string { identity } + "'"
                    };
                }
                if (action == "start") {
                    stopped_covergroups.erase(std::string { identity });
                } else if (action == "stop") {
                    stopped_covergroups.insert(std::string { identity });
                } else {
                    throw std::runtime_error {
                        "malformed covergroup control action"
                    };
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
                    const auto coverage_key
                        = interpreter->design_process(process);
                    const auto outcome = payload.substr(
                        kind_end + 1U,
                        outcome_end - kind_end - 1U);
                    if (outcome == "register") {
                        concurrent_assertion_design_processes.insert(
                            coverage_key);
                        return;
                    }
                    if (outcome == "abort") {
                        const std::array processes { coverage_key };
                        interpreter->kill_dynamic_processes(processes);
                        std::erase_if(
                            pending_concurrent_assertions,
                            [&](const auto& item) {
                                return item.second.design_process
                                    == coverage_key;
                            });
                        return;
                    }
                    pending_concurrent_assertions.erase(
                        interpreter->dynamic_process_root(process));
                    concurrent_assertion_actions_suppressed[process]
                        = outcome == "pass"
                        ? !concurrent_assertion_pass_actions_enabled
                        : outcome == "vacuous"
                        ? !concurrent_assertion_vacuous_actions_enabled
                        : !concurrent_assertion_failure_actions_enabled;
                    std::uint32_t slot { };
                    const auto slot_text = payload.substr(0, slot_end);
                    std::from_chars(
                        slot_text.data(), slot_text.data() + slot_text.size(),
                        slot);
                    const auto kind = payload.substr(
                        slot_end + 1U, kind_end - slot_end - 1U);
                    const auto coverage_index
                        = ensure_concurrent_assertion_coverage(
                            coverage_key, slot, kind,
                            payload.substr(outcome_end + 1U));
                    auto& coverage
                        = concurrent_assertion_coverage[coverage_index];
                    ConcurrentAssertionEvent event;
                    event.name = coverage.name;
                    event.process = coverage.process;
                    event.kind = coverage.kind;
                    event.slot = coverage.slot;
                    event.time = time;
                    event.delta = delta;
                    event.action_suppressed
                        = concurrent_assertion_actions_suppressed[process];
                    ++coverage.attempts;
                    if (outcome == "pass") {
                        event.outcome = ConcurrentAssertionOutcome::pass;
                        ++coverage.passes;
                    } else if (outcome == "vacuous") {
                        event.outcome = ConcurrentAssertionOutcome::vacuous;
                        ++coverage.vacuous;
                    } else {
                        event.outcome = ConcurrentAssertionOutcome::failure;
                        ++coverage.failures;
                    }
                    concurrent_assertion_events.push_back(std::move(event));
                    publish_vpi_assertion(
                        concurrent_assertion_events.back());
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
            constexpr std::string_view numeric_metavalue_message
                = "numeric_std.to_integer detected a metavalue and returned zero";
            if (severity == runtime::simir::AssertionSeverity::warning
                && message == numeric_metavalue_message
                && !numeric_metavalue_reports.emplace(
                        process,
                        source.path,
                        source.line,
                        source.column)
                        .second) {
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

[[nodiscard]] std::size_t ensure_concurrent_assertion_coverage(
    const runtime::simir::ProcessId process,
    const std::uint32_t slot,
    const std::string_view kind,
    const std::string_view name)
{
    if (const auto found = concurrent_assertion_indices.find(process);
        found != concurrent_assertion_indices.end()) {
        return found->second;
    }
    ConcurrentAssertionCoverage coverage;
    coverage.slot = slot;
    coverage.kind = kind == "assumption"
        ? ConcurrentAssertionCoverageKind::assumption
        : kind == "cover"
        ? ConcurrentAssertionCoverageKind::cover
        : kind == "restriction"
        ? ConcurrentAssertionCoverageKind::restriction
        : ConcurrentAssertionCoverageKind::assertion;
    coverage.name = name;
    const auto occurrence = std::ranges::find_if(
        built.design_ir.processes(),
        [&](const auto& item) {
            return item.runtime_index == process;
        });
    coverage.process = occurrence != built.design_ir.processes().end()
        ? occurrence->name
        : coverage.name;
    concurrent_assertion_coverage.push_back(std::move(coverage));
    const auto inserted = concurrent_assertion_coverage.size() - 1U;
    concurrent_assertion_indices.emplace(process, inserted);
    return inserted;
}

void finish_concurrent_assertions(
    const SimulationTick time,
    const std::uint64_t delta)
{
    for (const auto& [process, pending] : pending_concurrent_assertions) {
        const auto index = ensure_concurrent_assertion_coverage(
            pending.design_process, pending.slot, pending.kind, pending.name);
        auto& coverage = concurrent_assertion_coverage[index];
        ++coverage.attempts;
        if (pending.vacuous)
            ++coverage.vacuous;
        else
            ++coverage.failures;
        ConcurrentAssertionEvent event;
        event.name = coverage.name;
        event.process = coverage.process;
        event.kind = coverage.kind;
        event.slot = coverage.slot;
        event.time = time;
        event.delta = delta;
        event.outcome = pending.vacuous
            ? ConcurrentAssertionOutcome::vacuous
            : ConcurrentAssertionOutcome::failure;
        event.action_suppressed = pending.vacuous
            ? !concurrent_assertion_vacuous_actions_enabled
            : !concurrent_assertion_failure_actions_enabled;
        concurrent_assertion_events.push_back(event);
        publish_vpi_assertion(concurrent_assertion_events.back());
        if (concurrent_assertion_hook) {
            concurrent_assertion_hook(concurrent_assertion_events.back());
        }
        if (event.action_suppressed)
            continue;
        for (const auto& action : pending.actions) {
            if (action.report) {
                if (report_hook) {
                    report_hook(
                        process, action.text, action.severity,
                        action.source, time, delta);
                }
            } else if (output_hook) {
                output_hook(
                    process, action.text, action.newline, time, delta);
            }
        }
    }
    pending_concurrent_assertions.clear();
}
