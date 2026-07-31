// SPDX-License-Identifier: Apache-2.0
#include "simir_internal.hpp"

namespace fsim::runtime::simir {

Interpreter::Interpreter(
    SchedulerOptions options,
    const std::uint64_t seed)
    : impl_(std::make_unique<Impl>(options, seed)) {}
Interpreter::~Interpreter() = default;
Interpreter::Interpreter(Interpreter &&) noexcept = default;
Interpreter &Interpreter::operator=(Interpreter &&) noexcept = default;

void Interpreter::set_file_root(std::filesystem::path root) {
  impl_->set_file_root(std::move(root));
}

SignalId Interpreter::add_signal(Signal signal) {
  if (impl_->started) {
    throw std::logic_error("cannot add a SimIR signal after start");
  }
  const auto id = static_cast<SignalId>(impl_->signals.size());
  if (static_cast<std::size_t>(id) != impl_->signals.size()) {
    throw std::length_error("too many SimIR signals");
  }
  impl_->driven_values.push_back(signal.initial_value);
  impl_->driver_values.emplace_back();
  impl_->external_driver_values.emplace_back();
  impl_->signal_last_values.push_back(signal.initial_value);
  impl_->forced_values.emplace_back();
  impl_->signals.push_back(std::move(signal));
  impl_->static_fanout.emplace_back();
  impl_->dynamic_fanout.emplace_back();
  impl_->event_states.emplace_back();
  impl_->signal_events.emplace_back();
  impl_->signal_transactions.emplace_back();
  return id;
}

StringObjectId Interpreter::add_string_object(StringObject object) {
  if (impl_->started) {
    throw std::logic_error{
        "cannot add a SimIR string object after start"};
  }
  if (object.initial_value.size() > maximum_string_bytes) {
    throw std::length_error{"SimIR string object exceeds byte limit"};
  }
  const auto id =
      static_cast<StringObjectId>(impl_->string_objects.size());
  if (static_cast<std::size_t>(id)
      != impl_->string_objects.size()) {
    throw std::length_error{"too many SimIR string objects"};
  }
  impl_->string_objects.push_back(std::move(object));
  return id;
}

ContainerObjectId Interpreter::add_container_object(
    ContainerObject object) {
  if (impl_->started) {
    throw std::logic_error{
        "cannot add a SimIR container object after start"};
  }
  validate_container_value(object.initial_value);
  const auto id = static_cast<ContainerObjectId>(
      impl_->container_objects.size());
  if (static_cast<std::size_t>(id)
      != impl_->container_objects.size()) {
    throw std::length_error{"too many SimIR container objects"};
  }
  if (object.slice_alias) {
    const auto& alias = *object.slice_alias;
    if (alias.object >= id) {
      throw std::invalid_argument{
          "a SimIR container slice alias must reference an earlier object"};
    }
    const auto& source =
        impl_->container_objects[alias.object].initial_value.type;
    const auto& target = object.initial_value.type;
    const auto element_count =
        [](const std::int32_t left,
           const std::int32_t right) {
          return static_cast<std::uint64_t>(
                     left >= right
                         ? static_cast<std::int64_t>(left) - right
                         : static_cast<std::int64_t>(right) - left)
              + 1U;
        };
    const auto source_low =
        std::min(source.index_left, source.index_right);
    const auto source_high =
        std::max(source.index_left, source.index_right);
    const bool direction_matches =
        alias.selected_left == alias.selected_right
        || (alias.selected_left >= alias.selected_right)
            == (source.index_left >= source.index_right);
    if (!source.fixed || !target.fixed
        || alias.selected_left < source_low
        || alias.selected_left > source_high
        || alias.selected_right < source_low
        || alias.selected_right > source_high
        || !direction_matches
        || element_count(
               alias.selected_left,
               alias.selected_right)
            != element_count(
                target.index_left, target.index_right)
        || source.element_width != target.element_width
        || source.two_state != target.two_state
        || source.signed_elements != target.signed_elements) {
      throw std::invalid_argument{
          "invalid SimIR static-array slice alias"};
    }
  }
  impl_->container_objects.push_back(std::move(object));
  return id;
}

ProcessId Interpreter::add_process(Process process) {
  if (impl_->started) {
    throw std::logic_error("cannot add a SimIR process after start");
  }
  const auto id = static_cast<ProcessId>(impl_->processes.size());
  if (static_cast<std::size_t>(id) != impl_->processes.size()) {
    throw std::length_error("too many SimIR processes");
  }
  if (process.id != id) {
    throw std::invalid_argument("SimIR process IDs must be dense and ordered");
  }
  if (process.final && process.initialize) {
    throw std::invalid_argument(
        "a SimIR final process cannot initialize at time zero");
  }
  if (!process.register_value_kinds.empty()
      && process.register_value_kinds.size()
          != process.register_count) {
    throw std::invalid_argument(
        "SimIR register value-kind count does not match register_count");
  }
  for (const auto signal : process.static_sensitivity) {
    if (signal.signal >= impl_->signals.size()) {
      throw std::invalid_argument("process sensitivity references invalid signal");
    }
    if (signal.edge != EdgeKind::any &&
        impl_->signals[signal.signal].initial_value.width() != 1) {
      throw std::invalid_argument(
          "edge sensitivity currently requires a scalar signal");
    }
    impl_->static_fanout[signal.signal].push_back({id, signal.edge});
  }
  std::set<std::string> local_names;
  for (const auto& local : process.debug_locals) {
    if (local.name.empty() || local.width == 0
        || local.register_id >= process.register_count) {
      throw std::invalid_argument{"invalid SimIR debug-local metadata"};
    }
    if (!local_names.insert(local.name).second) {
      throw std::invalid_argument{"duplicate SimIR debug-local name"};
    }
  }
  std::set<std::string> string_local_names;
  for (const auto& local : process.debug_string_locals) {
    if (local.name.empty()
        || local.register_id >= process.string_register_count) {
      throw std::invalid_argument{
          "invalid SimIR string debug-local metadata"};
    }
    if (!string_local_names.insert(local.name).second
        || local_names.contains(local.name)) {
      throw std::invalid_argument{
          "duplicate SimIR debug-local name"};
    }
  }
  if (process.container_register_types.size()
      != process.container_register_count) {
    throw std::invalid_argument{
        "SimIR container register type count does not match register count"};
  }
  std::set<std::string> container_local_names;
  for (const auto& local : process.debug_container_locals) {
    if (local.name.empty()
        || local.register_id >= process.container_register_count
        || local.type != process.container_register_types.at(
            local.register_id)) {
      throw std::invalid_argument{
          "invalid SimIR container debug-local metadata"};
    }
    if (!container_local_names.insert(local.name).second
        || local_names.contains(local.name)
        || string_local_names.contains(local.name)) {
      throw std::invalid_argument{
          "duplicate SimIR debug-local name"};
    }
  }
  std::set<SignalId> outputs;
  for (const auto& operation : process.operations) {
    const auto signal = output_signal(operation);
    if (!signal) {
      continue;
    }
    if (*signal >= impl_->signals.size()) {
      throw std::invalid_argument(
          "process output references invalid signal");
    }
    outputs.insert(*signal);
  }
  for (const auto signal : outputs) {
    impl_->register_driver(id, signal);
  }

  Impl::ProcessState state;
  state.registers.assign(process.register_count, PackedLogic4{});
  state.string_registers.assign(process.string_register_count, {});
  state.container_registers.reserve(
      process.container_register_count);
  for (const auto& type : process.container_register_types) {
    state.container_registers.push_back(
        default_container_value(type));
  }
  state.random_state = Impl::initial_random_state(
      impl_->root_seed, id);
  state.waiting_on_static = !process.initialize;
  state.program = std::move(process);
  impl_->processes.push_back(std::move(state));
  return id;
}

void Interpreter::set_process_executor(
    const ProcessId process,
    std::unique_ptr<ProcessExecutor> executor) {
  if (impl_->started) {
    throw std::logic_error(
        "cannot install a SimIR process executor after start");
  }
  if (!executor) {
    throw std::invalid_argument("SimIR process executor cannot be null");
  }
  auto& state = impl_->get_process(process);
  if (state.executor) {
    throw std::logic_error(
        "a SimIR process executor is already installed");
  }
  state.executor = std::move(executor);
}

void Interpreter::start() {
  if (impl_->started) {
    return;
  }
  impl_->started = true;
  for (ProcessId id = 0; id < impl_->processes.size(); ++id) {
    if (impl_->processes[id].program.initialize
        && !impl_->processes[id].program.final) {
      impl_->queue_at(id, impl_->scheduler.now());
    }
  }
}

RunResult Interpreter::run(std::optional<SimulationTick> until) {
  start();
  auto ordinary = impl_->scheduler.run(until);
  const bool design_stop =
      ordinary.status == RunStatus::stopped
      && impl_->stopped_by_design;
  if (impl_->finals_ran
      || (ordinary.status != RunStatus::completed
          && !design_stop)) {
    return ordinary;
  }

  impl_->finals_ran = true;
  if (design_stop) {
    impl_->scheduler.discard_pending();
    impl_->scheduler.clear_stop();
  }
  for (ProcessId id = 0; id < impl_->processes.size(); ++id) {
    if (impl_->processes[id].program.final) {
      impl_->queue_at(id, impl_->scheduler.now());
    }
  }
  if (!impl_->scheduler.has_pending()) {
    if (design_stop) {
      impl_->scheduler.request_stop();
    }
    return ordinary;
  }

  const auto final_result = impl_->scheduler.run();
  ordinary.time = final_result.time;
  ordinary.delta = final_result.delta;
  ordinary.callbacks_executed += final_result.callbacks_executed;
  if (design_stop) {
    ordinary.status = RunStatus::stopped;
    impl_->scheduler.request_stop();
  } else {
    ordinary.status = final_result.status;
  }
  return ordinary;
}

void Interpreter::deposit_signal(SignalId signal, PackedLogic4 value) {
  impl_->commit(signal, std::move(value));
}

void Interpreter::force_signal(SignalId signal, PackedLogic4 value) {
  if (impl_->get_signal(signal).initial_value.width() != value.width()) {
    throw std::invalid_argument("SimIR signal force width mismatch");
  }
  value = impl_->normalize_signal_value(
      signal, std::move(value));
  impl_->forced_values[signal] = value;
  impl_->publish(signal, std::move(value));
}

void Interpreter::release_signal(SignalId signal) {
  (void)impl_->get_signal(signal);
  if (!impl_->forced_values[signal].has_value()) {
    return;
  }
  impl_->forced_values[signal].reset();
  impl_->publish(signal, impl_->driven_values[signal]);
}

bool Interpreter::signal_is_forced(const SignalId signal) const {
  (void)impl_->get_signal(signal);
  return impl_->forced_values[signal].has_value();
}

void Interpreter::schedule_signal_at(SignalId signal, PackedLogic4 value,
                                     SimulationTick time, StableOrder order) {
  // Validate eagerly so a malformed drive does not fail much later.
  if (impl_->get_signal(signal).initial_value.width() != value.width()) {
    throw std::invalid_argument("SimIR signal assignment width mismatch");
  }
  impl_->scheduler.schedule_at(
      time, SchedulerPhase::update, order,
      [state = impl_.get(), signal, value = std::move(value)](
          Scheduler &) mutable {
        state->stage_update(signal, std::move(value));
      });
}

void Interpreter::schedule_signal_after(SignalId signal, PackedLogic4 value,
                                        SimulationTick delay,
                                        StableOrder order) {
  if (delay >
      std::numeric_limits<SimulationTick>::max() - impl_->scheduler.now()) {
    throw std::overflow_error("simulation time overflow scheduling signal");
  }
  schedule_signal_at(signal, std::move(value), impl_->scheduler.now() + delay,
                     order);
}

const PackedLogic4 &Interpreter::signal_value(SignalId signal) const {
  return impl_->get_signal(signal).initial_value;
}

const std::string& Interpreter::string_object_value(
    const StringObjectId object) const {
  return impl_->get_string_object(object).initial_value;
}

void Interpreter::deposit_string_object(
    const StringObjectId object,
    const std::string_view value) {
  if (value.size() > maximum_string_bytes) {
    throw std::length_error{
        "SimIR string object exceeds byte limit"};
  }
  impl_->get_string_object(object).initial_value = value;
}

const ContainerValue& Interpreter::container_object_value(
    const ContainerObjectId object) const {
  return impl_->read_container_object_value(object);
}

void Interpreter::deposit_container_object(
    const ContainerObjectId object,
    ContainerValue value) {
  impl_->write_container_object_value(object, value);
}

const PackedLogic4& Interpreter::driver_value(
    const ProcessId process,
    const SignalId signal) const {
  (void)impl_->get_process(process);
  return impl_->current_driver_value(process, signal);
}

PackedLogic4 Interpreter::read_debug_local(
    const ProcessId process,
    const std::size_t local_index) const {
  auto& state = impl_->get_process(process);
  if (local_index >= state.program.debug_locals.size()) {
    throw std::out_of_range{"invalid SimIR debug-local index"};
  }
  const auto& local = state.program.debug_locals[local_index];
  if (state.executor) {
    return state.executor->read_register(
        local.register_id, local.width);
  }
  const auto& value = state.registers.at(local.register_id);
  if (value.width() != local.width) {
    throw std::logic_error{"SimIR debug local has not been initialized"};
  }
  return value;
}

std::string Interpreter::read_debug_string_local(
    const ProcessId process,
    const std::size_t local_index) const {
  auto& state = impl_->get_process(process);
  if (local_index >= state.program.debug_string_locals.size()) {
    throw std::out_of_range{"invalid SimIR string debug-local index"};
  }
  const auto& local =
      state.program.debug_string_locals[local_index];
  if (state.executor) {
    return state.executor->read_string_register(local.register_id);
  }
  return state.string_registers.at(local.register_id);
}

ContainerValue Interpreter::read_debug_container_local(
    const ProcessId process,
    const std::size_t local_index) const {
  auto& state = impl_->get_process(process);
  if (local_index >= state.program.debug_container_locals.size()) {
    throw std::out_of_range{
        "invalid SimIR container debug-local index"};
  }
  const auto& local =
      state.program.debug_container_locals[local_index];
  if (state.executor) {
    return state.executor->read_container_register(local.register_id);
  }
  return state.container_registers.at(local.register_id);
}

bool Interpreter::stopped_by_design() const noexcept {
  return impl_->stopped_by_design;
}

Scheduler &Interpreter::scheduler() noexcept { return impl_->scheduler; }
const Scheduler &Interpreter::scheduler() const noexcept {
  return impl_->scheduler;
}

void Interpreter::set_signal_change_hook(SignalChangeHook hook) {
  impl_->signal_change_hook = std::move(hook);
}

void Interpreter::set_execution_point_hook(ExecutionPointHook hook) {
  impl_->execution_point_hook = std::move(hook);
}

void Interpreter::set_output_hook(OutputHook hook) {
  impl_->output_hook = std::move(hook);
}

void Interpreter::set_report_hook(ReportHook hook) {
  impl_->report_hook = std::move(hook);
}



} // namespace fsim::runtime::simir
