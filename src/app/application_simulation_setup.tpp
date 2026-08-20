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
    , interpreter(std::move(built.design).create_interpreter(
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
        const bool profile_jit = std::getenv("FSIM_PROFILE_JIT") != nullptr;
        const auto jit_setup_begin = std::chrono::steady_clock::now();
        auto registration_time = std::chrono::steady_clock::duration::zero();
        auto materialization_launch_time
            = std::chrono::steady_clock::duration::zero();
        std::size_t compiled_operation_count = 0;
        std::size_t lowered_process_count = 0;
        std::size_t lowered_operation_count = 0;
        std::size_t largest_module_operation_count = 0;
        std::string largest_module_identity;
        std::size_t retained_process_count = 0;
        std::size_t retained_operation_count = 0;
        compiler::LlvmJitOptions options;
        options.optimization = engine == SimulationEngine::debug
            ? compiler::JitOptimizationLevel::o0
            : jit_optimization(built.optimization);
        options.debug_instrumentation = engine == SimulationEngine::debug;
        options.require_direct_update_slots
            = built.design.verilog_specify_paths().empty();
        if (!built.cache_path.empty()) {
            options.cache_directory = built.cache_path / "llvm-native";
        }
        jit = std::make_unique<compiler::LlvmJit>(std::move(options));
        if (!built.artifact_identity.empty()) {
            jit->set_immutable_design_identity(built.artifact_identity);
        }

        signal_widths.reserve(built.design.signals().size());
        signal_value_kinds.reserve(
            built.design.signals().size());
        signal_resolutions.reserve(built.design.signals().size());
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
            signal_resolutions.push_back(signal.resolution);
        }
        std::vector<const runtime::simir::Process*> processes;
        processes.reserve(built.design_ir.processes().size());
        std::vector<bool> constant_specialized_processes(
            built.design_ir.processes().size(), false);
        std::vector<bool> written_signals(
            built.design.signals().size(), false);
        for (std::size_t process = 0;
             process < built.design_ir.processes().size(); ++process) {
            const auto& program = interpreter->process_program(
                static_cast<runtime::simir::ProcessId>(process));
            for (const auto& region : program.driver_regions) {
                written_signals.at(region.signal) = true;
            }
        }
        std::vector<bool> static_vhdl_arrays(
            built.design.signals().size(), false);
        for (const auto& signal : built.design.signals()) {
            static_vhdl_arrays.at(signal.id) = signal.vhdl_array.has_value()
                && !signal.is_port && !written_signals.at(signal.id);
        }
        for (std::size_t process = 0;
             process < built.design_ir.processes().size(); ++process) {
            const auto& program = interpreter->process_program(
                static_cast<runtime::simir::ProcessId>(process));
            std::unique_ptr<runtime::simir::Process> specialized;
            if (engine != SimulationEngine::debug) {
                for (const auto& operation : program.operations) {
                    const auto* read
                        = runtime::simir::operation_get_if<
                            runtime::simir::ReadSignal>(&operation);
                    if (read == nullptr
                        || read->kind
                            != runtime::simir::SignalReadKind::current
                        || !static_vhdl_arrays.at(read->signal)) {
                        continue;
                    }
                    if (!specialized) {
                        specialized = std::make_unique<
                            runtime::simir::Process>(program);
                    }
                    const auto operation_index = static_cast<std::size_t>(
                        &operation - program.operations.data());
                    specialized->operations[operation_index]
                        = runtime::simir::LoadConstant {
                            read->destination,
                            interpreter->signal_value(read->signal) };
                }
            }
            if (specialized) {
                constant_specialized_processes.at(process) = true;
                processes.push_back(specialized.get());
                jit_specialized_processes.push_back(std::move(specialized));
            } else {
                processes.push_back(&program);
            }
        }
        struct AdaptiveCompilationGate {
            std::atomic_uint64_t interpreted_operations { };
            std::atomic_uint64_t interpreted_activations { };
            std::uint64_t minimum_operations_per_activation { };
            std::atomic_uint8_t eligible { };
        };
        struct PendingCompiledModule {
            struct Executor {
                const runtime::simir::Process* process { };
                std::size_t compiled_process { };
                std::shared_ptr<const LlvmProcessExecutor::SignalRemap>
                    signal_remap;
                runtime::simir::ProcessId generated_process { };
            };
            std::string identity;
            std::vector<const runtime::simir::Process*> processes;
            std::vector<std::string> symbols;
            std::vector<Executor> executors;
            std::shared_ptr<AdaptiveCompilationGate> adaptive_gate;
            bool startup { true };
        };
        std::vector<PendingCompiledModule> pending_modules;
        // Tiny one-shot initialization processes cost more to compile than
        // they can repay. Substantial non-recurring processes can contain
        // long testbench or protocol loops, however, so operation count must
        // still be allowed to select them for native execution.
        // Oversized one-off state machines are poor cold-JIT candidates.  In
        // representative large HDL designs their LLVM optimization and
        // backend cost exceeds the interpreter time they replace, while the
        // smaller shareable templates account for most executed operations.
        // Keep those large bodies on the interpreter tier; a later persisted
        // profile may promote them when a longer run can amortize compilation.
        constexpr std::size_t maximum_jit_process_operations = 9000U;
        // Keep the cold barrier to kernels whose native execution repays LLVM
        // lowering during short simulations. Larger recurring kernels are
        // still valuable for sustained runs, so compile and promote them only
        // after the startup tier has completed.
        constexpr std::size_t maximum_startup_jit_process_operations = 1536U;
        const std::size_t minimum_large_design_jit_operations
            = std::getenv("FSIM_JIT_PROMOTE_TINY_RECURRING") != nullptr
            ? 0U
            : 32U;
        // A large structural template must repay its standalone LLVM module
        // through enough equivalent elaborated processes.  Body size alone
        // is misleading for guarded clocked processes and combinational
        // initialization: both can contain a thousand unrolled operations
        // while executing only a small fraction of that work during a short
        // run.  Five or more structurally equivalent users provide enough
        // reuse to retain medium-sized hot kernels even when their total body
        // size falls just below the standalone amortization floor.
        constexpr std::size_t maximum_packed_module_operations = 512U;
        constexpr std::size_t minimum_reused_template_executors = 5U;
        constexpr std::size_t minimum_large_template_amortized_operations
            = 4096U;
        constexpr std::uint64_t adaptive_compilation_operation_threshold
            = 50'000U;
        constexpr std::size_t minimum_nonrecurring_jit_operations = 1024U;
        // LLVM's ORC layer compiles synchronously in this bounded pool. Keep
        // eight independent lowering/backend workers: controlled cold runs
        // show that reducing the pool lengthens the materialization tail more
        // than it helps concurrent scheduler progress.
        constexpr std::size_t maximum_materialization_jobs = 8U;
        // Background modules are still part of the cold result: Simulation
        // owns their futures and must join them before the JIT can be
        // destroyed. Limiting this tail to two workers serialized several
        // independent large specializations after an otherwise short run.
        // Keep one common eight-worker bound for both tiers; on hosts with
        // fewer CPUs materialization_job_limit already scales this down.
        const auto detected_materialization_jobs
            = std::thread::hardware_concurrency();
        const auto materialization_job_limit = std::min(
            maximum_materialization_jobs,
            detected_materialization_jobs == 0U
                ? std::size_t { 8 }
                : static_cast<std::size_t>(
                      detected_materialization_jobs));
        std::optional<std::set<runtime::simir::ProcessId>> process_filter;
        if (const auto* const value = std::getenv("FSIM_JIT_PROCESS_IDS")) {
            process_filter.emplace();
            auto remaining = std::string_view { value };
            while (!remaining.empty()) {
                const auto separator = remaining.find(',');
                const auto token = remaining.substr(0, separator);
                runtime::simir::ProcessId id { };
                const auto [end, error]
                    = std::from_chars(token.begin(), token.end(), id);
                if (error != std::errc { } || end != token.end()) {
                    throw std::invalid_argument(
                        "FSIM_JIT_PROCESS_IDS contains an invalid process ID");
                }
                process_filter->insert(id);
                if (separator == std::string_view::npos) {
                    break;
                }
                remaining.remove_prefix(separator + 1U);
            }
        }
        auto* const jit_pointer = jit.get();
        const bool selective_large_design_compilation
            = !process_filter && processes.size() >= 128U;
        jit_overlap_startup = selective_large_design_compilation;
        const auto materialize_pending_modules = [&] {
            const auto registration_begin = std::chrono::steady_clock::now();
            using CompilationResult
                = std::vector<compiler::JitProcessHandle>;
            struct MaterializationJob {
                std::string identity;
                std::vector<const runtime::simir::Process*> processes;
                std::vector<std::string> symbols;
                std::shared_ptr<std::promise<CompilationResult>> completion;
                std::shared_ptr<std::atomic_uint8_t> availability;
                std::size_t operation_count { };
                std::uint64_t execution_weight { };
                std::chrono::steady_clock::duration materialization_time { };
                std::size_t materialization_worker { };
                std::shared_ptr<AdaptiveCompilationGate> adaptive_gate;
                bool materialized { };
                bool startup { true };
            };
            std::vector<MaterializationJob> jobs;
            jobs.reserve(pending_modules.size());
            for (auto& module : pending_modules) {
                auto executors = std::move(module.executors);
                std::uint64_t execution_weight = 0;
                for (const auto* const process : module.processes) {
                    const auto sensitivity = std::max<std::size_t>(
                        1U, process->static_sensitivity.size());
                    execution_weight += static_cast<std::uint64_t>(
                        process->operations.size())
                        * sensitivity;
                }
                auto completion
                    = std::make_shared<std::promise<CompilationResult>>();
                auto availability = std::make_shared<std::atomic_uint8_t>(0U);
                auto compilation = completion->get_future().share();
                jit_compilations.push_back(compilation);
                if (module.startup) {
                    jit_startup_compilations.push_back(compilation);
                }
                for (auto& executor : executors) {
                    const auto* const process = executor.process;
                    const auto process_index = executor.compiled_process;
                    auto signal_remap = std::move(executor.signal_remap);
                    const auto generated_process = executor.generated_process;
                    std::shared_ptr<std::uint64_t> observed_operations;
                    if (module.adaptive_gate) {
                        interpreter->track_process_interpreter_operations(
                            process->id);
                        observed_operations
                            = std::make_shared<std::uint64_t>(0U);
                    }
                    interpreter->set_deferred_process_executor(
                        process->id,
                        [availability, jit_pointer, compilation,
                            adaptive_gate = module.adaptive_gate,
                            observed_operations,
                            process_index,
                            interpreter_pointer = interpreter.get(),
                            process_id = process->id,
                            this] {
                            if (adaptive_gate
                                && adaptive_gate->eligible.load(
                                       std::memory_order_relaxed)
                                    == 0U) {
                                const auto current = interpreter_pointer
                                    ->process_interpreter_operations(
                                        process_id);
                                const auto delta
                                    = current - *observed_operations;
                                *observed_operations = current;
                                const auto total = adaptive_gate
                                    ->interpreted_operations.fetch_add(
                                        delta,
                                        std::memory_order_relaxed)
                                    + delta;
                                const auto activations = delta == 0U
                                    ? adaptive_gate->interpreted_activations
                                          .load(std::memory_order_relaxed)
                                    : adaptive_gate->interpreted_activations
                                              .fetch_add(
                                                  1U,
                                                  std::memory_order_relaxed)
                                          + 1U;
                                if (total
                                        >= adaptive_compilation_operation_threshold
                                    && activations != 0U
                                    && total / activations
                                        >= adaptive_gate
                                            ->minimum_operations_per_activation
                                    && adaptive_gate->eligible.exchange(
                                           1U,
                                           std::memory_order_release)
                                        == 0U) {
                                    jit_background_condition.notify_all();
                                }
                            }
                            // Executor polling is a hot scheduling boundary.
                            // Avoid shared_future readiness locks until the
                            // release-published native handles are available.
                            if (availability->load(
                                    std::memory_order_acquire)
                                != 1U) {
                                return false;
                            }
                            const auto& handles = compilation.get();
                            return jit_pointer->supports_entry(
                                handles.at(process_index),
                                interpreter_pointer->process_instruction(
                                    process_id));
                        },
                        [this, jit_pointer, compilation, process,
                            process_index,
                            signal_remap = std::move(signal_remap),
                            generated_process] {
                            const auto& handles = compilation.get();
                            return std::make_unique<LlvmProcessExecutor>(
                                *jit_pointer,
                                handles.at(process_index),
                                *process,
                                this->signal_widths,
                                this->signal_value_kinds,
                                this->signal_resolutions,
                                signal_remap,
                                generated_process);
                        });
                }
                jobs.push_back(
                    { std::move(module.identity),
                        std::move(module.processes),
                        std::move(module.symbols),
                        std::move(completion), std::move(availability),
                        0U, execution_weight,
                        { }, { }, std::move(module.adaptive_gate), false,
                        module.startup });
                std::size_t module_operation_count = 0;
                for (const auto* const process : jobs.back().processes) {
                    module_operation_count += process->operations.size();
                }
                jobs.back().operation_count = module_operation_count;
                lowered_process_count += jobs.back().processes.size();
                lowered_operation_count += module_operation_count;
                if (module_operation_count
                    > largest_module_operation_count) {
                    largest_module_operation_count = module_operation_count;
                    largest_module_identity = jobs.back().identity;
                }
                ++compiled_modules;
            }
            registration_time += std::chrono::steady_clock::now()
                - registration_begin;
            pending_modules.clear();
            std::ranges::sort(jobs, [](const auto& lhs, const auto& rhs) {
                if (lhs.startup != rhs.startup) {
                    return lhs.startup;
                }
                if (static_cast<bool>(lhs.adaptive_gate)
                    != static_cast<bool>(rhs.adaptive_gate)) {
                    return !lhs.adaptive_gate;
                }
                if (lhs.operation_count != rhs.operation_count) {
                    // Longest-processing-time order keeps the bounded backend
                    // workers balanced. Sensitivity predicts runtime benefit,
                    // but does not predict LLVM lowering and code-generation
                    // cost and previously left a large module on the tail.
                    return lhs.operation_count > rhs.operation_count;
                }
                if (lhs.execution_weight != rhs.execution_weight) {
                    return lhs.execution_weight > rhs.execution_weight;
                }
                return lhs.identity < rhs.identity;
            });

            const auto materialization_launch_begin
                = std::chrono::steady_clock::now();
            const auto worker_count = std::min(
                materialization_job_limit, jobs.size());
            jit_materialization = std::async(
                std::launch::async,
                [this, jit_pointer, jobs = std::move(jobs),
                    worker_count]() mutable {
                    const auto compile_job = [this, jit_pointer](
                                                 auto& job,
                                                 const std::size_t worker) {
                        const auto materialization_begin
                            = std::chrono::steady_clock::now();
                        job.materialization_worker = worker;
                        try {
                            std::vector<compiler::JitProcessModuleEntry>
                                entries;
                            entries.reserve(job.processes.size());
                            for (std::size_t process = 0;
                                 process < job.processes.size(); ++process) {
                                entries.push_back(
                                    { job.symbols[process],
                                        job.processes[process] });
                            }
                            jit_pointer->add_process_module(
                                job.identity,
                                entries,
                                this->signal_widths,
                                this->signal_value_kinds);
                            CompilationResult handles;
                            handles.reserve(job.symbols.size());
                            for (const auto& symbol : job.symbols) {
                                handles.push_back(jit_pointer->lookup(symbol));
                            }
                            job.availability->store(
                                1U, std::memory_order_release);
                            // Future readiness is the final publication event.
                            // A caller that completes the startup barrier can
                            // therefore materialize every successful executor
                            // without racing this availability flag.
                            job.completion->set_value(std::move(handles));
                        } catch (const compiler::LlvmJitUnsupportedError& error) {
                            // Full register-width, dataflow, and control-flow
                            // validation runs here. Unsupported modules retain
                            // their interpreter executors.
                            if (std::getenv("FSIM_PROFILE_JIT_MODULES")
                                != nullptr) {
                                std::cerr
                                    << "fsim jit unsupported module: identity="
                                    << job.identity
                                    << " reason=" << error.what() << '\n';
                            }
                            job.completion->set_value({ });
                            job.availability->store(
                                2U, std::memory_order_release);
                        } catch (...) {
                            job.completion->set_exception(
                                std::current_exception());
                            job.availability->store(
                                2U, std::memory_order_release);
                        }
                        job.materialization_time
                            = std::chrono::steady_clock::now()
                            - materialization_begin;
                        job.materialized = true;
                    };
                    const auto run_jobs = [&](const std::size_t first,
                                              const std::size_t last,
                                              const std::size_t limit) {
                        if (first == last) {
                            return;
                        }
                        std::atomic_size_t next_job { first };
                        const auto range_worker_count
                            = std::min(limit, last - first);
                        std::vector<std::thread> workers;
                        workers.reserve(range_worker_count);
                        for (std::size_t worker = 0;
                             worker < range_worker_count; ++worker) {
                            workers.emplace_back(
                                [&jobs, &next_job, last, worker,
                                    &compile_job] {
                                while (true) {
                                    const auto index = next_job.fetch_add(
                                        1U, std::memory_order_relaxed);
                                    if (index >= last) {
                                        return;
                                    }
                                    compile_job(jobs[index], worker);
                                }
                            });
                        }
                        for (auto& worker : workers) {
                            worker.join();
                        }
                    };
                    const auto startup_end = static_cast<std::size_t>(
                        std::ranges::find(jobs, false,
                            &MaterializationJob::startup)
                        - jobs.begin());
                    run_jobs(0U, startup_end, worker_count);
                    if (startup_end != jobs.size()) {
                        std::unique_lock lock { jit_background_mutex };
                        jit_background_condition.wait(lock, [this] {
                            return jit_background_requested
                                || jit_background_cancelled;
                        });
                        if (jit_background_cancelled
                            && !jit_background_requested) {
                            return;
                        }
                    }
                    const auto adaptive_begin = static_cast<std::size_t>(
                        std::ranges::find_if(
                            jobs.begin() + startup_end,
                            jobs.end(),
                            [](const auto& job) {
                                return static_cast<bool>(job.adaptive_gate);
                            })
                        - jobs.begin());
                    run_jobs(startup_end, adaptive_begin, worker_count);
                    while (adaptive_begin != jobs.size()) {
                        bool compiled = false;
                        bool pending = false;
                        for (std::size_t index = adaptive_begin;
                             index < jobs.size(); ++index) {
                            auto& job = jobs[index];
                            if (job.materialized) {
                                continue;
                            }
                            bool forced = false;
                            {
                                std::scoped_lock lock {
                                    jit_background_mutex
                                };
                                forced = jit_background_forced;
                            }
                            if (forced
                                || job.adaptive_gate->eligible.load(
                                       std::memory_order_acquire)
                                    != 0U) {
                                compile_job(job, 0U);
                                compiled = true;
                            } else {
                                pending = true;
                            }
                        }
                        if (!pending) {
                            break;
                        }
                        if (compiled) {
                            continue;
                        }
                        std::unique_lock lock { jit_background_mutex };
                        jit_background_condition.wait(lock, [&] {
                            return jit_background_cancelled
                                || jit_background_forced
                                || std::ranges::any_of(
                                    jobs.begin() + adaptive_begin,
                                    jobs.end(),
                                    [](const auto& job) {
                                        return !job.materialized
                                            && job.adaptive_gate->eligible.load(
                                                   std::memory_order_acquire)
                                                != 0U;
                                    });
                        });
                        if (jit_background_cancelled
                            && !jit_background_forced) {
                            for (std::size_t index = adaptive_begin;
                                 index < jobs.size(); ++index) {
                                auto& job = jobs[index];
                                if (job.materialized) {
                                    continue;
                                }
                                job.completion->set_value({ });
                                job.availability->store(
                                    2U, std::memory_order_release);
                                job.materialized = true;
                            }
                            break;
                        }
                    }
                    if (std::getenv("FSIM_PROFILE_JIT_MODULES") != nullptr) {
                        std::ranges::sort(
                            jobs,
                            [](const auto& lhs, const auto& rhs) {
                                return lhs.materialization_time
                                    > rhs.materialization_time;
                            });
                        const auto milliseconds = [](const auto duration) {
                            return std::chrono::duration<double, std::milli> {
                                duration
                            }.count();
                        };
                        for (const auto& job : jobs) {
                            const auto identity = std::string_view {
                                job.identity
                            }.substr(0U, 96U);
                            std::cerr
                                << "fsim jit module profile: identity="
                                << identity
                                << " processes=" << job.processes.size()
                                << " process_ids=";
                            for (std::size_t index = 0;
                                 index < job.processes.size(); ++index) {
                                if (index != 0U) {
                                    std::cerr << ',';
                                }
                                std::cerr << job.processes[index]->id;
                            }
                            std::cerr
                                << " operations=" << job.operation_count
                                << " weight=" << job.execution_weight
                                << " worker=" << job.materialization_worker
                                << " startup=" << job.startup
                                << " materialization_ms="
                                << milliseconds(job.materialization_time)
                                << '\n';
                        }
                    }
                }).share();
            materialization_launch_time
                += std::chrono::steady_clock::now()
                - materialization_launch_begin;
        };
        const auto shareable_process = [](const runtime::simir::Process& process) {
            return std::ranges::all_of(
                process.operations,
                [](const runtime::simir::Operation& operation) {
                    bool shareable = false;
                    runtime::simir::visit_operation(
                        [&](const auto& value) {
                            using Type = std::decay_t<decltype(value)>;
                            shareable = std::is_same_v<Type, runtime::simir::DebugPoint>
                                || std::is_same_v<Type, runtime::simir::ReadSignal>
                                || std::is_same_v<Type, runtime::simir::WriteBlocking>
                                || std::is_same_v<Type, runtime::simir::WriteUpdate>
                                || std::is_same_v<Type, runtime::simir::WriteBlockingSlice>
                                || std::is_same_v<Type, runtime::simir::WriteUpdateSlice>
                                || std::is_same_v<
                                    Type,
                                    runtime::simir::WriteUpdateDynamicPartSlice>
                                || std::is_same_v<Type, runtime::simir::CopyRegister>
                                || std::is_same_v<Type, runtime::simir::IntegerCheck>
                                || std::is_same_v<Type, runtime::simir::LoadConstant>
                                || std::is_same_v<Type, runtime::simir::DynamicInsert>
                                || std::is_same_v<Type, runtime::simir::DynamicPartSelect>
                                || std::is_same_v<Type, runtime::simir::IntegerBinary>
                                || std::is_same_v<Type, runtime::simir::DynamicExtract>
                                || std::is_same_v<Type, runtime::simir::ReadContainerObject>
                                || std::is_same_v<Type, runtime::simir::ContainerRead>
                                || std::is_same_v<Type, runtime::simir::WriteProjected>
                                || std::is_same_v<
                                    Type,
                                    runtime::simir::WriteProjectedSlice>
                                || std::is_same_v<Type, runtime::simir::WaitSensitivity>
                                || std::is_same_v<Type, runtime::simir::CallableFramePop>
                                || std::is_same_v<Type, runtime::simir::CallableFramePush>
                                || std::is_same_v<Type, runtime::simir::Call>
                                || std::is_same_v<Type, runtime::simir::Jump>
                                || std::is_same_v<Type, runtime::simir::Binary>
                                || std::is_same_v<Type, runtime::simir::Assert>
                                || std::is_same_v<Type, runtime::simir::UnaryNot>
                                || std::is_same_v<Type, runtime::simir::LogicalNot>
                                || std::is_same_v<Type, runtime::simir::LogicalBinary>
                                || std::is_same_v<Type, runtime::simir::Reduction>
                                || std::is_same_v<Type, runtime::simir::Shift>
                                || std::is_same_v<Type, runtime::simir::Concatenate>
                                || std::is_same_v<Type, runtime::simir::ConditionalSelect>
                                || std::is_same_v<Type, runtime::simir::Branch>
                                || std::is_same_v<Type, runtime::simir::Insert>
                                || std::is_same_v<Type, runtime::simir::Return>
                                || std::is_same_v<Type, runtime::simir::Extract>;
                            if (!shareable
                                && std::getenv("FSIM_PROFILE_JIT_OPERATIONS")
                                    != nullptr) {
                                std::cerr
                                    << "FSIM-JIT-UNSHAREABLE-OPERATION type="
                                    << typeid(value).name() << '\n';
                            }
                        },
                        operation);
                    return shareable;
                });
        };
        const auto signal_remap = [&](const runtime::simir::Process& representative,
                                      const runtime::simir::Process& candidate)
            -> std::shared_ptr<const LlvmProcessExecutor::SignalRemap> {
            if (representative.operations.size() != candidate.operations.size()
                || representative.register_count != candidate.register_count
                || representative.register_value_kinds
                    != candidate.register_value_kinds
                || representative.string_register_count
                    != candidate.string_register_count
                || representative.container_register_count
                    != candidate.container_register_count
                || representative.container_register_types
                    != candidate.container_register_types) {
                return { };
            }
            std::map<std::uint32_t, std::uint32_t> assigned;
            const auto map_signal = [&](const runtime::simir::SignalId source,
                                        const runtime::simir::SignalId target) {
                if (source >= signal_widths.size()
                    || target >= signal_widths.size()
                    || signal_widths[source] != signal_widths[target]
                    || signal_value_kinds[source]
                        != signal_value_kinds[target]) {
                    return false;
                }
                const auto [found, inserted]
                    = assigned.try_emplace(source, target);
                return inserted || found->second == target;
            };
            for (std::size_t index = 0;
                 index < representative.operations.size(); ++index) {
                bool compatible = true;
                runtime::simir::visit_operation(
                    [&](const auto& left) {
                        using Type = std::decay_t<decltype(left)>;
                        const auto* const right
                            = runtime::simir::operation_get_if<Type>(
                                &candidate.operations[index]);
                        if (right == nullptr) {
                            compatible = false;
                            return;
                        }
                        if constexpr (std::is_same_v<
                                          Type, runtime::simir::DebugPoint>
                            || std::is_same_v<
                                Type, runtime::simir::WaitSensitivity>) {
                            // The candidate process owns debugger metadata and
                            // static sensitivity after the shared native body
                            // returns the common instruction index.
                        } else if constexpr (std::is_same_v<
                                                 Type,
                                                 runtime::simir::ReadSignal>) {
                            compatible = left.destination == right->destination
                                && left.kind == right->kind
                                && left.ticks == right->ticks
                                && left.clock.has_value()
                                    == right->clock.has_value()
                                && left.clock_edge == right->clock_edge
                                && left.gate.has_value()
                                    == right->gate.has_value()
                                && map_signal(left.signal, right->signal);
                            if (compatible && left.clock) {
                                compatible = map_signal(
                                    *left.clock, *right->clock);
                            }
                            if (compatible && left.gate) {
                                compatible = map_signal(
                                    *left.gate, *right->gate);
                            }
                        } else if constexpr (std::is_same_v<
                                                 Type,
                                                 runtime::simir::WriteProjected>) {
                            compatible = left.source == right->source
                                && left.delay == right->delay
                                && left.rejection == right->rejection
                                && left.mode == right->mode
                                && map_signal(left.signal, right->signal);
                        } else if constexpr (std::is_same_v<
                                                 Type,
                                                 runtime::simir::WriteProjectedSlice>) {
                            compatible = left.source == right->source
                                && left.offset == right->offset
                                && left.delay == right->delay
                                && left.rejection == right->rejection
                                && left.mode == right->mode
                                && map_signal(left.signal, right->signal);
                        } else if constexpr (std::is_same_v<
                                                 Type,
                                                 runtime::simir::WriteBlocking>
                            || std::is_same_v<
                                Type, runtime::simir::WriteUpdate>) {
                            compatible = left.source == right->source
                                && map_signal(left.signal, right->signal);
                        } else if constexpr (std::is_same_v<
                                                 Type,
                                                 runtime::simir::WriteBlockingSlice>
                            || std::is_same_v<
                                Type, runtime::simir::WriteUpdateSlice>) {
                            compatible = left.source == right->source
                                && left.offset == right->offset
                                && map_signal(left.signal, right->signal);
                        } else if constexpr (std::is_same_v<
                                                 Type,
                                                 runtime::simir::WriteUpdateDynamicPartSlice>) {
                            compatible = left.source == right->source
                                && left.selection == right->selection
                                && map_signal(left.signal, right->signal);
                        } else if constexpr (std::is_same_v<
                                                 Type,
                                                 runtime::simir::LoadConstant>) {
                            compatible = left.destination == right->destination
                                && left.value == right->value;
                        } else if constexpr (std::is_same_v<
                                                 Type,
                                                 runtime::simir::CopyRegister>) {
                            compatible = left.destination == right->destination
                                && left.source == right->source;
                        } else if constexpr (std::is_same_v<
                                                 Type,
                                                 runtime::simir::IntegerCheck>) {
                            compatible = left.source == right->source
                                && left.lower == right->lower
                                && left.upper == right->upper;
                        } else if constexpr (std::is_same_v<
                                                 Type,
                                                 runtime::simir::DynamicInsert>) {
                            compatible = left.destination == right->destination
                                && left.target == right->target
                                && left.source == right->source
                                && left.selection == right->selection;
                        } else if constexpr (std::is_same_v<
                                                 Type,
                                                 runtime::simir::DynamicPartSelect>) {
                            compatible = left.destination == right->destination
                                && left.source == right->source
                                && left.base == right->base
                                && left.left == right->left
                                && left.right == right->right
                                && left.width == right->width
                                && left.increasing == right->increasing
                                && left.source_descending
                                    == right->source_descending
                                && left.two_state == right->two_state
                                && left.base_offset == right->base_offset;
                        } else if constexpr (std::is_same_v<
                                                 Type,
                                                 runtime::simir::IntegerBinary>) {
                            compatible = left.operation == right->operation
                                && left.destination == right->destination
                                && left.lhs == right->lhs
                                && left.rhs == right->rhs;
                        } else if constexpr (std::is_same_v<
                                                 Type,
                                                 runtime::simir::DynamicExtract>) {
                            compatible = left.destination == right->destination
                                && left.source == right->source
                                && left.selection == right->selection;
                        } else if constexpr (std::is_same_v<
                                                 Type,
                                                 runtime::simir::ReadContainerObject>) {
                            // The callback resolves the candidate's object ID
                            // from its own operation stream. The generated body
                            // only fixes the destination register layout.
                            compatible = left.destination == right->destination;
                        } else if constexpr (std::is_same_v<
                                                 Type,
                                                 runtime::simir::ContainerRead>) {
                            compatible = left.destination == right->destination
                                && left.source == right->source
                                && left.index == right->index
                                && left.signed_index == right->signed_index
                                && left.linear_index == right->linear_index
                                && left.string_index == right->string_index;
                        } else if constexpr (std::is_same_v<
                                                 Type,
                                                 runtime::simir::CallableFramePop>) {
                            compatible = left.identity == right->identity
                                && left.preserve_packed == right->preserve_packed
                                && left.preserve_strings == right->preserve_strings
                                && left.preserve_containers
                                    == right->preserve_containers;
                        } else if constexpr (std::is_same_v<
                                                 Type,
                                                 runtime::simir::CallableFramePush>) {
                            compatible = left.identity == right->identity
                                && left.packed == right->packed
                                && left.strings == right->strings
                                && left.containers == right->containers
                                && left.native_isolated == right->native_isolated;
                        } else if constexpr (std::is_same_v<
                                                 Type, runtime::simir::Call>) {
                            compatible = left.target == right->target
                                && left.return_target == right->return_target
                                && left.stack.pointer == right->stack.pointer
                                && left.stack.entries == right->stack.entries
                                && left.stack.capacity == right->stack.capacity;
                        } else if constexpr (std::is_same_v<
                                                 Type, runtime::simir::Jump>) {
                            compatible = left.target == right->target;
                        } else if constexpr (std::is_same_v<
                                                 Type, runtime::simir::Binary>) {
                            compatible = left.operation == right->operation
                                && left.destination == right->destination
                                && left.lhs == right->lhs
                                && left.rhs == right->rhs;
                        } else if constexpr (std::is_same_v<
                                                 Type, runtime::simir::Assert>) {
                            // Assertion text and source ownership remain in
                            // the candidate SimIR. Generated code fixes only
                            // the condition register and whether failure must
                            // return to the scheduler instead of reporting.
                            compatible = left.condition == right->condition
                                && left.severity == right->severity;
                        } else if constexpr (std::is_same_v<
                                                 Type,
                                                 runtime::simir::LogicalNot>) {
                            compatible = left.destination == right->destination
                                && left.source == right->source;
                        } else if constexpr (std::is_same_v<
                                                 Type,
                                                 runtime::simir::LogicalBinary>) {
                            compatible = left.operation == right->operation
                                && left.destination == right->destination
                                && left.lhs == right->lhs
                                && left.rhs == right->rhs;
                        } else if constexpr (std::is_same_v<
                                                 Type,
                                                 runtime::simir::Reduction>) {
                            compatible = left.operation == right->operation
                                && left.destination == right->destination
                                && left.source == right->source;
                        } else if constexpr (std::is_same_v<
                                                 Type, runtime::simir::Shift>) {
                            compatible = left.operation == right->operation
                                && left.destination == right->destination
                                && left.value == right->value
                                && left.amount == right->amount
                                && left.signed_amount == right->signed_amount;
                        } else if constexpr (std::is_same_v<
                                                 Type,
                                                 runtime::simir::Concatenate>) {
                            compatible = left.destination == right->destination
                                && left.operands == right->operands
                                && left.width == right->width;
                        } else if constexpr (std::is_same_v<
                                                 Type,
                                                 runtime::simir::ConditionalSelect>) {
                            compatible = left.destination == right->destination
                                && left.condition == right->condition
                                && left.when_true == right->when_true
                                && left.when_false == right->when_false;
                        } else if constexpr (std::is_same_v<
                                                 Type, runtime::simir::Branch>) {
                            compatible = left.condition == right->condition
                                && left.when_true == right->when_true
                                && left.when_false == right->when_false
                                && left.unknown_policy == right->unknown_policy;
                        } else if constexpr (std::is_same_v<
                                                 Type, runtime::simir::Insert>) {
                            compatible = left.destination == right->destination
                                && left.target == right->target
                                && left.source == right->source
                                && left.offset == right->offset;
                        } else if constexpr (std::is_same_v<
                                                 Type, runtime::simir::Return>) {
                            compatible = left.stack.pointer == right->stack.pointer
                                && left.stack.entries == right->stack.entries
                                && left.stack.capacity == right->stack.capacity;
                        } else if constexpr (std::is_same_v<
                                                 Type, runtime::simir::Extract>) {
                            compatible = left.destination == right->destination
                                && left.source == right->source
                                && left.offset == right->offset
                                && left.width == right->width;
                        }
                    },
                    representative.operations[index]);
                if (!compatible) {
                    return { };
                }
            }
            auto result
                = std::make_shared<LlvmProcessExecutor::SignalRemap>();
            result->reserve(assigned.size());
            for (const auto& [source, target] : assigned) {
                if (source != target) {
                    result->emplace_back(source, target);
                }
            }
            return result;
        };
        std::multimap<std::string, PendingCompiledModule, std::less<>>
            shared_process_modules;
        for (const auto& specialization : built.design_ir.specializations()) {
            if (specialization.language == semantic::Language::systemc) {
                continue;
            }
            std::array<std::vector<const runtime::simir::Process*>, 2>
                selected;
            std::array<std::vector<std::string>, 2> symbols;
            std::array<std::vector<PendingCompiledModule::Executor>, 2>
                executors;
            for (auto& tier : selected) {
                tier.reserve(specialization.processes.size());
            }
            for (auto& tier : symbols) {
                tier.reserve(specialization.processes.size());
            }
            for (auto& tier : executors) {
                tier.reserve(specialization.processes.size());
            }
            for (std::size_t specialization_process = 0;
                 specialization_process < specialization.processes.size();
                 ++specialization_process) {
                const auto process_id
                    = specialization.processes[specialization_process];
                const auto runtime_id = built.design_ir.processes()[process_id.value()].runtime_index;
                const auto& process = *processes.at(runtime_id);
                const bool constant_specialized
                    = constant_specialized_processes.at(runtime_id);
                if (process_filter && !process_filter->contains(process.id)) {
                    ++retained_process_count;
                    retained_operation_count += process.operations.size();
                    continue;
                }
                if (std::getenv("FSIM_PROFILE_JIT_OPERATIONS") != nullptr) {
                    std::vector<std::pair<std::string_view, std::size_t>> counts;
                    std::vector<std::pair<std::string, std::size_t>>
                        container_profiles;
                    std::size_t profile_operation_index { };
                    for (const auto& process_operation : process.operations) {
                        runtime::simir::visit_operation(
                            [&](const auto& operation) {
                                using Operation
                                    = std::decay_t<decltype(operation)>;
                                const std::string_view name {
                                    typeid(operation).name()
                                };
                                const auto found = std::ranges::find(
                                    counts, name, &decltype(counts)::value_type::first);
                                if (found == counts.end()) {
                                    counts.emplace_back(name, 1U);
                                } else {
                                    ++found->second;
                                }
                                if constexpr (
                                    std::is_same_v<Operation,
                                        runtime::simir::ContainerRead>
                                    || std::is_same_v<Operation,
                                        runtime::simir::ContainerWrite>) {
                                    const auto container = [&] {
                                        if constexpr (std::is_same_v<Operation,
                                                          runtime::simir::ContainerRead>) {
                                            return operation.source;
                                        } else {
                                            return operation.target;
                                        }
                                    }();
                                    const auto& type
                                        = process.container_register_types.at(container);
                                    std::string profile
                                        = std::is_same_v<Operation,
                                              runtime::simir::ContainerRead>
                                        ? "read"
                                        : "write";
                                    profile += " fixed="
                                        + std::to_string(type.fixed)
                                        + " associative="
                                        + std::to_string(type.associative)
                                        + " element_kind="
                                        + std::to_string(
                                            static_cast<unsigned>(
                                                type.element_kind))
                                        + " element_width="
                                        + std::to_string(type.element_width)
                                        + " linear="
                                        + std::to_string(operation.linear_index)
                                        + " string_index="
                                        + std::to_string(operation.string_index);
                                    const auto profile_found = std::ranges::find(
                                        container_profiles, profile,
                                        &decltype(container_profiles)::value_type::first);
                                    if (profile_found
                                        == container_profiles.end()) {
                                        container_profiles.emplace_back(
                                            std::move(profile), 1U);
                                    } else {
                                        ++profile_found->second;
                                    }
                                }
                                if constexpr (
                                    std::is_same_v<Operation,
                                        runtime::simir::ReadSignal>) {
                                    std::cerr << "FSIM-JIT-READ-SIGNAL id="
                                              << process.id
                                              << " instruction="
                                              << profile_operation_index
                                              << " signal=" << operation.signal
                                              << " width="
                                              << signal_widths.at(
                                                     operation.signal)
                                              << " destination="
                                              << operation.destination << '\n';
                                } else if constexpr (
                                    std::is_same_v<Operation,
                                        runtime::simir::WriteProjected>) {
                                    std::cerr << "FSIM-JIT-WRITE-PROJECTED id="
                                              << process.id
                                              << " instruction="
                                              << profile_operation_index
                                              << " signal=" << operation.signal
                                              << " width="
                                              << signal_widths.at(
                                                     operation.signal)
                                              << " source=" << operation.source
                                              << '\n';
                                } else if constexpr (
                                    std::is_same_v<Operation,
                                        runtime::simir::WriteProjectedSlice>) {
                                    std::cerr
                                        << "FSIM-JIT-WRITE-PROJECTED-SLICE id="
                                        << process.id
                                        << " instruction="
                                        << profile_operation_index
                                        << " signal=" << operation.signal
                                        << " width="
                                        << signal_widths.at(
                                               operation.signal)
                                        << " source=" << operation.source
                                        << " offset=" << operation.offset
                                        << '\n';
                                }
                            },
                            process_operation);
                        ++profile_operation_index;
                    }
                    std::ranges::sort(
                        counts, std::greater { },
                        &decltype(counts)::value_type::second);
                    for (const auto& [name, count] : counts) {
                        std::cerr << "FSIM-JIT-OPERATION-SUMMARY id="
                                  << process.id << " count=" << count
                                  << " type=" << name << '\n';
                    }
                    std::ranges::sort(
                        container_profiles, std::greater { },
                        &decltype(container_profiles)::value_type::second);
                    for (const auto& [profile, count] : container_profiles) {
                        std::cerr << "FSIM-JIT-CONTAINER-SUMMARY id="
                                  << process.id << " count=" << count << ' '
                                  << profile << '\n';
                    }
                }
                if (process.operations.size()
                    > maximum_jit_process_operations) {
                    ++retained_process_count;
                    retained_operation_count += process.operations.size();
                    continue;
                }
                const bool recurring_process
                    = !process.static_sensitivity.empty()
                    || std::ranges::any_of(
                        process.operations,
                        [](const runtime::simir::Operation& operation) {
                            return runtime::simir::operation_holds<
                                runtime::simir::WaitSensitivity>(operation);
                        });
                if (selective_large_design_compilation
                    && !recurring_process
                    && process.operations.size()
                        < minimum_nonrecurring_jit_operations) {
                    ++retained_process_count;
                    retained_operation_count += process.operations.size();
                    continue;
                }
                const bool shareable = shareable_process(process);
                if (std::getenv("FSIM_PROFILE_JIT_OPERATIONS") != nullptr) {
                    std::cerr << "FSIM-JIT-SHAREABLE id=" << process.id
                              << " value=" << shareable << '\n';
                }
                if (selective_large_design_compilation && !shareable
                    && !constant_specialized
                    && process.operations.size()
                        < minimum_large_design_jit_operations) {
                    ++retained_process_count;
                    retained_operation_count += process.operations.size();
                    continue;
                }
                if (shareable && selective_large_design_compilation) {
                    std::string structural_bucket
                        = std::to_string(process.operations.size()) + ":"
                        + std::to_string(process.register_count) + ":"
                        + std::to_string(process.string_register_count) + ":"
                        + std::to_string(process.static_sensitivity.size())
                        + ":";
                    for (const auto kind : process.register_value_kinds) {
                        structural_bucket += std::to_string(
                            static_cast<unsigned>(kind));
                        structural_bucket += ',';
                    }
                    const auto [first, last]
                        = shared_process_modules.equal_range(
                            structural_bucket);
                    bool reused = false;
                    for (auto candidate = first;
                         candidate != last; ++candidate) {
                        auto& shared = candidate->second;
                        if (auto remap = signal_remap(
                                *shared.processes.front(), process)) {
                            shared.executors.push_back(
                                { &process, 0U, std::move(remap),
                                    shared.processes.front()->id });
                            reused = true;
                            break;
                        }
                    }
                    if (!reused) {
                        PendingCompiledModule shared;
                        shared.identity = "fsim-process-template:"
                            + structural_bucket + ":"
                            + std::to_string(
                                shared_process_modules.size());
                        shared.processes.push_back(&process);
                        shared.symbols.push_back(
                            "fsim_process_" + std::to_string(process.id));
                        shared.executors.push_back(
                            { &process, 0U, { }, process.id });
                        shared.startup
                            = !selective_large_design_compilation
                            || process.operations.size()
                                <= maximum_startup_jit_process_operations;
                        shared_process_modules.emplace(
                            structural_bucket, std::move(shared));
                    }
                    ++compiled_processes;
                    compiled_operation_count += process.operations.size();
                    continue;
                }
                const auto tier = !selective_large_design_compilation
                        || process.operations.size()
                            <= maximum_startup_jit_process_operations
                    ? std::size_t { 0 }
                    : std::size_t { 1 };
                selected[tier].push_back(&process);
                compiled_operation_count += process.operations.size();
                symbols[tier].push_back(
                    "fsim_process_" + std::to_string(process.id));
                executors[tier].push_back(
                    { &process, selected[tier].size() - 1U, { }, process.id });
                ++compiled_processes;
            }
            const auto module_identity = "fsim-specialization:" + std::to_string(specialization.id.value()) + ":" + specialization.name + "@" + built.design_ir.instances()[specialization.instance.value()].path + "#provenance=" + built.specialization_cache_keys.at(specialization.id.value())
                + (built.artifact_identity.empty()
                        ? std::string { }
                        : "#artifact=" + built.artifact_identity);
            for (std::size_t tier = 0; tier < selected.size(); ++tier) {
                if (selected[tier].empty()) {
                    continue;
                }
                const bool startup = tier == 0U;
                pending_modules.push_back(
                    { module_identity
                          + (startup ? "#tier=startup" : "#tier=background"),
                        std::move(selected[tier]), std::move(symbols[tier]),
                        std::move(executors[tier]), { }, startup });
            }
        }
        for (auto& [identity, module] : shared_process_modules) {
            (void)identity;
            const auto amortized_operations = module.processes.front()
                ->operations.size() * module.executors.size();
            if (selective_large_design_compilation
                && amortized_operations
                    < minimum_large_design_jit_operations) {
                for (const auto& executor : module.executors) {
                    --compiled_processes;
                    compiled_operation_count
                        -= executor.process->operations.size();
                    ++retained_process_count;
                    retained_operation_count
                        += executor.process->operations.size();
                }
                continue;
            }
            if (selective_large_design_compilation
                && module.processes.front()->operations.size()
                    > maximum_packed_module_operations
                && module.executors.size()
                    < minimum_reused_template_executors
                && amortized_operations
                    < minimum_large_template_amortized_operations) {
                module.adaptive_gate
                    = std::make_shared<AdaptiveCompilationGate>();
                module.adaptive_gate->minimum_operations_per_activation
                    = std::max<std::uint64_t>(
                        1U,
                        (module.processes.front()->operations.size() + 9U)
                            / 10U);
                module.startup = false;
            }
            pending_modules.push_back(std::move(module));
        }
        const auto unpacked_module_count = pending_modules.size();
        std::ranges::stable_sort(
            pending_modules,
            [](const auto& lhs, const auto& rhs) {
                return lhs.startup && !rhs.startup;
            });
        // Large generated designs can contain thousands of one-process LLVM
        // modules. Amortize ORC registration, object emission, and atomic
        // cache publication while retaining enough independent units to keep
        // every compile worker busy. Small designs preserve their established
        // module/cache accounting exactly.
        if (pending_modules.size() >= 128U) {
            constexpr std::size_t maximum_pack_processes = 64U;
            std::vector<PendingCompiledModule> packed_modules;
            packed_modules.reserve(pending_modules.size());
            PendingCompiledModule packed;
            std::size_t packed_operations = 0;
            std::size_t pack_index = 0;
            const auto flush_pack = [&] {
                if (packed.processes.empty()) {
                    return;
                }
                packed_modules.push_back(std::move(packed));
                packed = PendingCompiledModule { };
                packed_operations = 0;
                ++pack_index;
            };
            for (auto& module : pending_modules) {
                std::size_t module_operations = 0;
                for (const auto* const process : module.processes) {
                    module_operations += process->operations.size();
                }
                if (module_operations > maximum_packed_module_operations) {
                    flush_pack();
                    packed_modules.push_back(std::move(module));
                    continue;
                }
                if (!packed.processes.empty()
                    && (packed.startup != module.startup
                        || packed_operations + module_operations
                            > maximum_packed_module_operations
                        || packed.processes.size() + module.processes.size()
                            > maximum_pack_processes)) {
                    flush_pack();
                }
                if (packed.processes.empty()) {
                    packed.identity = "fsim-packed-module:"
                        + std::to_string(pack_index) + ":design="
                        + (built.artifact_identity.empty()
                                ? built.cache_key
                                : built.artifact_identity);
                    packed.startup = module.startup;
                }
                const auto process_base = packed.processes.size();
                for (auto& executor : module.executors) {
                    executor.compiled_process += process_base;
                }
                packed.processes.insert(
                    packed.processes.end(),
                    std::make_move_iterator(module.processes.begin()),
                    std::make_move_iterator(module.processes.end()));
                packed.symbols.insert(
                    packed.symbols.end(),
                    std::make_move_iterator(module.symbols.begin()),
                    std::make_move_iterator(module.symbols.end()));
                packed.executors.insert(
                    packed.executors.end(),
                    std::make_move_iterator(module.executors.begin()),
                    std::make_move_iterator(module.executors.end()));
                packed_operations += module_operations;
            }
            flush_pack();
            pending_modules = std::move(packed_modules);
        }
        if (!pending_modules.empty()) {
            materialize_pending_modules();
        }
        if (profile_jit) {
            const auto milliseconds = [](const auto duration) {
                return std::chrono::duration<double, std::milli>(duration)
                    .count();
            };
            std::cerr
                << "FSIM-JIT-PROFILE setup_ms="
                << milliseconds(
                       std::chrono::steady_clock::now() - jit_setup_begin)
                << " registration_ms=" << milliseconds(registration_time)
                << " materialization_launch_ms="
                << milliseconds(materialization_launch_time)
                << " modules=" << compiled_modules
                << " unpacked_modules=" << unpacked_module_count
                << " processes=" << compiled_processes
                << " operations=" << compiled_operation_count
                << " lowered_processes=" << lowered_process_count
                << " lowered_operations=" << lowered_operation_count
                << " largest_module_operations="
                << largest_module_operation_count
                << " largest_module_identity='"
                << largest_module_identity << "'"
                << " retained_processes=" << retained_process_count
                << " retained_operations=" << retained_operation_count
                << " selective_large_design="
                << selective_large_design_compilation
                << '\n';
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
