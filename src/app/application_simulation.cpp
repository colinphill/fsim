// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"

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
        interpreter(built.design.create_interpreter(
            runtime::SchedulerOptions{max_deltas, 32},
            built.seed)) {
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
                specialization.id.value());
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
    interpreter->set_output_hook(
        [this](
            const runtime::simir::ProcessId process,
            const std::string_view text,
            const bool newline,
            const SimulationTick time,
            const std::uint64_t delta) {
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
  SafePointHook safe_point_hook;
  std::map<std::uint64_t, SafePointHook> safe_point_observers;
  std::uint64_t next_safe_point_observer{1};
  OutputHook output_hook;
  ReportHook report_hook;
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

const elaboration::ElaboratedDesign& Simulation::design() const noexcept {
  return runtime_adapter();
}

const elaboration::ElaboratedDesign&
Simulation::runtime_adapter() const noexcept {
  return impl_->built.design;
}

const semantic::design::DesignIr& Simulation::design_ir() const noexcept {
  return impl_->built.design_ir;
}

const semantic::Model& Simulation::semantics() const noexcept {
  return impl_->built.semantics;
}

const std::vector<MappedLibraryProvenance>&
Simulation::mapped_libraries() const noexcept {
  return impl_->built.mapped_libraries;
}

std::string_view Simulation::time_resolution() const noexcept {
  return impl_->built.time_resolution;
}

std::optional<SignalId> Simulation::find_signal(
    const std::string_view path) const noexcept {
  const auto found = std::ranges::find_if(
      impl_->built.design_ir.objects(), [&](const auto& object) {
        return design_object_is_signal_bearing(object)
            && object.path == path
            && object.runtime_index
                <= std::numeric_limits<SignalId>::max();
      });
  return found == impl_->built.design_ir.objects().end()
      ? std::nullopt
      : std::optional<SignalId>{
            static_cast<SignalId>(found->runtime_index)};
}

const PackedLogic4& Simulation::read_signal(const SignalId signal) const {
  return impl_->interpreter->signal_value(signal);
}

const PackedLogic4& Simulation::read_driver(
    const runtime::simir::ProcessId process,
    const SignalId signal) const {
  return impl_->interpreter->driver_value(process, signal);
}

PackedLogic4 Simulation::read_process_local(
    const runtime::simir::ProcessId process,
    const std::size_t local_index) const {
  return impl_->interpreter->read_debug_local(process, local_index);
}

std::string Simulation::read_process_string_local(
    const runtime::simir::ProcessId process,
    const std::size_t local_index) const {
  return impl_->interpreter->read_debug_string_local(
      process, local_index);
}

runtime::simir::ContainerValue
Simulation::read_process_container_local(
    const runtime::simir::ProcessId process,
    const std::size_t local_index) const {
  return impl_->interpreter->read_debug_container_local(
      process, local_index);
}

const std::string& Simulation::read_string_object(
    const runtime::simir::StringObjectId object) const {
  return impl_->interpreter->string_object_value(object);
}

const runtime::simir::ContainerValue&
Simulation::read_container_object(
    const runtime::simir::ContainerObjectId object) const {
  return impl_->interpreter->container_object_value(object);
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

void Simulation::deposit_signal(
    const SignalId signal,
    PackedLogic4 value) {
  impl_->validate_external_value(signal, value, "deposit");
  impl_->interpreter->deposit_signal(signal, std::move(value));
}

void Simulation::force_signal(
    const SignalId signal,
    PackedLogic4 value) {
  impl_->validate_external_value(signal, value, "force");
  impl_->interpreter->force_signal(signal, std::move(value));
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

void Simulation::set_signal_change_hook(SignalChangeHook hook) {
  impl_->signal_change_hook = std::move(hook);
}

std::uint64_t Simulation::add_signal_change_hook(SignalChangeHook hook) {
  if (!hook) {
    throw std::invalid_argument("signal change observer cannot be empty");
  }
  if (impl_->next_signal_observer == 0) {
    throw std::overflow_error("signal change observer token space exhausted");
  }
  const auto token = impl_->next_signal_observer++;
  impl_->signal_observers.emplace(token, std::move(hook));
  return token;
}

void Simulation::remove_signal_change_hook(const std::uint64_t token) noexcept {
  impl_->signal_observers.erase(token);
}

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


} // namespace fsim::app
