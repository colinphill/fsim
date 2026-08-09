// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"
#include "application_uvm_registry.hpp"

#include <ranges>

namespace fsim::app {
using namespace application_detail;

struct Simulation::Impl {
  enum class Lifecycle {
    ready,
    finished,
    poisoned,
  };

  Impl(
      BuiltProject project,
      const std::uint64_t max_deltas,
      const SimulationEngine engine)
      : built(std::move(project)),
        class_heap({}, built.seed),
        class_methods(class_heap, {}, &class_static_store),
        uvm_objects(
            class_heap,
            [this](const std::string_view specialization,
                   const std::string_view declared,
                   const std::string_view) {
              return construct_class(
                  specialization,
                  declared,
                  std::span<const runtime::PackedLogic4>{},
                  std::span<const std::string>{},
                  std::span<const std::string>{},
                  "$uvm-clone");
            }),
        uvm_components(class_heap, uvm_objects),
        uvm_activity(),
        uvm_phases(uvm_components),
        uvm_objections(uvm_objects, uvm_components, uvm_phases),
        uvm_tlm1(class_heap, uvm_components),
        uvm_tlm2(uvm_components),
        uvm_sequences(
            class_heap, uvm_objects, uvm_components, uvm_phases,
            uvm_objections),
        uvm_callbacks(uvm_objects, uvm_components),
        uvm_transactions(uvm_objects, uvm_components, uvm_callbacks),
        uvm_register_model(uvm_components),
        uvm_foreign(
            uvm_phases, uvm_objections, uvm_tlm1, uvm_tlm2, uvm_activity),
        uvm_registry(
            class_heap,
            uvm_objects,
            uvm_components,
            [this](const std::string_view specialization,
                   const std::string_view name) {
              const auto handle = construct_class(
                  specialization,
                  class_specialization(specialization).declaration_identity,
                  std::span<const runtime::PackedLogic4>{},
                  std::span<const std::string>{},
                  std::span<const std::string>{},
                  "$uvm-registry");
              uvm_objects.set_name(handle, std::string{name});
              return handle;
            },
            [this](const std::string_view specialization,
                   const std::string_view name,
                   const runtime::SystemVerilogClassHandle parent,
                   const runtime::SystemVerilogUvmRootHandle root) {
              std::array actuals{
                  runtime::PackedLogic4(64),
                  runtime::PackedLogic4::from_aval_bval(64, parent, 0)};
              std::array string_actuals{
                  std::string{name}, std::string{}};
              return construct_class(
                  specialization,
                  class_specialization(specialization).declaration_identity,
                  actuals,
                  string_actuals,
                  std::span<const std::string>{},
                  "$uvm-registry",
                  root);
            }),
        uvm_factory(uvm_registry),
        uvm_resources(&class_heap),
        uvm_config_db(uvm_resources),
        uvm_command_line(uvm_factory, uvm_resources, uvm_config_db),
        uvm_reports(uvm_objects, uvm_components),
        interpreter(built.design.create_interpreter(
            runtime::SchedulerOptions{max_deltas, 32},
            built.seed)) {
    uvm_phases.set_scheduler(interpreter->scheduler());
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
    uvm_register_model.set_backdoor_transport({
        [this](const auto, const std::string_view path)
            -> std::optional<std::size_t> {
          const auto signal = backdoor_signal(path);
          return signal ? std::optional<std::size_t>{
                              interpreter->signal_value(*signal).width()}
                        : std::nullopt;
        },
        [this](const auto, const std::string_view path) {
          const auto signal = backdoor_signal(path);
          if (!signal)
            throw std::invalid_argument{"UVM HDL path does not resolve"};
          return interpreter->signal_value(*signal);
        },
        [this](const auto, const std::string_view path, const auto kind,
               const runtime::PackedLogic4 &value) {
          const auto signal = backdoor_signal(path);
          if (!signal)
            throw std::invalid_argument{"UVM HDL path does not resolve"};
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
          throw std::invalid_argument{"invalid UVM HDL write operation"};
        }});
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
        throw std::logic_error{
            "portable SystemVerilog UVM bootstrap state does not match "
            "the fresh simulation"};
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
      descriptor.specialization_identity =
          specialization.specialization_identity;
      descriptor.base_specialization_identity =
          specialization.base_specialization_identity;
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
    const auto has_systemc_process = std::ranges::any_of(
        built.design_ir.boundaries(), [](const auto& boundary) {
          return boundary.kind
              == semantic::design::BoundaryKind::systemc_process;
        });
    if (has_systemc_process && !built.systemc_hierarchy
        && built.systemc_hierarchies.empty()) {
      throw std::logic_error{
          "SystemC processes require their native hierarchy registry"};
    }
    for (const auto& boundary : built.design_ir.boundaries()) {
      if (boundary.kind
              != semantic::design::BoundaryKind::systemc_process
          || !boundary.process) {
        continue;
      }
      const auto process = built.design_ir.processes()[
          boundary.process->value()].runtime_index;
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
      options.optimization =
          engine == SimulationEngine::debug
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
          const auto runtime_id = built.design_ir.processes()[
              process_id.value()].runtime_index;
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
          entries.push_back({symbols[index], selected[index]});
        }
        const auto module_identity =
            "fsim-specialization:" +
            std::to_string(specialization.id.value()) + ":" +
            specialization.name + "@" +
            built.design_ir.instances()[specialization.instance.value()]
                .path + "#provenance=" +
            built.specialization_cache_keys.at(
                specialization.id.value())
            + (built.artifact_identity.empty()
                   ? std::string{}
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
          constexpr std::string_view assertion_marker{
              "\x1f" "fsim.concurrent-assertion|"};
          constexpr std::string_view control_marker{
              "\x1f" "fsim.assertion-control|"};
          if (text.starts_with(control_marker)) {
            auto control = text.substr(control_marker.size());
            if (control.starts_with("assertcontrol|")) {
              std::uint32_t control_type{};
              const auto value = control.substr(
                  std::string_view{"assertcontrol|"}.size());
              const auto parsed = std::from_chars(
                  value.data(), value.data() + value.size(), control_type);
              if (parsed.ec == std::errc{}) {
                switch (control_type) {
                case 3: control = "asserton"; break;
                case 4: control = "assertoff"; break;
                case 5: control = "assertkill"; break;
                case 6: control = "assertpasson"; break;
                case 7: control = "assertpassoff"; break;
                case 8: control = "assertfailon"; break;
                case 9: control = "assertfailoff"; break;
                case 10: control = "assertnonvacuouson"; break;
                case 11: control = "assertvacuousoff"; break;
                default: break;
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
              concurrent_assertion_actions_suppressed[key] =
                  !concurrent_assertions_enabled
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
                coverage.name =
                    payload.substr(outcome_end + 1U);
                const auto occurrence = std::ranges::find_if(
                    built.design_ir.processes(),
                    [&](const auto& item) {
                      return item.runtime_index == key;
                    });
                coverage.process =
                    occurrence != built.design_ir.processes().end()
                        ? occurrence->name
                        : coverage.name;
                concurrent_assertion_coverage.push_back(
                    std::move(coverage));
                const auto inserted =
                    concurrent_assertion_coverage.size() - 1U;
                concurrent_assertion_indices.emplace(key, inserted);
                found = concurrent_assertion_indices.find(key);
              }
              auto& coverage =
                  concurrent_assertion_coverage[found->second];
              ConcurrentAssertionEvent event;
              event.name = coverage.name;
              event.process = coverage.process;
              event.kind = coverage.kind;
              event.slot = coverage.slot;
              event.time = time;
              event.delta = delta;
              event.action_suppressed =
                  concurrent_assertion_actions_suppressed[key];
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

  void validate_external_value(
      const SignalId signal,
      const PackedLogic4& value,
      const std::string_view operation) const {
    const auto& info = built.design.signals().at(signal);
    if (info.width != value.width()) {
      throw std::invalid_argument(
          std::string{operation} + " width does not match signal '"
          + info.name + "'");
    }
    if (info.source_domain != frontend::ValueDomain::Bit2
        && info.source_domain != frontend::ValueDomain::Boolean) {
      return;
    }
    for (std::size_t bit = 0; bit < value.width(); ++bit) {
      const auto state = value.get(bit);
      if (state != runtime::Logic4::zero
          && state != runtime::Logic4::one) {
        throw std::invalid_argument(
            std::string{operation}
            + " would place an X/Z value into two-state signal '"
            + info.name + "'");
      }
    }
  }

  [[nodiscard]] const frontend::SystemVerilogClassSpecialization&
  class_specialization(const std::string_view identity) const {
    auto found = std::ranges::find(
        built.systemverilog_class_specializations,
        identity,
        &frontend::SystemVerilogClassSpecialization::specialization_identity);
    if (found == built.systemverilog_class_specializations.end()) {
      const auto matches = std::ranges::count(
          built.systemverilog_class_specializations,
          identity,
          &frontend::SystemVerilogClassSpecialization::declaration_identity);
      if (matches == 1) {
        found = std::ranges::find(
            built.systemverilog_class_specializations,
            identity,
            &frontend::SystemVerilogClassSpecialization::declaration_identity);
      }
    }
    if (found == built.systemverilog_class_specializations.end()) {
      throw std::out_of_range{
          "SystemVerilog class specialization '" + std::string{identity}
          + "' is not available in this simulation"};
    }
    return *found;
  }

  [[nodiscard]] runtime::SystemVerilogUvmRootHandle component_root(
      const std::string_view allocation_scope) {
    const std::string identity{
        allocation_scope.empty() ? "$simulation" : allocation_scope};
    const auto found = uvm_roots_by_scope.find(identity);
    if (found != uvm_roots_by_scope.end()) return found->second;
    const auto root = uvm_components.create_root(identity);
    const auto [inserted, did_insert] =
        uvm_roots_by_scope.emplace(identity, root);
    if (!did_insert) {
      uvm_components.destroy_root(root);
      throw std::logic_error{"duplicate automatic UVM root identity"};
    }
    try {
      uvm_phases.participate_standard_root(root);
    } catch (...) {
      uvm_roots_by_scope.erase(inserted);
      uvm_components.destroy_root(root);
      throw;
    }
    return root;
  }

  [[nodiscard]] runtime::PackedLogic4 invoke_source_randomize(
      const runtime::SystemVerilogClassHandle handle,
      const std::span<const std::string> selected_names) {
    auto& object = class_heap.object(handle);
    const auto prior_properties = object.properties;
    const auto callback = [&](const std::string_view name) {
      const auto* found = application_detail::systemverilog_randomize_callback(
          built.systemverilog_class_specializations,
          class_heap, handle, name);
      if (found == nullptr) return;
      if (found->kind != frontend::SystemVerilogClassMethodKind::Function
          || found->is_static || !found->arguments.empty()
          || found->return_type.spelling != "void") {
        throw std::invalid_argument{
            "randomize callback requires a nonstatic zero-argument void function"};
      }
      std::vector<runtime::PackedLogic4> actuals;
      (void)invoke_source_profile(*found, handle, actuals, {}, {});
    };
    try {
      callback("pre_randomize");
    } catch (...) {
      object.properties = prior_properties;
      return runtime::PackedLogic4::from_aval_bval(32, 0, 0);
    }
    runtime::SystemVerilogClassRandomizeRequest request;
    for (const auto& name : selected_names) {
      if (!name.empty()) request.variable_list.push_back(name);
    }
    request.call_identity = object.specialization_identity
        + "::randomize@source";
    request.class_constraints = [this, handle, specialization =
        object.specialization_identity](auto& solver, const auto& variables) {
      application_detail::configure_systemverilog_class_constraints(
          solver,
          variables,
          built.systemverilog_hir,
          class_specialization(specialization),
          [this, handle](const auto identity) {
            return class_heap.constraint_mode(handle, identity);
          });
    };
    const auto result = runtime::randomize_systemverilog_class_object(
        class_heap, handle, request);
    if (result.language_result() != 0) {
      try {
        callback("post_randomize");
      } catch (...) {
        object.properties = prior_properties;
        return runtime::PackedLogic4::from_aval_bval(32, 0, 0);
      }
    }
    return runtime::PackedLogic4::from_aval_bval(
        32, result.language_result(), 0);
  }

  [[nodiscard]] runtime::PackedLogic4 invoke_source_randomization_mode(
      const runtime::SystemVerilogClassHandle handle,
      const std::string_view method,
      const std::span<const runtime::PackedLogic4> actuals) {
    return application_detail::invoke_systemverilog_randomization_mode(
        class_heap, handle, method, actuals);
  }

  using ConstructorEnvironment =
      std::map<std::string, runtime::PackedLogic4, std::less<>>;

  [[nodiscard]] static runtime::PackedLogic4 resize_packed(
      const runtime::PackedLogic4& value,
      const std::size_t width) {
    runtime::PackedLogic4 result(width, runtime::Logic4::zero);
    for (std::size_t bit = 0; bit < std::min(width, value.width()); ++bit) {
      result.set(bit, value.get(bit));
    }
    return result;
  }

  [[nodiscard]] static runtime::PackedLogic4 packed_property_value(
      const runtime::SystemVerilogClassPropertyValue& value) {
    if (value.kind == runtime::SystemVerilogClassPropertyKind::ClassHandle) {
      return runtime::PackedLogic4::from_aval_bval(64, value.handle, 0);
    }
    return value.packed;
  }

  [[nodiscard]] const frontend::SystemVerilogClassPropertyLayout&
  source_property(const std::string_view canonical_identity) const {
    const auto [owner, name] = static_property_parts(canonical_identity);
    const auto& specialization = class_specialization(owner);
    const auto found = std::ranges::find_if(
        specialization.properties, [&](const auto& property) {
          return property.owner_identity == owner && property.name == name;
        });
    if (found == specialization.properties.end()) {
      throw std::out_of_range{
          "SystemVerilog class property profile '"
          + std::string{canonical_identity} + "' is not available"};
    }
    return *found;
  }

  void assign_property_value(
      runtime::SystemVerilogClassPropertyValue& destination,
      const std::string_view canonical_identity,
      const runtime::PackedLogic4& value) {
    if (destination.kind
        == runtime::SystemVerilogClassPropertyKind::ClassHandle) {
      const auto word = value.low_word();
      if (word.bval != 0) {
        throw std::invalid_argument{
            "class-handle property assignment contains X or Z"};
      }
      const auto handle = word.aval;
      const auto& declared_type = source_property(
          canonical_identity).type.systemverilog_class_declaration;
      if (handle != 0) {
        (void)class_heap.checked_cast(handle, declared_type);
      }
      destination.handle = handle;
      return;
    }
    destination.packed = resize_packed(value, destination.packed.width());
  }

  [[nodiscard]] runtime::PackedLogic4 invoke_class_container(
      const runtime::SystemVerilogClassHandle receiver,
      const std::string_view operation,
      const std::span<const runtime::PackedLogic4> actuals) {
    constexpr std::string_view read_prefix{"@container-read:"};
    constexpr std::string_view write_prefix{"@container-write:"};
    constexpr std::string_view resize_prefix{"@container-resize:"};
    constexpr std::string_view push_back_prefix{
        "@container-push-back:"};
    constexpr std::string_view pop_front_prefix{
        "@container-pop-front:"};
    constexpr std::string_view size_prefix{"@container-size:"};
    const auto prefix = operation.starts_with(read_prefix)
        ? read_prefix
        : operation.starts_with(write_prefix)
            ? write_prefix
            : operation.starts_with(resize_prefix)
                ? resize_prefix
                : operation.starts_with(push_back_prefix)
                    ? push_back_prefix
                    : operation.starts_with(pop_front_prefix)
                        ? pop_front_prefix
                        : operation.starts_with(size_prefix)
                            ? size_prefix
                            : std::string_view{};
    if (prefix.empty()) {
      throw std::invalid_argument{
          "unknown class handle container operation"};
    }
    auto& property = class_heap.property(receiver, operation.substr(
        prefix.size()));
    if (!property.handle_container) {
      throw std::invalid_argument{
          "class property is not a handle container"};
    }
    auto& container = *property.handle_container;
    const auto known_word = [&](const std::size_t index) {
      if (index >= actuals.size()) {
        throw std::invalid_argument{
            "class handle container operation has missing actuals"};
      }
      const auto word = actuals[index].low_word();
      if (word.bval != 0) {
        throw std::invalid_argument{
            "class handle container operand contains X or Z"};
      }
      return word.aval;
    };
    if (prefix == resize_prefix) {
      if (actuals.size() != 1U
          || !std::in_range<std::size_t>(known_word(0))) {
        throw std::length_error{
            "class handle container size exceeds host storage"};
      }
      container.resize(static_cast<std::size_t>(known_word(0)));
      return runtime::PackedLogic4::from_aval_bval(64, 0, 0);
    }
    if (prefix == push_back_prefix) {
      if (actuals.size() != 1U) {
        throw std::invalid_argument{
            "class handle queue push_back has inconsistent actuals"};
      }
      const auto value = known_word(0);
      container.push_back(class_heap, value);
      return runtime::PackedLogic4::from_aval_bval(64, value, 0);
    }
    if (prefix == pop_front_prefix) {
      if (!actuals.empty()) {
        throw std::invalid_argument{
            "class handle queue pop_front has inconsistent actuals"};
      }
      return runtime::PackedLogic4::from_aval_bval(
          64, container.pop_front(), 0);
    }
    if (prefix == size_prefix) {
      if (!actuals.empty()) {
        throw std::invalid_argument{
            "class handle container size has inconsistent actuals"};
      }
      return runtime::PackedLogic4::from_aval_bval(
          32, container.size(), 0);
    }
    if (actuals.size() != (prefix == read_prefix ? 1U : 2U)) {
      throw std::invalid_argument{
          "class handle container operation has inconsistent actuals"};
    }
    const auto index = known_word(0);
    const auto keyed = container.kind()
            == runtime::SystemVerilogClassContainerKind::AssociativeArray
        || container.kind()
            == runtime::SystemVerilogClassContainerKind::UnpackedAggregate;
    if (prefix == read_prefix) {
      const auto value = keyed
          ? container.at(std::to_string(index))
          : container.at(static_cast<std::size_t>(index));
      return runtime::PackedLogic4::from_aval_bval(64, value, 0);
    }
    const auto value = known_word(1);
    if (keyed) {
      container.set(class_heap, std::to_string(index), value);
    } else {
      container.set(class_heap, static_cast<std::size_t>(index), value);
    }
    return runtime::PackedLogic4::from_aval_bval(64, value, 0);
  }

  [[nodiscard]] runtime::PackedLogic4 invoke_checked_class_cast(
      const runtime::SystemVerilogClassHandle handle,
      const std::string_view operation) const {
    constexpr std::string_view prefix{"@checked-cast:"};
    try {
      (void)class_heap.checked_cast(handle, operation.substr(prefix.size()));
      return runtime::PackedLogic4::from_aval_bval(1, 1, 0);
    } catch (const std::invalid_argument&) {
      return runtime::PackedLogic4::from_aval_bval(1, 0, 0);
    }
  }

  [[nodiscard]] static std::pair<std::string_view, std::string_view>
  static_property_parts(const std::string_view identity) {
    const auto separator = identity.rfind("::");
    if (separator == std::string_view::npos
        || separator == 0 || separator + 2U >= identity.size()) {
      throw std::invalid_argument{
          "class static property identity is malformed"};
    }
    return {identity.substr(0, separator), identity.substr(separator + 2U)};
  }

  [[nodiscard]] std::optional<runtime::PackedLogic4>
  evaluate_constructor_expression(
      const frontend::Expression& expression,
      const runtime::SystemVerilogClassHandle handle,
      ConstructorEnvironment& environment) {
    using frontend::ExpressionKind;
    if (expression.kind == ExpressionKind::Identifier) {
      if (expression.text == "this" || expression.text == "super") {
        return runtime::PackedLogic4::from_aval_bval(64, handle, 0);
      }
      const auto found = environment.find(expression.text);
      return found == environment.end()
          ? std::nullopt
          : std::optional{found->second};
    }
    if (expression.kind == ExpressionKind::IntegerLiteral) {
      std::int64_t value{};
      const auto* begin = expression.text.data();
      const auto* end = begin + expression.text.size();
      const auto converted = std::from_chars(begin, end, value, 10);
      if (converted.ec != std::errc{} || converted.ptr != end) {
        return std::nullopt;
      }
      return runtime::PackedLogic4::from_aval_bval(
          64, static_cast<std::uint64_t>(value), 0);
    }
    constexpr std::string_view property_prefix{"@sv-property:"};
    if (expression.kind == ExpressionKind::Call
        && expression.text.starts_with(property_prefix)) {
      return packed_property_value(class_heap.property(
          handle, expression.text.substr(property_prefix.size())));
    }
    constexpr std::string_view static_property_prefix{"@sv-static-property:"};
    if (expression.kind == ExpressionKind::Call
        && expression.text.starts_with(static_property_prefix)) {
      const auto [owner, name] = static_property_parts(
          std::string_view{expression.text}.substr(
              static_property_prefix.size()));
      return packed_property_value(class_static_store.property(owner, name));
    }
    if (expression.kind == ExpressionKind::Call
        && expression.text == "@sv-null") {
      return runtime::PackedLogic4::from_aval_bval(64, 0, 0);
    }
    constexpr std::string_view method_prefix{"@sv-method:"};
    constexpr std::string_view base_method_prefix{"@sv-base-method:"};
    if (expression.kind == ExpressionKind::Call
        && (expression.text.starts_with(method_prefix)
            || expression.text.starts_with(base_method_prefix))
        && !expression.operands.empty()) {
      const auto selected_prefix =
          expression.text.starts_with(base_method_prefix)
          ? base_method_prefix : method_prefix;
      const auto receiver = evaluate_constructor_expression(
          expression.operands.front(), handle, environment);
      if (!receiver || receiver->low_word().bval != 0) return std::nullopt;
      std::vector<runtime::PackedLogic4> actuals;
      for (const auto& operand : expression.operands | std::views::drop(1)) {
        const auto actual = evaluate_constructor_expression(
            operand, handle, environment);
        if (!actual) return std::nullopt;
        actuals.push_back(*actual);
      }
      std::vector<std::string> names(actuals.size());
      if (expression.call_argument_names.size()
          == expression.operands.size()) {
        std::ranges::copy(
            expression.call_argument_names | std::views::drop(1),
            names.begin());
      }
      std::vector<std::uint8_t> directions(actuals.size());
      if (expression.call_argument_directions.size()
          == expression.operands.size()) {
        std::ranges::transform(
            expression.call_argument_directions | std::views::drop(1),
            directions.begin(),
            [](const auto direction) {
              return static_cast<std::uint8_t>(direction);
            });
      }
      std::vector<std::string> string_actuals(actuals.size());
      auto result = invoke_source_function(
          receiver->low_word().aval,
          expression.text.substr(selected_prefix.size()),
          actuals,
          string_actuals,
          names,
          directions,
          selected_prefix == method_prefix);
      for (std::size_t index = 1; index < expression.operands.size(); ++index) {
        const auto direction = index - 1U < directions.size()
            ? static_cast<frontend::PortDirection>(directions[index - 1U])
            : frontend::PortDirection::Input;
        if (direction == frontend::PortDirection::Input
            || expression.operands[index].kind
                != ExpressionKind::Identifier) {
          continue;
        }
        if (const auto found = environment.find(
                expression.operands[index].text);
            found != environment.end()) {
          found->second = resize_packed(
              actuals[index - 1U], found->second.width());
        }
      }
      return result;
    }
    constexpr std::string_view static_method_prefix{"@sv-static-method:"};
    if (expression.kind == ExpressionKind::Call
        && expression.text.starts_with(static_method_prefix)) {
      std::vector<runtime::PackedLogic4> actuals;
      for (const auto& operand : expression.operands) {
        const auto actual = evaluate_constructor_expression(
            operand, handle, environment);
        if (!actual) return std::nullopt;
        actuals.push_back(*actual);
      }
      auto names = expression.call_argument_names;
      if (names.empty()) names.resize(actuals.size());
      std::vector<std::uint8_t> directions;
      std::ranges::transform(
          expression.call_argument_directions,
          std::back_inserter(directions),
          [](const auto direction) {
            return static_cast<std::uint8_t>(direction);
          });
      std::vector<std::string> string_actuals(actuals.size());
      return invoke_source_static_function(
          expression.text.substr(static_method_prefix.size()),
          actuals,
          string_actuals,
          names,
          directions);
    }
    if (expression.kind == ExpressionKind::Unary
        && expression.operands.size() == 1U) {
      const auto operand = evaluate_constructor_expression(
          expression.operands.front(), handle, environment);
      if (!operand) return std::nullopt;
      const auto word = operand->low_word();
      if (word.bval != 0) {
        return runtime::PackedLogic4(64, runtime::Logic4::x);
      }
      if (expression.text == "+") return operand;
      if (expression.text == "-") {
        return runtime::PackedLogic4::from_aval_bval(
            64, std::uint64_t{0} - word.aval, 0);
      }
      if (expression.text == "~") {
        return runtime::PackedLogic4::from_aval_bval(64, ~word.aval, 0);
      }
      if (expression.text == "!") {
        return runtime::PackedLogic4::from_aval_bval(
            1, word.aval == 0 ? 1 : 0, 0);
      }
      return std::nullopt;
    }
    if (expression.kind != ExpressionKind::Binary
        || expression.operands.size() != 2U) {
      return std::nullopt;
    }
    const auto left = evaluate_constructor_expression(
        expression.operands[0], handle, environment);
    const auto right = evaluate_constructor_expression(
        expression.operands[1], handle, environment);
    if (!left || !right) return std::nullopt;
    const auto left_word = left->low_word();
    const auto right_word = right->low_word();
    if (left_word.bval != 0 || right_word.bval != 0) {
      return runtime::PackedLogic4(64, runtime::Logic4::x);
    }
    const auto lhs = left_word.aval;
    const auto rhs = right_word.aval;
    std::uint64_t value{};
    if (expression.text == "+") value = lhs + rhs;
    else if (expression.text == "-") value = lhs - rhs;
    else if (expression.text == "*") value = lhs * rhs;
    else if (expression.text == "/") {
      if (rhs == 0) return std::nullopt;
      value = lhs / rhs;
    } else if (expression.text == "%") {
      if (rhs == 0) return std::nullopt;
      value = lhs % rhs;
    } else if (expression.text == "&") value = lhs & rhs;
    else if (expression.text == "|") value = lhs | rhs;
    else if (expression.text == "^") value = lhs ^ rhs;
    else if (expression.text == "<<") value = rhs < 64 ? lhs << rhs : 0;
    else if (expression.text == ">>") value = rhs < 64 ? lhs >> rhs : 0;
    else if (expression.text == "==" || expression.text == "===") {
      value = lhs == rhs;
    } else if (expression.text == "!=" || expression.text == "!==") {
      value = lhs != rhs;
    } else if (expression.text == "<") value = lhs < rhs;
    else if (expression.text == "<=") value = lhs <= rhs;
    else if (expression.text == ">") value = lhs > rhs;
    else if (expression.text == ">=") value = lhs >= rhs;
    else return std::nullopt;
    return runtime::PackedLogic4::from_aval_bval(64, value, 0);
  }

  [[nodiscard]] ConstructorEnvironment bind_constructor_actuals(
      const frontend::SystemVerilogClassMethodProfile& constructor,
      const runtime::SystemVerilogClassHandle handle,
      const std::span<const runtime::PackedLogic4> actuals,
      const std::span<const std::string> actual_names) {
    if (!actual_names.empty() && actual_names.size() != actuals.size()) {
      throw std::invalid_argument{
          "constructor actual names do not align with values"};
    }
    ConstructorEnvironment environment;
    std::vector<bool> assigned(constructor.arguments.size());
    std::size_t next_positional{};
    for (std::size_t index = 0; index < actuals.size(); ++index) {
      const auto name = actual_names.empty()
          ? std::string_view{}
          : std::string_view{actual_names[index]};
      std::size_t formal_index{};
      if (name.empty()) {
        while (next_positional < assigned.size()
               && assigned[next_positional]) {
          ++next_positional;
        }
        formal_index = next_positional;
      } else {
        const auto found = std::ranges::find(
            constructor.arguments,
            name,
            &frontend::FunctionArgument::name);
        if (found == constructor.arguments.end()) {
          throw std::invalid_argument{
              "constructor has no formal named '" + std::string{name} + "'"};
        }
        formal_index = static_cast<std::size_t>(
            std::distance(constructor.arguments.begin(), found));
      }
      if (formal_index >= constructor.arguments.size()
          || assigned[formal_index]) {
        throw std::invalid_argument{
            "constructor actual cannot be associated with a unique formal"};
      }
      assigned[formal_index] = true;
      const auto width = constructor.arguments[formal_index].type.width();
      const auto actual_width = width
          ? static_cast<std::size_t>(*width)
          : actuals[index].width();
      environment.emplace(
          constructor.arguments[formal_index].name,
          constructor.arguments[formal_index].direction
                  == frontend::PortDirection::Output
              ? runtime::PackedLogic4(actual_width, runtime::Logic4::x)
              : resize_packed(actuals[index], actual_width));
    }
    for (std::size_t index = 0; index < constructor.arguments.size(); ++index) {
      if (assigned[index]) continue;
      const auto& formal = constructor.arguments[index];
      if (!formal.default_value) {
        throw std::invalid_argument{
            "constructor formal '" + formal.name + "' has no actual"};
      }
      if (formal.type.domain == frontend::ValueDomain::String) {
        environment.emplace(
            formal.name, runtime::PackedLogic4(64, runtime::Logic4::zero));
        continue;
      }
      const auto value = evaluate_constructor_expression(
          *formal.default_value, handle, environment);
      if (!value) {
        throw std::invalid_argument{
            "constructor default for '" + formal.name
            + "' is not executable"};
      }
      const auto width = formal.type.width();
      environment.emplace(
          formal.name, resize_packed(*value, width ? *width : value->width()));
    }
    for (const auto& variable : constructor.variables) {
      const auto width = variable.type.width().value_or(64);
      if (variable.type.domain == frontend::ValueDomain::String) {
        environment[variable.name] =
            runtime::PackedLogic4(64, runtime::Logic4::zero);
        continue;
      }
      auto value = variable.initializer
          ? evaluate_constructor_expression(
                *variable.initializer, handle, environment)
          : std::optional<runtime::PackedLogic4>{
                runtime::PackedLogic4(width, runtime::Logic4::x)};
      if (!value) {
        throw std::invalid_argument{
            "constructor local initializer for '" + variable.name
            + "' is not executable"};
      }
      environment[variable.name] = resize_packed(*value, width);
    }
    return environment;
  }

  void execute_constructor_statements(
      const std::span<const frontend::Statement> statements,
      const runtime::SystemVerilogClassHandle handle,
      ConstructorEnvironment& environment,
      const bool native_uvm_library) {
    constexpr std::string_view property_prefix{"@sv-property:"};
    constexpr std::string_view base_prefix{"@sv-base-constructor:"};
    for (const auto& statement : statements) {
      if (statement.kind == frontend::StatementKind::TaskCall
          && statement.task_name.starts_with(base_prefix)) {
        continue;
      }
      if (statement.kind == frontend::StatementKind::Assignment) {
        const auto value = evaluate_constructor_expression(
            statement.value, handle, environment);
        if (!value) {
          if (native_uvm_library) continue;
          throw std::invalid_argument{
              "constructor assignment expression is not executable"};
        }
        if (statement.target.kind == frontend::ExpressionKind::Call
            && statement.target.text.starts_with(property_prefix)) {
          const auto identity = statement.target.text.substr(
              property_prefix.size());
          assign_property_value(
              class_heap.property(handle, identity), identity, *value);
          continue;
        }
        if (statement.target.kind == frontend::ExpressionKind::Identifier) {
          const auto found = environment.find(statement.target.text);
          if (found != environment.end()) {
            found->second = resize_packed(*value, found->second.width());
            continue;
          }
        }
        if (native_uvm_library) continue;
        throw std::invalid_argument{
            "constructor assignment target is not executable"};
      }
      if (statement.kind == frontend::StatementKind::Block) {
        execute_constructor_statements(
            statement.statements, handle, environment, native_uvm_library);
        continue;
      }
      if (statement.kind == frontend::StatementKind::If) {
        const auto condition = evaluate_constructor_expression(
            statement.condition, handle, environment);
        if (!condition || condition->low_word().bval != 0) {
          if (native_uvm_library) continue;
          throw std::invalid_argument{
              "constructor condition is not a known packed value"};
        }
        execute_constructor_statements(
            condition->low_word().aval != 0
                ? std::span<const frontend::Statement>{statement.statements}
                : std::span<const frontend::Statement>{statement.else_statements},
            handle,
            environment,
            native_uvm_library);
        continue;
      }
      if (statement.kind != frontend::StatementKind::Null) {
        if (native_uvm_library) continue;
        throw std::invalid_argument{
            "constructor contains an operation that is not executable"};
      }
    }
  }

#include "application_simulation_source_methods.tpp"

  [[nodiscard]] runtime::SystemVerilogClassHandle allocate_class(
      const std::string_view specialization_identity,
      const std::string_view declared_type,
      const std::string_view allocation_scope = "$api") {
    const auto& specialization = class_specialization(
        specialization_identity);
    runtime::SystemVerilogClassDescriptor descriptor;
    descriptor.dynamic_type = specialization.declaration_identity;
    descriptor.declared_type = declared_type.empty()
        ? descriptor.dynamic_type
        : std::string{declared_type};
    descriptor.specialization_identity =
        specialization.specialization_identity;
    const auto separator = allocation_scope.find('.');
    descriptor.random_root_identity = std::string{
        allocation_scope.substr(0, separator)};
    const auto* current = &specialization;
    while (current != nullptr) {
      descriptor.assignable_declared_types.push_back(
          current->declaration_identity);
      if (current->base_specialization_identity.empty()) break;
      current = &class_specialization(
          current->base_specialization_identity);
    }
    for (const auto& property : specialization.properties) {
      if (!property.is_static) {
        descriptor.properties.push_back(
            class_property_descriptor(property, true));
      }
    }
    const auto declaration = std::ranges::find(
        built.systemverilog_hir.classes(),
        specialization.declaration_identity,
        &semantic::sv::ClassDeclaration::canonical_identity);
    if (declaration != built.systemverilog_hir.classes().end()) {
      for (const auto& constraint : declaration->composed_constraints) {
        if (constraint.override_legal) {
          descriptor.constraint_modes.emplace_back(
              constraint.selected_identity,
              constraint.mode_enabled);
        }
      }
    } else {
      descriptor.constraint_modes = specialization.constraint_modes;
    }
    return class_heap.allocate(descriptor);
  }

  [[nodiscard]] runtime::SystemVerilogClassHandle construct_class(
      const std::string_view specialization_identity,
      const std::string_view declared_type,
      const std::span<const runtime::PackedLogic4> actuals,
      const std::span<const std::string> string_actuals,
      const std::span<const std::string> actual_names,
      const std::string_view allocation_scope,
      const runtime::SystemVerilogUvmRootHandle requested_root = 0) {
    const auto& specialization = class_specialization(
        specialization_identity);
    const auto handle = allocate_class(
        specialization.specialization_identity,
        declared_type,
        allocation_scope);
    const auto before = packed_class_snapshot();
    const auto component_specialization = is_systemverilog_uvm_type(
        built.systemverilog_class_specializations,
        specialization,
        "uvm_component");
    bool object_initialized{};
    bool automatic_root_created{};
    runtime::SystemVerilogUvmRootHandle automatic_root{};
    std::string automatic_root_identity;
    try {
      invoke_source_constructor(
          specialization, handle, actuals, actual_names);
      if (is_systemverilog_uvm_type(
              built.systemverilog_class_specializations,
              specialization,
              "uvm_object")) {
        uvm_objects.initialize(handle);
        object_initialized = true;
        std::size_t name_index{};
        if (!actual_names.empty()) {
          const auto found = std::ranges::find(actual_names, "name");
          if (found != actual_names.end()) {
            name_index = static_cast<std::size_t>(
                std::distance(actual_names.begin(), found));
          }
        }
        if (name_index < string_actuals.size()
            && !string_actuals[name_index].empty()) {
          uvm_objects.set_name(handle, string_actuals[name_index]);
        }
      }
      if (component_specialization) {
        std::size_t name_index{};
        std::size_t parent_index{1U};
        if (!actual_names.empty()) {
          const auto named_index = [&](const std::string_view name,
                                       const std::size_t fallback) {
            const auto found = std::ranges::find(actual_names, name);
            return found == actual_names.end()
                ? fallback
                : static_cast<std::size_t>(
                      std::distance(actual_names.begin(), found));
          };
          name_index = named_index("name", 0U);
          parent_index = named_index("parent", 1U);
        }
        std::string name = name_index < string_actuals.size()
            ? string_actuals[name_index]
            : std::string{};
        if (name.empty()) {
          name = "COMP_" + std::to_string(uvm_objects.instance_id(handle));
        }
        const auto parent = parent_index < actuals.size()
            ? actuals[parent_index].low_word().aval
            : runtime::SystemVerilogClassHandle{};
        auto root = requested_root;
        if (parent == 0 && root == 0) {
          automatic_root_identity = allocation_scope.empty()
              ? "$simulation"
              : std::string{allocation_scope};
          automatic_root_created =
              !uvm_roots_by_scope.contains(automatic_root_identity);
          root = component_root(allocation_scope);
          automatic_root = root;
        }
        uvm_components.initialize(handle, std::move(name), parent, root);
      }
    } catch (...) {
      if (component_specialization) {
        if (uvm_components.contains(handle)) {
          try {
            uvm_components.release(handle);
          } catch (...) {
          }
        } else {
          if (object_initialized) uvm_objects.erase(handle);
          (void)class_heap.release(handle);
        }
        if (automatic_root_created
            && uvm_components.contains_root(automatic_root)) {
          try {
            uvm_phases.unparticipate_standard_root(automatic_root);
          } catch (...) {
          }
          try {
            uvm_components.destroy_root(automatic_root);
          } catch (...) {
          }
          uvm_roots_by_scope.erase(automatic_root_identity);
        }
      }
      if (component_specialization && class_heap.contains(handle)) {
        (void)class_heap.release(handle);
      }
      throw;
    }
    notify_class_changes(before);
    return handle;
  }

  using PackedSnapshot = std::map<
      std::pair<runtime::SystemVerilogClassHandle, std::string>,
      runtime::PackedLogic4>;
  using StaticPackedSnapshot = std::map<
      std::pair<std::string, std::string>, runtime::PackedLogic4>;

  [[nodiscard]] PackedSnapshot packed_class_snapshot() const {
    PackedSnapshot result;
    for (const auto handle : class_heap.live_handles()) {
      const auto& object = class_heap.object(handle);
      for (std::size_t index = 0; index < object.properties.size(); ++index) {
        const auto& property = object.properties[index];
        if (property.packed.width() != 0) {
          result.emplace(
              std::pair{handle, object.property_names[index]},
              property.packed);
        }
      }
    }
    return result;
  }

  void notify_class_changes(const PackedSnapshot& before) {
    if (!class_property_change_hook) return;
    const auto after = packed_class_snapshot();
    for (const auto& [identity, value] : after) {
      const auto prior = before.find(identity);
      if (prior == before.end() || prior->second != value) {
        class_property_change_hook(
            identity.first, identity.second, value,
            interpreter->scheduler().now(),
            interpreter->scheduler().delta());
      }
    }
  }

  [[nodiscard]] StaticPackedSnapshot packed_static_snapshot() const {
    StaticPackedSnapshot result;
    for (const auto& state : class_static_store.snapshots()) {
      for (std::size_t index = 0; index < state.properties.size(); ++index) {
        if (state.properties[index].packed.width() != 0) {
          result.emplace(
              std::pair{state.specialization_identity,
                        state.property_names[index]},
              state.properties[index].packed);
        }
      }
    }
    return result;
  }

  void notify_static_changes(const StaticPackedSnapshot& before) {
    if (!class_static_property_change_hook) return;
    for (const auto& [identity, value] : packed_static_snapshot()) {
      const auto prior = before.find(identity);
      if (prior == before.end() || prior->second != value) {
        class_static_property_change_hook(
            identity.first, identity.second, value,
            interpreter->scheduler().now(), interpreter->scheduler().delta());
      }
    }
  }

  [[nodiscard]] runtime::SystemVerilogClassInvocationResult
  invoke_class_method(
      const std::string_view canonical_method,
      const runtime::SystemVerilogClassHandle this_handle,
      std::vector<runtime::SystemVerilogClassMethodValue>& actuals,
      const std::optional<std::uint32_t> virtual_slot) {
    const auto before = packed_class_snapshot();
    const auto static_before = packed_static_snapshot();
    auto result = virtual_slot
        ? class_methods.invoke_virtual(*virtual_slot, this_handle, actuals)
        : class_methods.invoke(canonical_method, this_handle, actuals);
    notify_class_changes(before);
    notify_static_changes(static_before);
    return result;
  }

#include "application_simulation_uvm_phase.tpp"

  [[nodiscard]] std::optional<runtime::simir::SignalId>
  backdoor_signal(const std::string_view path) const noexcept {
    const auto found = std::ranges::find_if(
        built.design_ir.objects(), [&](const auto &object) {
          return design_object_is_signal_bearing(object) &&
                 object.path == path &&
                 object.runtime_index <=
                     std::numeric_limits<runtime::simir::SignalId>::max();
        });
    return found == built.design_ir.objects().end()
               ? std::nullopt
               : std::optional<runtime::simir::SignalId>{static_cast<
                     runtime::simir::SignalId>(found->runtime_index)};
  }

  ~Impl() {
    if (systemc_start_attempted && !systemc_ended) {
      try {
        end_systemc();
      } catch (...) {
      }
    }
  }

  void start_systemc() {
    if (systemc_start_attempted) {
      return;
    }
    systemc_start_attempted = true;
    for_each_systemc_registry([&](auto& registry, const auto& roots) {
      registry.start_simulation(roots);
    });
  }

  void end_systemc() {
    if (!systemc_start_attempted || systemc_ended) {
      return;
    }
    for_each_systemc_registry([&](auto& registry, const auto& roots) {
      registry.end_simulation(roots);
    });
    systemc_ended = true;
  }

  template <typename Callback>
  void for_each_systemc_registry(Callback&& callback) {
    if (built.systemc_hierarchies.empty()) {
      if (built.systemc_hierarchy) {
        callback(*built.systemc_hierarchy, built.systemc_roots);
      }
      return;
    }
    for (const auto& registry : built.systemc_hierarchies) {
      std::vector<std::uint64_t> roots;
      std::ranges::copy_if(
          built.systemc_roots,
          std::back_inserter(roots),
          [&](const auto handle) {
            return registry->owns_handle(handle);
          });
      callback(*registry, roots);
    }
  }

  BuiltProject built;
  runtime::SystemVerilogClassHeap class_heap;
  runtime::SystemVerilogChandleRegistry chandle_registry;
  runtime::SystemVerilogClassStaticStore class_static_store;
  runtime::SystemVerilogClassMethodRuntime class_methods;
  runtime::SystemVerilogUvmObjectService uvm_objects;
  runtime::SystemVerilogUvmComponentService uvm_components;
  runtime::SystemVerilogUvmActivityService uvm_activity;
  runtime::SystemVerilogUvmPhaseService uvm_phases;
  runtime::SystemVerilogUvmObjectionService uvm_objections;
  runtime::SystemVerilogUvmTlm1Service uvm_tlm1;
  runtime::SystemVerilogUvmTlm2Service uvm_tlm2;
  runtime::SystemVerilogUvmSequenceService uvm_sequences;
  runtime::SystemVerilogUvmCallbackService uvm_callbacks;
  runtime::SystemVerilogUvmTransactionRecorderService uvm_transactions;
  runtime::SystemVerilogUvmRegisterModelService uvm_register_model;
  runtime::SystemVerilogUvmForeignService uvm_foreign;
  runtime::SystemVerilogUvmRegistryService uvm_registry;
  runtime::SystemVerilogUvmFactoryService uvm_factory;
  runtime::SystemVerilogUvmResourcePoolService uvm_resources;
  runtime::SystemVerilogUvmConfigDbService uvm_config_db;
  runtime::SystemVerilogUvmCommandLineService uvm_command_line;
  runtime::SystemVerilogUvmReportService uvm_reports;
  std::map<std::string, runtime::SystemVerilogUvmRootHandle, std::less<>>
      uvm_roots_by_scope;
#if defined(FSIM_HAS_LLVM)
  // Shared by every compiled executor. It is fully populated before executor
  // installation and outlives the interpreter that owns those executors.
  std::vector<std::uint32_t> signal_widths;
  std::vector<runtime::simir::ValueKind>
      signal_value_kinds;
  // The interpreter owns executors referring to this JIT. Member destruction
  // is reversed, so declaring the JIT first destroys the interpreter first.
  std::unique_ptr<compiler::LlvmJit> jit;
#endif
  std::unique_ptr<runtime::simir::Interpreter> interpreter;
  std::size_t compiled_processes{};
  std::size_t compiled_modules{};
  SignalChangeHook signal_change_hook;
  std::map<std::uint64_t, SignalChangeHook> signal_observers;
  std::uint64_t next_signal_observer{1};
  ScalarSignalChangeHook scalar_signal_change_hook;
  std::map<std::uint64_t, ScalarSignalChangeHook> scalar_signal_observers;
  std::uint64_t next_scalar_signal_observer{1};
  SafePointHook safe_point_hook;
  std::map<std::uint64_t, SafePointHook> safe_point_observers;
  std::uint64_t next_safe_point_observer{1};
  OutputHook output_hook;
  ReportHook report_hook;
  std::vector<ConcurrentAssertionCoverage>
      concurrent_assertion_coverage;
  std::vector<ConcurrentAssertionEvent>
      concurrent_assertion_events;
  ConcurrentAssertionHook concurrent_assertion_hook;
  std::map<std::uint32_t, std::size_t>
      concurrent_assertion_indices;
  std::map<std::uint32_t, bool>
      concurrent_assertion_actions_suppressed;
  bool concurrent_assertions_enabled{true};
  bool concurrent_assertion_pass_actions_enabled{true};
  bool concurrent_assertion_failure_actions_enabled{true};
  ClassPropertyChangeHook class_property_change_hook;
  ClassStaticPropertyChangeHook class_static_property_change_hook;
  std::map<std::string, ConstructorEnvironment> source_static_locals_;
  std::size_t source_method_depth_{};
  Lifecycle lifecycle{Lifecycle::ready};
  bool systemc_start_attempted{};
  bool systemc_ended{};
};

Simulation::Simulation(
    BuiltProject project,
    const std::uint64_t max_deltas,
    const SimulationEngine engine)
    : impl_(
          std::make_unique<Impl>(
              std::move(project), max_deltas, engine)) {}
Simulation::~Simulation() = default;
Simulation::Simulation(Simulation&&) noexcept = default;
Simulation& Simulation::operator=(Simulation&&) noexcept = default;

#include "application_simulation_accessors.tpp"

const runtime::SystemVerilogClassPropertyValue&
Simulation::read_class_property(
    const runtime::SystemVerilogClassHandle handle,
    const std::string_view property) const {
  return impl_->class_heap.property(handle, property);
}

void Simulation::deposit_class_property(
    const runtime::SystemVerilogClassHandle handle,
    const std::string_view property,
    runtime::PackedLogic4 value) {
  if (impl_->lifecycle == Impl::Lifecycle::finished
      || impl_->lifecycle == Impl::Lifecycle::poisoned) {
    throw std::logic_error{"simulation class heap is no longer mutable"};
  }
  auto& destination = impl_->class_heap.property(handle, property);
  if (destination.packed.width() == 0
      || destination.packed.width() != value.width()) {
    throw std::invalid_argument{
        "class property deposit requires an equal-width packed property"};
  }
  if (destination.kind == runtime::SystemVerilogClassPropertyKind::Bit2) {
    for (std::size_t bit = 0; bit < value.width(); ++bit) {
      if (value.get(bit) != runtime::Logic4::zero
          && value.get(bit) != runtime::Logic4::one) {
        throw std::invalid_argument{
            "class property deposit would place X/Z into two-state storage"};
      }
    }
  }
  destination.packed = std::move(value);
  if (impl_->class_property_change_hook) {
    impl_->class_property_change_hook(
        handle, property, destination.packed,
        impl_->interpreter->scheduler().now(),
        impl_->interpreter->scheduler().delta());
  }
}

runtime::SystemVerilogClassInvocationResult
Simulation::invoke_class_method(
    const std::string_view canonical_method,
    const runtime::SystemVerilogClassHandle this_handle,
    std::vector<runtime::SystemVerilogClassMethodValue>& actuals,
    const std::optional<std::uint32_t> virtual_slot) {
  if (impl_->lifecycle == Impl::Lifecycle::finished
      || impl_->lifecycle == Impl::Lifecycle::poisoned) {
    throw std::logic_error{"simulation class methods are no longer mutable"};
  }
  return impl_->invoke_class_method(
      canonical_method, this_handle, actuals, virtual_slot);
}

runtime::SystemVerilogUvmPhaseExecutionResult
Simulation::execute_uvm_function_phase(
    const runtime::SystemVerilogUvmPhaseHandle phase) {
  if (impl_->lifecycle == Impl::Lifecycle::finished
      || impl_->lifecycle == Impl::Lifecycle::poisoned) {
    throw std::logic_error{"simulation UVM phases are no longer mutable"};
  }
  return impl_->execute_uvm_function_phase(phase);
}

runtime::SystemVerilogUvmPhaseExecutionResult
Simulation::execute_uvm_task_phase(
    const runtime::SystemVerilogUvmPhaseHandle phase,
    const UvmTaskPhaseContinuation& continuation) {
  if (impl_->lifecycle == Impl::Lifecycle::finished
      || impl_->lifecycle == Impl::Lifecycle::poisoned) {
    throw std::logic_error{"simulation UVM phases are no longer mutable"};
  }
  return impl_->execute_uvm_task_phase(phase, continuation);
}

void Simulation::schedule_class_method(
    const runtime::SimulationTick time,
    const runtime::StableOrder stable_order,
    std::string canonical_method,
    const runtime::SystemVerilogClassHandle this_handle,
    std::vector<runtime::SystemVerilogClassMethodValue> actuals,
    const std::optional<std::uint32_t> virtual_slot,
    ClassMethodCompletion completion) {
  if (impl_->lifecycle != Impl::Lifecycle::ready) {
    throw std::logic_error{
        "class methods may only be scheduled on a ready simulation"};
  }
  impl_->interpreter->scheduler().schedule_at(
      time,
      runtime::SchedulerPhase::active,
      stable_order,
      [implementation = impl_.get(),
       canonical_method = std::move(canonical_method),
       this_handle,
       actuals = std::move(actuals),
       virtual_slot,
       completion = std::move(completion)](runtime::Scheduler&) mutable {
        const auto result = implementation->invoke_class_method(
            canonical_method, this_handle, actuals, virtual_slot);
        if (completion) completion(result, actuals);
      });
}

void Simulation::deposit_string_object(
    const runtime::simir::StringObjectId object,
    const std::string_view value) {
  impl_->interpreter->deposit_string_object(object, value);
}

void Simulation::deposit_container_object(
    const runtime::simir::ContainerObjectId object,
    runtime::simir::ContainerValue value) {
  impl_->interpreter->deposit_container_object(
      object, std::move(value));
}

void Simulation::release_signal(const SignalId signal) {
  impl_->interpreter->release_signal(signal);
}

bool Simulation::signal_is_forced(const SignalId signal) const {
  return impl_->interpreter->signal_is_forced(signal);
}

void Simulation::start() {
  if (impl_->lifecycle != Impl::Lifecycle::ready) {
    throw std::logic_error{"simulation is not ready to start"};
  }
  try {
    impl_->start_systemc();
    impl_->interpreter->start();
  } catch (...) {
    impl_->lifecycle = Impl::Lifecycle::poisoned;
    throw;
  }
}

runtime::RunResult Simulation::run(
    const std::optional<SimulationTick> until) {
  if (impl_->lifecycle == Impl::Lifecycle::poisoned) {
    throw std::logic_error(
        "simulation is unavailable after a fatal runtime error");
  }
  if (impl_->lifecycle == Impl::Lifecycle::finished) {
    throw std::logic_error("simulation has finished");
  }
  if (until && *until < now()) {
    throw std::invalid_argument("run time limit is before the current time");
  }
  try {
    impl_->start_systemc();
    auto result = impl_->interpreter->run(until);
    if (result.status == runtime::RunStatus::completed
        || impl_->interpreter->stopped_by_design()) {
      impl_->end_systemc();
      impl_->lifecycle = Impl::Lifecycle::finished;
    }
    return result;
  } catch (...) {
    impl_->lifecycle = Impl::Lifecycle::poisoned;
    throw;
  }
}

void Simulation::request_stop() noexcept {
  impl_->interpreter->scheduler().request_stop();
}

void Simulation::clear_stop() noexcept {
  if (impl_->lifecycle == Impl::Lifecycle::ready) {
    impl_->interpreter->scheduler().clear_stop();
  }
}

SimulationTick Simulation::now() const noexcept {
  return impl_->interpreter->scheduler().now();
}

std::uint64_t Simulation::delta() const noexcept {
  return impl_->interpreter->scheduler().delta();
}

bool Simulation::has_pending() const noexcept {
  return impl_->interpreter->scheduler().has_pending();
}

bool Simulation::finished() const noexcept {
  return impl_->lifecycle == Impl::Lifecycle::finished;
}

bool Simulation::poisoned() const noexcept {
  return impl_->lifecycle == Impl::Lifecycle::poisoned;
}

std::size_t Simulation::compiled_process_count() const noexcept {
  return impl_->compiled_processes;
}

std::size_t Simulation::compiled_module_count() const noexcept {
  return impl_->compiled_modules;
}

NativeCacheStatistics Simulation::native_cache_statistics() const noexcept {
#if defined(FSIM_HAS_LLVM)
  if (impl_->jit) {
    const auto statistics = impl_->jit->cache_statistics();
    return {
        statistics.hits,
        statistics.misses,
        statistics.stores,
        statistics.rejected_entries,
        statistics.load_failures,
        statistics.store_failures,
        statistics.pruned_entries,
        statistics.pruned_bytes,
        statistics.prune_failures,
    };
  }
#endif
  return {};
}

std::vector<ConcurrentAssertionCoverage>
Simulation::concurrent_assertion_coverage() const {
  auto result = impl_->concurrent_assertion_coverage;
  std::ranges::sort(
      result, {}, &ConcurrentAssertionCoverage::process);
  return result;
}

const std::vector<ConcurrentAssertionEvent>&
Simulation::concurrent_assertion_events() const noexcept {
  return impl_->concurrent_assertion_events;
}

#include "application_simulation_scalar.tpp"

void Simulation::set_safe_point_hook(SafePointHook hook) {
  impl_->safe_point_hook = std::move(hook);
}

std::uint64_t Simulation::add_safe_point_hook(SafePointHook hook) {
  if (!hook) {
    throw std::invalid_argument("safe-point observer cannot be empty");
  }
  if (impl_->next_safe_point_observer == 0) {
    throw std::overflow_error("safe-point observer token space exhausted");
  }
  const auto token = impl_->next_safe_point_observer++;
  impl_->safe_point_observers.emplace(token, std::move(hook));
  return token;
}

void Simulation::remove_safe_point_hook(const std::uint64_t token) noexcept {
  impl_->safe_point_observers.erase(token);
}

void Simulation::set_execution_point_hook(ExecutionPointHook hook) {
  impl_->interpreter->set_execution_point_hook(std::move(hook));
}

void Simulation::set_output_hook(OutputHook hook) {
  impl_->output_hook = std::move(hook);
}

void Simulation::set_report_hook(ReportHook hook) {
  impl_->report_hook = std::move(hook);
}

void Simulation::set_concurrent_assertion_hook(
    ConcurrentAssertionHook hook) {
  impl_->concurrent_assertion_hook = std::move(hook);
}

std::uint64_t Simulation::add_uvm_activity_hook(UvmActivityHook hook) {
  return impl_->uvm_activity.add_observer(std::move(hook));
}

void Simulation::remove_uvm_activity_hook(
    const std::uint64_t token) noexcept {
  impl_->uvm_activity.remove_observer(token);
}

void Simulation::set_class_property_change_hook(
    ClassPropertyChangeHook hook) {
  impl_->class_property_change_hook = std::move(hook);
}

void Simulation::set_class_static_property_change_hook(
    ClassStaticPropertyChangeHook hook) {
  impl_->class_static_property_change_hook = std::move(hook);
}


} // namespace fsim::app
