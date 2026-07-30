// SPDX-License-Identifier: Apache-2.0
#include "simir_internal.hpp"

namespace fsim::runtime::simir {

[[nodiscard]] std::size_t Interpreter::Impl::InertialDriverKeyHash::operator()(
    const InertialDriverKey& key) const noexcept  {
      auto result = static_cast<std::size_t>(key.process);
      result ^= static_cast<std::size_t>(key.signal)
          + UINT64_C(0x9e3779b97f4a7c15)
          + (result << 6U) + (result >> 2U);
      result ^= static_cast<std::size_t>(key.offset)
          + UINT64_C(0x9e3779b97f4a7c15)
          + (result << 6U) + (result >> 2U);
      result ^= static_cast<std::size_t>(key.width)
          + UINT64_C(0x9e3779b97f4a7c15)
          + (result << 6U) + (result >> 2U);
      return result;
    }

[[nodiscard]] std::size_t Interpreter::Impl::ProjectedDriverKeyHash::operator()(
    const ProjectedDriverKey& key) const noexcept  {
      auto result = static_cast<std::size_t>(key.process);
      result ^= static_cast<std::size_t>(key.signal)
          + UINT64_C(0x9e3779b97f4a7c15)
          + (result << 6U) + (result >> 2U);
      result ^= static_cast<std::size_t>(key.offset)
          + UINT64_C(0x9e3779b97f4a7c15)
          + (result << 6U) + (result >> 2U);
      return result;
    }

Interpreter::Impl::Impl(
    SchedulerOptions options,
    const std::uint64_t seed)
    : scheduler(options), root_seed(seed)  {}

[[nodiscard]] Signal &Interpreter::Impl::get_signal(SignalId id)  {
    if (id >= signals.size()) {
      throw std::out_of_range("invalid SimIR signal ID");
    }
    return signals[id];
  }

[[nodiscard]] const Signal &Interpreter::Impl::get_signal(SignalId id) const  {
    if (id >= signals.size()) {
      throw std::out_of_range("invalid SimIR signal ID");
    }
    return signals[id];
  }

[[nodiscard]] Interpreter::Impl::ProcessState&
Interpreter::Impl::get_process(ProcessId id) {
    if (id >= processes.size()) {
      throw std::out_of_range("invalid SimIR process ID");
    }
    return processes[id];
  }

[[nodiscard]] PackedLogic4 &Interpreter::Impl::get_register(ProcessState &process,
                                         RegisterId id)  {
    if (id >= process.registers.size()) {
      throw InterpreterError(process.program.id, process.pc,
                             "invalid register ID");
    }
    return process.registers[id];
  }

[[nodiscard]] std::string& Interpreter::Impl::get_string_register(
    ProcessState& process,
    const StringRegisterId id) {
  if (id >= process.string_registers.size()) {
    throw InterpreterError(
        process.program.id, process.pc, "invalid string register ID");
  }
  return process.string_registers[id];
}

[[nodiscard]] StringObject& Interpreter::Impl::get_string_object(
    const StringObjectId id) {
  if (id >= string_objects.size()) {
    throw std::out_of_range{"invalid SimIR string object ID"};
  }
  return string_objects[id];
}

[[nodiscard]] const StringObject& Interpreter::Impl::get_string_object(
    const StringObjectId id) const {
  if (id >= string_objects.size()) {
    throw std::out_of_range{"invalid SimIR string object ID"};
  }
  return string_objects[id];
}

[[nodiscard]] ContainerValue&
Interpreter::Impl::get_container_register(
    ProcessState& process,
    const ContainerRegisterId id) {
  if (id >= process.container_registers.size()) {
    throw InterpreterError(
        process.program.id, process.pc,
        "invalid container register ID");
  }
  return process.container_registers[id];
}

[[nodiscard]] ContainerObject& Interpreter::Impl::get_container_object(
    const ContainerObjectId id) {
  if (id >= container_objects.size()) {
    throw std::out_of_range{"invalid SimIR container object ID"};
  }
  return container_objects[id];
}

[[nodiscard]] const ContainerObject&
Interpreter::Impl::get_container_object(
    const ContainerObjectId id) const {
  if (id >= container_objects.size()) {
    throw std::out_of_range{"invalid SimIR container object ID"};
  }
  return container_objects[id];
}

[[nodiscard]] ValueKind Interpreter::Impl::register_value_kind(
    const ProcessState& process,
    const RegisterId id)  {
    if (process.program.register_value_kinds.empty()) {
      return ValueKind::logic4;
    }
    return process.program.register_value_kinds.at(id);
  }

[[nodiscard]] PackedLogic4 Interpreter::Impl::coerce_value_kind(
    PackedLogic4 value,
    const ValueKind kind)  {
    if (kind == ValueKind::logic9) {
      return value.is_logic9()
          ? value
          : value.promoted_to_logic9();
    }
    return value.is_logic9()
        ? collapse_to_logic4(value)
        : value;
  }

[[nodiscard]] PackedLogic4 Interpreter::Impl::normalize_signal_value(
    const SignalId signal,
    PackedLogic4 value) const  {
    return coerce_value_kind(
        std::move(value),
        get_signal(signal).value_kind);
  }

void Interpreter::Impl::remove_dynamic_wait(ProcessState &process)  {
    if (!process.waiting_on_signal) {
      return;
    }
    for (const auto sensitivity : process.dynamic_sensitivity) {
      auto &fanout = dynamic_fanout[sensitivity.signal];
      fanout.erase(
          std::remove_if(
              fanout.begin(), fanout.end(),
              [&](const Fanout& entry) {
                return entry.process == process.program.id;
              }),
          fanout.end());
    }
    process.dynamic_sensitivity.clear();
    process.dynamic_triggered.clear();
    process.waiting_on_signal = false;
    process.dynamic_wait_all = false;
  }

void Interpreter::Impl::write_process_register(
    Interpreter::Impl::ProcessState& process,
    const RegisterId destination,
    const PackedLogic4& value)  {
    const auto converted = coerce_value_kind(
        value, register_value_kind(process, destination));
    if (process.executor) {
      process.executor->write_register(
          destination, converted);
      return;
    }
    get_register(process, destination) = converted;
  }

void Interpreter::Impl::clear_wait_timeout(ProcessState& process)  {
    if (!process.wait_timeout_origin) {
      return;
    }
    if (process.wait_timeout_generation
        == std::numeric_limits<std::uint64_t>::max()) {
      fail(process, "wait timeout generation overflow");
    }
    ++process.wait_timeout_generation;
    process.wait_timeout_origin.reset();
    process.wait_timeout_deadline.reset();
    process.wait_timeout_result.reset();
  }

void Interpreter::Impl::set_wait_timeout_result(
    Interpreter::Impl::ProcessState& process,
    const bool timed_out)  {
    if (!process.wait_timeout_result) {
      return;
    }
    write_process_register(
        process,
        *process.wait_timeout_result,
        PackedLogic4::from_msb_string(
            timed_out ? "1" : "0"));
  }

void Interpreter::Impl::begin_wait_timeout(
    Interpreter::Impl::ProcessState& process,
    const InstructionIndex origin,
    const SimulationTick delay,
    const std::optional<RegisterId> result)  {
    clear_wait_timeout(process);
    if (delay
        > std::numeric_limits<SimulationTick>::max()
            - scheduler.now()) {
      process.pc = origin;
      fail(process, "simulation time overflow in WaitOn timeout");
    }
    if (process.wait_timeout_generation
        == std::numeric_limits<std::uint64_t>::max()) {
      process.pc = origin;
      fail(process, "wait timeout generation overflow");
    }
    const auto generation =
        ++process.wait_timeout_generation;
    const auto deadline = scheduler.now() + delay;
    process.wait_timeout_origin = origin;
    process.wait_timeout_deadline = deadline;
    process.wait_timeout_result = result;
    set_wait_timeout_result(process, false);

    auto callback =
        [this, id = process.program.id, origin, generation](
            Scheduler&) {
          auto& state = get_process(id);
          if (state.wait_timeout_generation != generation
              || state.wait_timeout_origin
                  != std::optional{origin}) {
            return;
          }
          set_wait_timeout_result(state, true);
          state.wait_timeout_origin.reset();
          state.wait_timeout_deadline.reset();
          state.wait_timeout_result.reset();
          queue_active_current(id);
        };
    if (delay == 0) {
      scheduler.schedule(
          SchedulerPhase::inactive,
          process.program.id,
          std::move(callback));
    } else {
      scheduler.schedule_at(
          deadline,
          SchedulerPhase::active,
          process.program.id,
          std::move(callback));
    }
  }

void Interpreter::Impl::rearm_wait_timeout(
    Interpreter::Impl::ProcessState& process,
    const InstructionIndex instruction,
    const InstructionIndex origin,
    const std::optional<RegisterId> result)  {
    if (process.wait_timeout_origin
            != std::optional{origin}
        || !process.wait_timeout_deadline) {
      process.pc = instruction;
      fail(
          process,
          "WaitOn timeout rearm has no matching active deadline");
    }
    if (process.wait_timeout_result != result) {
      process.pc = instruction;
      fail(
          process,
          "WaitOn timeout rearm result register mismatch");
    }
    if (*process.wait_timeout_deadline < scheduler.now()) {
      process.pc = instruction;
      fail(process, "WaitOn timeout deadline was missed");
    }
  }

void Interpreter::Impl::mark_dynamic_event_resume(
    Interpreter::Impl::ProcessState& process)  {
    const auto timed_out =
        process.wait_timeout_deadline
        && *process.wait_timeout_deadline <= scheduler.now();
    set_wait_timeout_result(process, timed_out);
  }

[[nodiscard]] bool Interpreter::Impl::dynamic_wait_satisfied(
    Interpreter::Impl::ProcessState& process,
    const SignalId signal,
    const EdgeKind edge)  {
    if (!process.dynamic_wait_all) {
      return true;
    }
    for (std::size_t index = 0;
         index < process.dynamic_sensitivity.size(); ++index) {
      const auto& sensitivity =
          process.dynamic_sensitivity[index];
      if (sensitivity.signal == signal
          && sensitivity.edge == edge) {
        process.dynamic_triggered[index] = true;
      }
    }
    return std::all_of(
        process.dynamic_triggered.begin(),
        process.dynamic_triggered.end(),
        [](const bool triggered) { return triggered; });
  }

void Interpreter::Impl::queue_at(ProcessId id, SimulationTick time)  {
    auto &process = get_process(id);
    if (process.halted || process.queued) {
      return;
    }
    process.queued = true;
    scheduler.schedule_at(
        time, SchedulerPhase::active, id,
        [this, id](Scheduler &) {
          auto &state = get_process(id);
          state.queued = false;
          state.waiting_on_static = false;
          remove_dynamic_wait(state);
          execute(id);
        });
  }

void Interpreter::Impl::queue_next_delta(ProcessId id)  {
    auto &process = get_process(id);
    if (process.halted || process.queued) {
      return;
    }
    process.queued = true;
    scheduler.schedule_next_delta(
        SchedulerPhase::active, id,
        [this, id](Scheduler &) {
          auto &state = get_process(id);
          state.queued = false;
          state.waiting_on_static = false;
          remove_dynamic_wait(state);
          execute(id);
        });
  }

void Interpreter::Impl::queue_current(ProcessId id)  {
    auto& process = get_process(id);
    if (process.halted || process.queued) {
      return;
    }
    process.queued = true;
    const auto phase =
        scheduler.current_phase().value_or(SchedulerPhase::active);
    scheduler.schedule(
        phase, id,
        [this, id](Scheduler&) {
          auto& state = get_process(id);
          state.queued = false;
          execute(id);
        });
  }

void Interpreter::Impl::queue_active_current(ProcessId id)  {
    auto& process = get_process(id);
    if (process.halted || process.queued) {
      return;
    }
    process.queued = true;
    scheduler.schedule(
        SchedulerPhase::active, id,
        [this, id](Scheduler&) {
          auto& state = get_process(id);
          state.queued = false;
          state.waiting_on_static = false;
          remove_dynamic_wait(state);
          execute(id);
        });
  }

void Interpreter::Impl::trigger_event(const SignalId event)  {
    (void)get_signal(event);
    for (const auto& sensitivity : static_fanout[event]) {
      auto& process = get_process(sensitivity.process);
      if (process.waiting_on_static) {
        queue_active_current(sensitivity.process);
      }
    }
    // Copy because queue_active_current removes dynamic registrations.
    const auto dynamic = dynamic_fanout[event];
    for (const auto& sensitivity : dynamic) {
      auto& process = get_process(sensitivity.process);
      if (dynamic_wait_satisfied(
              process, event, sensitivity.edge)) {
        mark_dynamic_event_resume(process);
        queue_active_current(sensitivity.process);
      }
    }
  }

[[nodiscard]] std::uint64_t Interpreter::Impl::invalidate_event(
    const SignalId event)  {
    (void)get_signal(event);
    auto& state = event_states[event];
    if (state.generation
        == std::numeric_limits<std::uint64_t>::max()) {
      throw std::overflow_error{
          "event notification generation overflow"};
    }
    state.kind = PendingEventKind::none;
    state.due = 0;
    return ++state.generation;
  }

void Interpreter::Impl::cancel_event(const SignalId event)  {
    (void)invalidate_event(event);
  }

void Interpreter::Impl::notify_event(
    const SignalId event,
    const SimulationTick delay,
    const EventNotificationKind kind,
    const StableOrder order)  {
    (void)get_signal(event);
    auto& state = event_states[event];
    auto effective_kind = kind;

    if (kind == EventNotificationKind::delayed) {
      if (state.kind != PendingEventKind::none) {
        throw std::logic_error{
            "notify_delayed requires an event with no pending notification"};
      }
      effective_kind =
          delay == 0
              ? EventNotificationKind::delta
              : EventNotificationKind::timed;
    }

    if (effective_kind == EventNotificationKind::immediate) {
      if (delay != 0) {
        throw std::invalid_argument{
            "immediate event notification cannot have a delay"};
      }
      (void)invalidate_event(event);
      trigger_event(event);
      return;
    }

    if (effective_kind == EventNotificationKind::delta) {
      if (delay != 0) {
        throw std::invalid_argument{
            "delta event notification cannot have a delay"};
      }
      if (state.kind == PendingEventKind::delta) {
        return;
      }
      const auto generation = invalidate_event(event);
      state.kind = PendingEventKind::delta;
      state.due = scheduler.now();
      scheduler.schedule_next_delta(
          SchedulerPhase::active,
          order,
          [this, event, generation](Scheduler&) {
            auto& pending = event_states[event];
            if (pending.generation != generation
                || pending.kind != PendingEventKind::delta) {
              return;
            }
            pending.kind = PendingEventKind::none;
            pending.due = 0;
            trigger_event(event);
          });
      return;
    }

    if (effective_kind != EventNotificationKind::timed || delay == 0) {
      throw std::invalid_argument{
          "timed event notification requires a non-zero delay"};
    }
    if (delay
        > std::numeric_limits<SimulationTick>::max()
            - scheduler.now()) {
      throw std::overflow_error{
          "simulation time overflow while scheduling event notification"};
    }
    const auto due = scheduler.now() + delay;
    if (state.kind == PendingEventKind::delta
        || (state.kind == PendingEventKind::timed
            && state.due <= due)) {
      return;
    }
    const auto generation = invalidate_event(event);
    state.kind = PendingEventKind::timed;
    state.due = due;
    scheduler.schedule_at(
        due,
        SchedulerPhase::active,
        order,
        [this, event, generation, due](Scheduler&) {
          auto& pending = event_states[event];
          if (pending.generation != generation
              || pending.kind != PendingEventKind::timed
              || pending.due != due) {
            return;
          }
          pending.kind = PendingEventKind::none;
          pending.due = 0;
          trigger_event(event);
        });
  }

void Interpreter::Impl::notify_execution_point(
    Interpreter::Impl::ProcessState& process,
    const InstructionIndex instruction,
    const ExecutionPointKind kind,
    const SourceLocation& source)  {
    if (execution_point_hook) {
      execution_point_hook(
          scheduler,
          ExecutionPoint{
              process.program.id, instruction, kind, source});
    }
  }

[[nodiscard]] bool Interpreter::Impl::monitor_watches(
    const SignalId signal) const  {
    return monitor
        && std::ranges::any_of(
            monitor->values,
            [signal](const MonitorValue& value) {
              return value.kind == MonitorValueKind::signal
                  && value.signal == signal;
            });
  }

[[nodiscard]] std::string Interpreter::Impl::render_monitor() const  {
    if (!monitor) {
      return {};
    }
    std::string text;
    for (const auto& value : monitor->values) {
      if (value.kind == MonitorValueKind::time) {
        text += make_time_output(
            value.prefix,
            {},
            scheduler.now(),
            value.minimum_width,
            value.left_justify,
            value.zero_pad);
      } else {
        text += make_formatted_output(
            value.prefix,
            {},
            value.format,
            get_signal(value.signal).initial_value,
            value.signed_decimal,
            value.suppress_leading_zero,
            value.minimum_width,
            value.left_justify,
            value.zero_pad);
      }
    }
    text += monitor->trailing_text;
    return text;
  }

void Interpreter::Impl::schedule_monitor_publication()  {
    if (!monitor || !monitor_enabled) {
      return;
    }
    const auto publication =
        std::pair{scheduler.now(), scheduler.delta()};
    if (monitor_publication == publication) {
      return;
    }
    monitor_publication = publication;
    const auto generation = monitor_generation;
    const auto process = monitor_process;
    scheduler.schedule(
        SchedulerPhase::postponed,
        process,
        [this, generation, process](Scheduler& runtime) {
          if (generation != monitor_generation
              || !monitor_enabled || !monitor) {
            return;
          }
          monitor_publication.reset();
          if (output_hook) {
            output_hook(
                process,
                render_monitor(),
                monitor->newline,
                runtime.now(),
                runtime.delta());
          }
        });
  }

void Interpreter::Impl::install_monitor(
    const ProcessId process,
    const MonitorInstall& registration)  {
    if (monitor_generation
        == std::numeric_limits<std::uint64_t>::max()) {
      throw std::overflow_error{"monitor generation overflow"};
    }
    ++monitor_generation;
    monitor = registration;
    monitor_process = process;
    monitor_enabled = true;
    monitor_publication.reset();
    schedule_monitor_publication();
  }

void Interpreter::Impl::set_monitor_enabled(const bool enabled)  {
    if (monitor_generation
        == std::numeric_limits<std::uint64_t>::max()) {
      throw std::overflow_error{"monitor generation overflow"};
    }
    ++monitor_generation;
    monitor_enabled = enabled;
    monitor_publication.reset();
    if (enabled) {
      schedule_monitor_publication();
    }
  }

[[nodiscard]] std::uint64_t Interpreter::Impl::initial_random_state(
    const std::uint64_t seed,
    const ProcessId process) noexcept  {
    auto value =
        seed
        + UINT64_C(0x9e3779b97f4a7c15)
              * (static_cast<std::uint64_t>(process) + 1U);
    value = (value ^ (value >> 30U))
        * UINT64_C(0xbf58476d1ce4e5b9);
    value = (value ^ (value >> 27U))
        * UINT64_C(0x94d049bb133111eb);
    return value ^ (value >> 31U);
  }

[[nodiscard]] std::uint32_t Interpreter::Impl::next_random(
    Interpreter::Impl::ProcessState& process) noexcept  {
    auto value =
        (process.random_state += UINT64_C(0x9e3779b97f4a7c15));
    value = (value ^ (value >> 30U))
        * UINT64_C(0xbf58476d1ce4e5b9);
    value = (value ^ (value >> 27U))
        * UINT64_C(0x94d049bb133111eb);
    value ^= value >> 31U;
    return static_cast<std::uint32_t>(value >> 32U);
  }

[[nodiscard]] std::optional<std::uint32_t>
Interpreter::Impl::known_random_bound(const PackedLogic4& value)  {
    if (value.empty()) {
      return std::nullopt;
    }
    std::uint32_t result{};
    const auto width = std::min<std::size_t>(32U, value.width());
    for (std::size_t bit = 0; bit < width; ++bit) {
      const auto state = value.get(bit);
      if (state == Logic4::x || state == Logic4::z) {
        return std::nullopt;
      }
      if (state == Logic4::one) {
        result |= UINT32_C(1) << bit;
      }
    }
    return result;
  }

[[nodiscard]] PackedLogic4 Interpreter::Impl::random_value(
    const ProcessId process_id,
    const RandomKind kind,
    const std::optional<PackedLogic4>& maximum,
    const std::optional<PackedLogic4>& minimum)  {
    auto& process = get_process(process_id);
    if (kind != RandomKind::urandom_range) {
      return PackedLogic4::from_aval_bval(
          32, next_random(process), 0);
    }
    if (!maximum) {
      throw std::logic_error{
          "$urandom_range operation has no maximum"};
    }
    const auto known_maximum = known_random_bound(*maximum);
    const auto known_minimum =
        minimum
            ? known_random_bound(*minimum)
            : std::optional<std::uint32_t>{0U};
    if (!known_maximum || !known_minimum) {
      return PackedLogic4(32, Logic4::x);
    }
    auto low = *known_minimum;
    auto high = *known_maximum;
    if (high < low) {
      std::swap(low, high);
    }
    const auto span =
        static_cast<std::uint64_t>(high)
        - static_cast<std::uint64_t>(low) + 1U;
    std::uint32_t sample{};
    if (span == (UINT64_C(1) << 32U)) {
      sample = next_random(process);
    } else {
      const auto full_range = UINT64_C(1) << 32U;
      const auto accepted = full_range - full_range % span;
      do {
        sample = next_random(process);
      } while (static_cast<std::uint64_t>(sample) >= accepted);
      sample = static_cast<std::uint32_t>(
          static_cast<std::uint64_t>(low)
          + static_cast<std::uint64_t>(sample) % span);
    }
    return PackedLogic4::from_aval_bval(32, sample, 0);
  }

void Interpreter::Impl::publish(SignalId signal_id, PackedLogic4 value)  {
    auto &signal = get_signal(signal_id);
    if (signal.initial_value.width() != value.width()) {
      throw std::invalid_argument("SimIR signal assignment width mismatch");
    }
    signal_transactions[signal_id] =
        std::pair{scheduler.now(), scheduler.delta() + 1};
    if (signal.initial_value == value) {
      return;
    }
    const auto old_value = signal.initial_value;
    signal_last_values[signal_id] = old_value;
    signal.initial_value = std::move(value);
    signal_events[signal_id] =
        std::pair{scheduler.now(), scheduler.delta() + 1};
    scheduler.note_signal_change(signal_id);
    if (signal_change_hook) {
      signal_change_hook(signal_id, signal.initial_value, scheduler.now());
    }
    if (monitor_watches(signal_id)) {
      schedule_monitor_publication();
    }

    for (const auto &sensitivity : static_fanout[signal_id]) {
      auto &process = get_process(sensitivity.process);
      if (!process.waiting_on_static) {
        continue;
      }
      if (sensitivity.edge != EdgeKind::any &&
          (old_value.width() != 1 ||
           !edge_matches(sensitivity.edge, old_value.get(0),
                         signal.initial_value.get(0)))) {
        continue;
      }
      queue_next_delta(sensitivity.process);
    }
    // Copy because queue_next_delta removes a process from every dynamic list.
    const auto dynamic = dynamic_fanout[signal_id];
    for (const auto sensitivity : dynamic) {
      if (sensitivity.edge != EdgeKind::any
          && (old_value.width() != 1
              || !edge_matches(
                  sensitivity.edge, old_value.get(0),
                  signal.initial_value.get(0)))) {
        continue;
      }
      auto& process = get_process(sensitivity.process);
      if (dynamic_wait_satisfied(
              process, signal_id, sensitivity.edge)) {
        mark_dynamic_event_resume(process);
        queue_next_delta(sensitivity.process);
      }
    }
  }

} // namespace fsim::runtime::simir
