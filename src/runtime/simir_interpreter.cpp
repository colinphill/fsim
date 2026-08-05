// SPDX-License-Identifier: Apache-2.0
#include "simir_internal.hpp"
#include "fsim/runtime/systemverilog_string.hpp"

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

void Interpreter::set_class_allocate_hook(ClassAllocateHook hook) {
  impl_->class_allocate_hook = std::move(hook);
}

void Interpreter::set_class_property_read_hook(ClassPropertyReadHook hook) {
  impl_->class_property_read_hook = std::move(hook);
}

void Interpreter::set_class_property_write_hook(ClassPropertyWriteHook hook) {
  impl_->class_property_write_hook = std::move(hook);
}

void Interpreter::set_class_method_call_hook(ClassMethodCallHook hook) {
  impl_->class_method_call_hook = std::move(hook);
}

void Interpreter::set_class_static_property_read_hook(
    ClassStaticPropertyReadHook hook) {
  impl_->class_static_property_read_hook = std::move(hook);
}

void Interpreter::set_class_static_property_write_hook(
    ClassStaticPropertyWriteHook hook) {
  impl_->class_static_property_write_hook = std::move(hook);
}

void Interpreter::set_class_static_method_call_hook(
    ClassStaticMethodCallHook hook) {
  impl_->class_static_method_call_hook = std::move(hook);
}

SignalId Interpreter::add_signal(Signal signal) {
  if (impl_->started) {
    throw std::logic_error("cannot add a SimIR signal after start");
  }
  const auto id = static_cast<SignalId>(impl_->signals.size());
  if (static_cast<std::size_t>(id) != impl_->signals.size()) {
    throw std::length_error("too many SimIR signals");
  }
  const auto valid_strength = [](const StrengthRank rank) {
    return static_cast<std::underlying_type_t<StrengthRank>>(rank)
        <= static_cast<std::underlying_type_t<StrengthRank>>(
            StrengthRank::supply);
  };
  if (!valid_strength(signal.implicit_drive_strength.zero)
      || !valid_strength(signal.implicit_drive_strength.one)
      || (signal.charge_strength
          && !valid_strength(*signal.charge_strength))) {
    throw std::invalid_argument{"invalid SimIR signal strength metadata"};
  }
  if (signal.systemverilog_scalar != SystemVerilogScalarKind::None) {
    const auto scalar = decode_systemverilog_scalar_payload(
        signal.initial_value, signal.systemverilog_scalar);
    const auto classification = scalar
        ? classify_systemverilog_scalar(scalar.value)
        : SystemVerilogScalarClassification{
              .error = SystemVerilogScalarError::InvalidKind};
    const bool valid_value = scalar
        && (signal.systemverilog_scalar == SystemVerilogScalarKind::Chandle
            || (classification && classification.finite));
    if (!valid_value
        || signal.resolution != ResolutionKind::none
        || signal.implicit_driver || signal.charge_strength
        || signal.charge_decay) {
      throw std::invalid_argument{
          "invalid SimIR SystemVerilog scalar signal metadata"};
    }
  }
  impl_->driven_values.push_back(signal.initial_value);
  impl_->driver_values.emplace_back();
  impl_->driver_strengths.emplace_back();
  impl_->external_driver_values.emplace_back();
  impl_->charge_decay_handles.emplace_back();
  impl_->charge_values.push_back(
      signal.charge_strength
          ? std::optional<PackedLogic4>{signal.initial_value}
          : std::nullopt);
  impl_->signal_last_values.push_back(signal.initial_value);
  impl_->forced_values.emplace_back();
  impl_->forced_masks.emplace_back(
      signal.initial_value.width(), Logic4::zero);
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
  (void)systemverilog_string_length(object.initial_value);
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
  const auto valid_strength = [](const StrengthRank rank) {
    return static_cast<std::underlying_type_t<StrengthRank>>(rank)
        <= static_cast<std::underlying_type_t<StrengthRank>>(
            StrengthRank::supply);
  };
  if (!valid_strength(process.drive_strength.zero)
      || !valid_strength(process.drive_strength.one)) {
    throw std::invalid_argument{"invalid SimIR process drive strength"};
  }
  const bool has_switch_metadata = process.switch_source.has_value()
      || process.switch_target.has_value()
      || process.switch_control.has_value();
  const bool switch_connection = process.switch_bidirectional;
  if ((switch_connection
       && (!process.switch_source || !process.switch_target))
      || (has_switch_metadata
      && (!process.switch_source || !process.switch_target
          || *process.switch_source >= impl_->signals.size()
          || *process.switch_target >= impl_->signals.size()
          || (process.switch_control
              && *process.switch_control >= impl_->signals.size())))) {
    throw std::invalid_argument{
        "SimIR transmission connection has invalid endpoint metadata"};
  }
  if (has_switch_metadata) {
    const auto source_width = impl_->signals[*process.switch_source]
                                  .initial_value.width();
    const auto target_width = impl_->signals[*process.switch_target]
                                  .initial_value.width();
    if ((source_width != target_width
         && source_width != 1 && target_width != 1)
        || (process.switch_control
            && impl_->signals[*process.switch_control]
                       .initial_value.width() != 1
            && impl_->signals[*process.switch_control]
                       .initial_value.width()
                != std::max(source_width, target_width))) {
      throw std::invalid_argument{
          "SimIR transmission connection has incompatible endpoint widths"};
    }
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
    if (signal.edge != EdgeKind::any
        && signal.edge != EdgeKind::transaction &&
        impl_->signals[signal.signal].initial_value.width() != 1) {
      throw std::invalid_argument(
          "edge sensitivity currently requires a scalar signal");
    }
    impl_->static_fanout[signal.signal].push_back({id, signal.edge});
  }
  std::set<std::string> local_names;
  for (const auto& local : process.debug_locals) {
    if (local.name.empty()
        || local.register_id >= process.register_count) {
      throw std::invalid_argument{"invalid SimIR debug-local metadata"};
    }
    if (local.systemverilog_scalar != SystemVerilogScalarKind::None) {
      const auto expected_width = local.systemverilog_scalar
                  == SystemVerilogScalarKind::ShortReal
          ? 32U
          : local.systemverilog_scalar == SystemVerilogScalarKind::Real
                  || local.systemverilog_scalar
                      == SystemVerilogScalarKind::Realtime
                  || local.systemverilog_scalar
                      == SystemVerilogScalarKind::Time
                  || local.systemverilog_scalar
                      == SystemVerilogScalarKind::Chandle
              ? 64U : 0U;
      if (local.width != expected_width
          || local.value_kind != ValueKind::logic4) {
        throw std::invalid_argument{
            "invalid SimIR scalar debug-local metadata"};
      }
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
  std::map<SignalId, std::vector<Process::DriverRegion>> outputs;
  if (process.driver_regions.empty()) {
    for (const auto& operation : process.operations) {
      const auto signal = output_signal(operation);
      if (signal) {
        outputs[*signal].push_back(
            Process::DriverRegion{*signal, 0, 0, true});
      }
    }
  } else {
    for (const auto& region : process.driver_regions) {
      outputs[region.signal].push_back(region);
    }
  }
  for (const auto& [signal, regions] : outputs) {
    if (signal >= impl_->signals.size()) {
      throw std::invalid_argument(
          "process output references invalid signal");
    }
    if (!switch_connection) {
      impl_->register_driver(
          id, signal, regions, process.drive_strength);
    }
  }

  Impl::ProcessState state;
  state.frame = std::make_shared<Impl::ProcessFrame>();
  state.frame->registers.assign(
      process.register_count, PackedLogic4{});
  state.frame->string_registers.assign(
      process.string_register_count, {});
  state.frame->container_registers.reserve(
      process.container_register_count);
  for (const auto& type : process.container_register_types) {
    state.frame->container_registers.push_back(
        default_container_value(type));
  }
  state.random_state = Impl::initial_random_state(
      impl_->root_seed, id);
  state.design_process = id;
  state.waiting_on_static = !process.initialize;
  state.program = std::move(process);
  impl_->processes.push_back(std::move(state));
  return id;
}

std::uint32_t Interpreter::add_module_path(ModulePath path) {
  if (impl_->started) {
    throw std::logic_error("cannot add a SimIR module path after start");
  }
  const auto id = static_cast<std::uint32_t>(impl_->module_paths.size());
  if (static_cast<std::size_t>(id) != impl_->module_paths.size()) {
    throw std::length_error("too many SimIR module paths");
  }
  if (path.id != id) {
    throw std::invalid_argument(
        "SimIR module-path IDs must be dense and ordered");
  }
  const auto valid_terminal = [&](const ModulePathTerminal& terminal) {
    if (terminal.signal >= impl_->signals.size() || terminal.width == 0) {
      return false;
    }
    const auto width =
        impl_->signals[terminal.signal].initial_value.width();
    return terminal.offset <= width
        && terminal.width <= width - terminal.offset;
  };
  if (path.sources.empty() || path.destinations.empty()
      || !std::ranges::all_of(path.sources, valid_terminal)
      || !std::ranges::all_of(path.destinations, valid_terminal)) {
    throw std::invalid_argument("invalid SimIR module-path terminals");
  }
  const bool valid_delay_count = path.delays.size() == 1
      || path.delays.size() == 2 || path.delays.size() == 3
      || path.delays.size() == 6 || path.delays.size() == 12;
  if (!valid_delay_count) {
    throw std::invalid_argument("invalid SimIR module-path delay count");
  }
  if (!path.full
      && (path.sources.size() != path.destinations.size()
          || !std::ranges::equal(
              path.sources, path.destinations,
              [](const auto& source, const auto& destination) {
                return source.width == destination.width;
              }))) {
    throw std::invalid_argument(
        "parallel SimIR module-path terminal widths do not match");
  }
  if (!std::ranges::all_of(
          path.drivers,
          [&](const ProcessId driver) {
            return driver < impl_->processes.size();
          })
      || !std::ranges::is_sorted(path.drivers)
      || std::ranges::adjacent_find(path.drivers)
          != path.drivers.end()) {
    throw std::invalid_argument("invalid SimIR module-path driver set");
  }
  if (path.source_edge > ModulePathEdge::edge
      || path.polarity > ModulePathPolarity::negative
      || path.pulse_style > ModulePathPulseStyle::ondetect
      || path.selection_group > path.id || (path.conditional && path.ifnone)
      || path.conditional == path.condition.empty()
      || path.pulse_reject_limit.has_value()
          != path.pulse_error_limit.has_value()
      || (path.pulse_reject_limit
          && *path.pulse_reject_limit > *path.pulse_error_limit)) {
    throw std::invalid_argument("invalid SimIR module-path enumeration");
  }
  validate_module_path_expression(path.condition, impl_->signals);
  validate_module_path_expression(path.data_source, impl_->signals);
  impl_->module_paths.push_back(std::move(path));
  return id;
}

std::uint32_t Interpreter::add_module_timing_check(
    ModuleTimingCheck check) {
  if (impl_->started) {
    throw std::logic_error{
        "cannot add a SimIR module timing check after start"};
  }
  const auto id = static_cast<std::uint32_t>(
      impl_->module_timing_checks.size());
  if (static_cast<std::size_t>(id)
      != impl_->module_timing_checks.size()) {
    throw std::length_error{"too many SimIR module timing checks"};
  }
  const auto valid_event = [&](const ModuleTimingEvent& event) {
    return event.terminal.signal < impl_->signals.size()
        && event.terminal.width == 1
        && event.terminal.offset
            < impl_->signals[event.terminal.signal].initial_value.width()
        && event.edge <= ModulePathEdge::edge;
  };
  const auto valid_delayed_terminal = [&](const ModulePathTerminal& terminal) {
    return terminal.signal < impl_->signals.size()
        && terminal.width == 1
        && terminal.offset
            < impl_->signals[terminal.signal].initial_value.width();
  };
  const bool compound = check.kind == ModuleTimingCheckKind::setuphold
      || check.kind == ModuleTimingCheckKind::recrem
      || check.kind == ModuleTimingCheckKind::fullskew
      || check.kind == ModuleTimingCheckKind::nochange;
  const auto expected_limits = compound ? 2U : 1U;
  bool valid_compound_sum = !compound;
  if (check.limits.size() == 2) {
    const bool overflow =
        (check.limits[1] > 0
         && check.limits[0]
             > std::numeric_limits<std::int64_t>::max()
                 - check.limits[1])
        || (check.limits[1] < 0
            && check.limits[0]
                < std::numeric_limits<std::int64_t>::min()
                    - check.limits[1]);
    valid_compound_sum = !overflow
        && check.limits[0] + check.limits[1] > 0;
  }
  const bool controlled_reference =
      check.reference.edge != ModulePathEdge::none;
  if (check.id != id || check.kind > ModuleTimingCheckKind::nochange
      || !valid_event(check.reference)
      || ((check.kind == ModuleTimingCheckKind::period
           || check.kind == ModuleTimingCheckKind::width)
          && !controlled_reference)
      || (check.kind != ModuleTimingCheckKind::period
          && check.kind != ModuleTimingCheckKind::width
          && (!check.data || !valid_event(*check.data)))
      || check.limits.size() != expected_limits
      || ((check.kind != ModuleTimingCheckKind::setuphold
           && check.kind != ModuleTimingCheckKind::recrem
           && check.kind != ModuleTimingCheckKind::nochange)
          && std::ranges::any_of(
              check.limits,
              [](const std::int64_t limit) { return limit < 0; }))
      || ((check.kind == ModuleTimingCheckKind::setuphold
           || check.kind == ModuleTimingCheckKind::recrem)
          && !valid_compound_sum)
      || (check.kind == ModuleTimingCheckKind::nochange
          && check.limits.size() == 2
          && check.limits[0] > check.limits[1])
      || (check.threshold.has_value()
          && check.kind != ModuleTimingCheckKind::width)
      || (check.notifier
          && (*check.notifier >= impl_->signals.size()
              || impl_->signals[*check.notifier].initial_value.width() != 1))
      || (check.delayed_reference
          && !valid_delayed_terminal(*check.delayed_reference))
      || (check.delayed_data
          && !valid_delayed_terminal(*check.delayed_data))) {
    throw std::invalid_argument{"invalid SimIR module timing check"};
  }
  validate_module_path_expression(check.reference.condition, impl_->signals);
  if (check.data) {
    validate_module_path_expression(check.data->condition, impl_->signals);
  }
  validate_module_path_expression(check.timestamp_condition, impl_->signals);
  validate_module_path_expression(check.timecheck_condition, impl_->signals);
  impl_->module_timing_checks.push_back(std::move(check));
  impl_->module_timing_check_states.emplace_back();
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

void Interpreter::deposit_scalar_signal(
    const SignalId signal,
    const SystemVerilogScalarValue value) {
  const auto& stored = impl_->get_signal(signal);
  if (stored.systemverilog_scalar != value.kind) {
    throw std::invalid_argument{"SimIR scalar signal kind mismatch"};
  }
  const auto encoded = encode_systemverilog_scalar_payload(value);
  if (!encoded) {
    throw std::invalid_argument{"invalid SimIR scalar signal value"};
  }
  deposit_signal(signal, encoded.value);
}

void Interpreter::force_signal(SignalId signal, PackedLogic4 value) {
  const auto width = impl_->get_signal(signal).initial_value.width();
  if (width != value.width()) {
    throw std::invalid_argument("SimIR signal force width mismatch");
  }
  impl_->force_slice(signal, std::move(value), 0);
}

void Interpreter::force_scalar_signal(
    const SignalId signal,
    const SystemVerilogScalarValue value) {
  const auto& stored = impl_->get_signal(signal);
  if (stored.systemverilog_scalar != value.kind) {
    throw std::invalid_argument{"SimIR scalar signal kind mismatch"};
  }
  const auto encoded = encode_systemverilog_scalar_payload(value);
  if (!encoded) {
    throw std::invalid_argument{"invalid SimIR scalar signal value"};
  }
  force_signal(signal, encoded.value);
}

void Interpreter::release_signal(SignalId signal) {
  const auto width = impl_->get_signal(signal).initial_value.width();
  impl_->release_slice(signal, 0, width);
}

void Interpreter::force_signal_slice(
    const SignalId signal,
    PackedLogic4 value,
    const std::size_t offset) {
  impl_->force_slice(signal, std::move(value), offset);
}

void Interpreter::release_signal_slice(
    const SignalId signal,
    const std::size_t offset,
    const std::size_t width) {
  impl_->release_slice(signal, offset, width);
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

void Interpreter::schedule_scalar_signal_at(
    const SignalId signal,
    const SystemVerilogScalarValue value,
    const SimulationTick time,
    const StableOrder order) {
  const auto& stored = impl_->get_signal(signal);
  if (stored.systemverilog_scalar != value.kind) {
    throw std::invalid_argument{"SimIR scalar signal kind mismatch"};
  }
  const auto encoded = encode_systemverilog_scalar_payload(value);
  if (!encoded) {
    throw std::invalid_argument{"invalid SimIR scalar signal value"};
  }
  schedule_signal_at(signal, encoded.value, time, order);
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

void Interpreter::schedule_scalar_signal_after(
    const SignalId signal,
    const SystemVerilogScalarValue value,
    const SimulationTick delay,
    const StableOrder order) {
  if (delay
      > std::numeric_limits<SimulationTick>::max()
          - impl_->scheduler.now()) {
    throw std::overflow_error{"simulation time overflow scheduling signal"};
  }
  schedule_scalar_signal_at(
      signal, value, impl_->scheduler.now() + delay, order);
}

const PackedLogic4 &Interpreter::signal_value(SignalId signal) const {
  return impl_->get_signal(signal).initial_value;
}

SystemVerilogScalarValue Interpreter::scalar_signal_value(
    const SignalId signal) const {
  const auto& stored = impl_->get_signal(signal);
  const auto decoded = decode_systemverilog_scalar_payload(
      stored.initial_value, stored.systemverilog_scalar);
  if (!decoded) {
    throw std::logic_error{"SimIR signal is not a valid scalar value"};
  }
  return decoded.value;
}

std::vector<SystemVerilogScalarSignalSnapshot>
Interpreter::scalar_signal_snapshots() const {
  std::vector<SystemVerilogScalarSignalSnapshot> result;
  for (std::size_t index = 0; index < impl_->signals.size(); ++index) {
    const auto signal = static_cast<SignalId>(index);
    const auto& stored = impl_->signals[index];
    if (stored.systemverilog_scalar == SystemVerilogScalarKind::None) {
      continue;
    }
    result.push_back({signal, stored.name, scalar_signal_value(signal)});
  }
  return result;
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
  (void)systemverilog_string_length(value);
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
  const auto& value = state.frame->registers.at(local.register_id);
  if (value.width() != local.width) {
    throw std::logic_error{"SimIR debug local has not been initialized"};
  }
  return value;
}

SystemVerilogScalarValue Interpreter::read_debug_scalar_local(
    const ProcessId process,
    const std::size_t local_index) const {
  auto& state = impl_->get_process(process);
  if (local_index >= state.program.debug_locals.size()) {
    throw std::out_of_range{"invalid SimIR debug-local index"};
  }
  const auto& local = state.program.debug_locals[local_index];
  if (local.systemverilog_scalar == SystemVerilogScalarKind::None) {
    throw std::logic_error{"SimIR debug local is not a scalar value"};
  }
  const auto packed = read_debug_local(process, local_index);
  const auto decoded = decode_systemverilog_scalar_payload(
      packed, local.systemverilog_scalar);
  if (!decoded) {
    throw std::logic_error{"SimIR scalar debug local is not initialized"};
  }
  return decoded.value;
}

void Interpreter::write_debug_scalar_local(
    const ProcessId process,
    const std::size_t local_index,
    const SystemVerilogScalarValue value) {
  auto& state = impl_->get_process(process);
  if (local_index >= state.program.debug_locals.size()) {
    throw std::out_of_range{"invalid SimIR debug-local index"};
  }
  const auto& local = state.program.debug_locals[local_index];
  if (value.kind != local.systemverilog_scalar) {
    throw std::invalid_argument{"SimIR scalar debug-local kind mismatch"};
  }
  const auto encoded = encode_systemverilog_scalar_payload(value);
  if (!encoded || encoded.value.width() != local.width) {
    throw std::invalid_argument{"invalid SimIR scalar debug-local payload"};
  }
  impl_->write_process_register(state, local.register_id, encoded.value);
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
  return state.frame->string_registers.at(local.register_id);
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
  return state.frame->container_registers.at(local.register_id);
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

void Interpreter::set_scalar_signal_change_hook(
    ScalarSignalChangeHook hook) {
  impl_->scalar_signal_change_hook = std::move(hook);
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
