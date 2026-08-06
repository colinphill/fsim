// SPDX-License-Identifier: Apache-2.0
#include "simir_internal.hpp"
#include "fsim/runtime/scope_randomize.hpp"
#include "simir_signal_attributes.hpp"
#include "fsim/runtime/string_methods.hpp"
#include "fsim/runtime/systemverilog_string.hpp"
#include <cmath>
#include "simir_execution_context.hpp"

namespace {

[[nodiscard]] PackedLogic4 resize_class_value(
    const PackedLogic4& value,
    const std::size_t width) {
  PackedLogic4 result(width, Logic4::zero);
  for (std::size_t bit = 0; bit < std::min(width, value.width()); ++bit) {
    result.set(bit, value.get(bit));
  }
  return value.is_logic9() ? result.promoted_to_logic9() : result;
}

[[nodiscard]] SimulationTick normalized_dynamic_wait_delay(
    const WaitFor& wait,
    const PackedLogic4& payload) {
  if (!wait.source || wait.source_width == 0
      || wait.source_width > 64
      || payload.width() != wait.source_width
      || wait.rounding_quantum == 0) {
    throw std::invalid_argument{
        "runtime WaitFor has invalid dynamic-delay metadata"};
  }
  if (wait.source_kind == SystemVerilogScalarKind::ShortReal
      || wait.source_kind == SystemVerilogScalarKind::Real
      || wait.source_kind == SystemVerilogScalarKind::Realtime) {
    const auto decoded = decode_systemverilog_scalar_payload(
        payload, wait.source_kind);
    const auto number = decoded ? decoded.value.as_real() : std::nullopt;
    if (!number || !std::isfinite(*number)) {
      throw std::invalid_argument{
          "runtime WaitFor requires a finite real delay"};
    }
    if (*number < 0.0) {
      throw std::invalid_argument{
          "runtime WaitFor delay cannot be negative"};
    }
    const auto scaled_quanta =
        static_cast<long double>(*number)
        * static_cast<long double>(wait.delay)
        / static_cast<long double>(wait.rounding_quantum);
    if (!std::isfinite(scaled_quanta)) {
      throw std::overflow_error{
          "runtime WaitFor delay overflows simulation ticks"};
    }
    const auto rounded_quanta = std::round(scaled_quanta);
    const auto maximum_quanta =
        static_cast<long double>(
            std::numeric_limits<SimulationTick>::max())
        / static_cast<long double>(wait.rounding_quantum);
    if (rounded_quanta > maximum_quanta) {
      throw std::overflow_error{
          "runtime WaitFor delay overflows simulation ticks"};
    }
    return static_cast<SimulationTick>(rounded_quanta)
        * wait.rounding_quantum;
  }

  std::uint64_t magnitude = 0;
  if (wait.source_kind == SystemVerilogScalarKind::Time) {
    const auto decoded = decode_systemverilog_scalar_payload(
        payload, wait.source_kind);
    const auto ticks = decoded ? decoded.value.as_time() : std::nullopt;
    if (!ticks) {
      throw std::invalid_argument{
          "runtime WaitFor requires a known time delay"};
    }
    magnitude = *ticks;
  } else if (wait.source_kind == SystemVerilogScalarKind::None) {
    const auto word = payload.low_word();
    if (word.bval != 0) {
      throw std::invalid_argument{
          "runtime WaitFor requires a known integral delay"};
    }
    if (wait.source_signed
        && payload.width() != 0
        && ((word.aval >> (payload.width() - 1U)) & 1U) != 0) {
      throw std::invalid_argument{
          "runtime WaitFor delay cannot be negative"};
    }
    magnitude = word.aval;
  } else {
    throw std::invalid_argument{
        "runtime WaitFor has an invalid delay value kind"};
  }
  if (magnitude != 0
      && wait.delay
          > std::numeric_limits<SimulationTick>::max() / magnitude) {
    throw std::overflow_error{
        "runtime WaitFor delay overflows simulation ticks"};
  }
  return magnitude * wait.delay;
}

}  // namespace

void Interpreter::Impl::request_channel_update(
    const ProcessId process_id,
    const std::uint64_t channel) {
  auto& process = get_process(process_id);
  if (!process.executor) {
    throw std::logic_error{
        "primitive-channel update requires an alternate executor"};
  }
  if (!pending_channel_updates.insert(channel).second) {
    return;
  }
  auto callback =
      [this, process_id, channel](Scheduler&) {
        auto& state = get_process(process_id);
        ExecutionContext context{*this, process_id};
        try {
          state.executor->update_channel(channel, context);
        } catch (...) {
          pending_channel_updates.erase(channel);
          throw;
        }
        pending_channel_updates.erase(channel);
      };
  const auto phase = scheduler.current_phase();
  if (phase && *phase >= SchedulerPhase::update) {
    scheduler.schedule_next_delta(
        SchedulerPhase::update, channel, std::move(callback));
  } else {
    scheduler.schedule(
        SchedulerPhase::update, channel, std::move(callback));
  }
}
void Interpreter::Impl::handle_boundary(
    ProcessState& process,
    const InstructionIndex instruction,
    const InstructionIndex next_instruction) {
  if (instruction >= process.program.operations.size()) {
    process.pc = instruction;
    fail(process, "executor returned an invalid boundary instruction");
  }
  if (instruction == std::numeric_limits<InstructionIndex>::max()
      || next_instruction != instruction + 1) {
    process.pc = instruction;
    fail(
        process,
        "executor returned a non-sequential boundary resume instruction");
  }

  const auto& operation = process.program.operations[instruction];
  process.pc = next_instruction;
  if (const auto* point = fsim::runtime::simir::operation_get_if<DebugPoint>(&operation)) {
    clear_wait_timeout(process);
    process.current_source = point->source;
    process.current_scope = point->scope;
    auto kind = ExecutionPointKind::statement;
    switch (point->kind) {
    case DebugPointKind::statement:
      kind = ExecutionPointKind::statement;
      break;
    case DebugPointKind::call:
      kind = ExecutionPointKind::call;
      break;
    case DebugPointKind::wait:
      kind = ExecutionPointKind::wait;
      break;
    case DebugPointKind::assertion:
      kind = ExecutionPointKind::assertion;
      break;
    case DebugPointKind::process_entry:
      kind = ExecutionPointKind::process_entry;
      break;
    }
    notify_execution_point(
        process, instruction, kind, process.current_source,
        process.current_scope);
    if (scheduler.stop_requested()) {
      queue_current(process.program.id);
    }
    return;
  }
  if (const auto* wait = fsim::runtime::simir::operation_get_if<WaitFor>(&operation)) {
    clear_wait_timeout(process);
    auto delay = wait->delay;
    if (wait->source) {
      try {
        const auto payload = process.executor
            ? process.executor->read_register(
                  *wait->source, wait->source_width)
            : get_register(process, *wait->source);
        delay = normalized_dynamic_wait_delay(*wait, payload);
      } catch (const std::exception& error) {
        process.pc = instruction;
        fail(process, error.what());
      }
    }
    if (delay == 0) {
      process.status = ProcessStatus::waiting;
      if (process.program.reactive) {
        queue_next_delta(process.program.id);
      } else {
        process.queued = true;
        scheduler.schedule(
            SchedulerPhase::inactive,
            process.program.id,
            [this, id = process.program.id](Scheduler&) {
              auto& state = get_process(id);
              state.queued = false;
              execute(id);
            });
      }
      notify_execution_point(
          process, instruction, ExecutionPointKind::process_suspend,
          process.current_source);
      return;
    }
    if (delay
        > std::numeric_limits<SimulationTick>::max() - scheduler.now()) {
      process.pc = instruction;
      fail(process, "simulation time overflow in WaitFor");
    }
    queue_at(process.program.id, scheduler.now() + delay);
    process.status = ProcessStatus::waiting;
    notify_execution_point(
        process, instruction, ExecutionPointKind::process_suspend,
        process.current_source);
    return;
  }
  if (const auto* wait = fsim::runtime::simir::operation_get_if<WaitOn>(&operation)) {
    if (wait->signals.empty() && !wait->timeout) {
      process.pc = instruction;
      fail(
          process,
          "WaitOn requires at least one signal or a timeout");
    }
    if (!wait->edges.empty()
        && wait->edges.size() != wait->signals.size()) {
      process.pc = instruction;
      fail(process, "WaitOn edge count must match its signal count");
    }
    if (!wait->timeout
        && (wait->timeout_result
            || wait->timeout_origin)) {
      process.pc = instruction;
      fail(
          process,
          "WaitOn timeout metadata requires a timeout");
    }
    if (wait->timeout_origin
        && !wait->timeout_result) {
      process.pc = instruction;
      fail(
          process,
          "WaitOn timeout rearm requires a result register");
    }
    if (wait->timeout_origin) {
      if (*wait->timeout_origin >= instruction) {
        process.pc = instruction;
        fail(
            process,
            "WaitOn timeout origin must precede its rearm");
      }
      const auto* origin = fsim::runtime::simir::operation_get_if<WaitOn>(
          &process.program.operations[*wait->timeout_origin]);
      if (origin == nullptr
          || !origin->timeout
          || origin->timeout_origin
          || origin->timeout != wait->timeout
          || origin->timeout_result
              != wait->timeout_result
          || origin->signals != wait->signals
          || origin->edges != wait->edges) {
        process.pc = instruction;
        fail(
            process,
            "WaitOn timeout rearm does not match its origin");
      }
    }
    process.waiting_on_signal = true;
    process.status = ProcessStatus::waiting;
    process.dynamic_sensitivity.clear();
    process.dynamic_sensitivity.reserve(wait->signals.size());
    for (std::size_t index = 0; index < wait->signals.size(); ++index) {
      const auto signal = wait->signals[index];
      (void)get_signal(signal);
      const auto edge =
          wait->edges.empty() ? EdgeKind::any : wait->edges[index];
      switch (edge) {
      case EdgeKind::any:
        break;
      case EdgeKind::posedge:
      case EdgeKind::negedge:
        if (get_signal(signal).initial_value.width() != 1) {
          process.pc = instruction;
          fail(process, "WaitOn edge requires a scalar signal");
        }
        break;
      default:
        process.pc = instruction;
        fail(process, "WaitOn has an invalid edge kind");
      }
      process.dynamic_sensitivity.push_back({signal, edge});
    }
    std::sort(
        process.dynamic_sensitivity.begin(),
        process.dynamic_sensitivity.end(),
        [](const Sensitivity& lhs, const Sensitivity& rhs) {
          return lhs.signal < rhs.signal
              || (lhs.signal == rhs.signal
                  && lhs.edge < rhs.edge);
        });
    process.dynamic_sensitivity.erase(
        std::unique(
            process.dynamic_sensitivity.begin(),
            process.dynamic_sensitivity.end(),
            [](const Sensitivity& lhs, const Sensitivity& rhs) {
              return lhs.signal == rhs.signal
                  && lhs.edge == rhs.edge;
            }),
        process.dynamic_sensitivity.end());
    for (const auto sensitivity : process.dynamic_sensitivity) {
      dynamic_fanout[sensitivity.signal].push_back(
          {process.program.id, sensitivity.edge});
    }
    if (wait->timeout) {
      if (wait->timeout_origin) {
        rearm_wait_timeout(
            process,
            instruction,
            *wait->timeout_origin,
            wait->timeout_result);
      } else {
        begin_wait_timeout(
            process,
            instruction,
            *wait->timeout,
            wait->timeout_result);
      }
    } else {
      clear_wait_timeout(process);
    }
    notify_execution_point(
        process, instruction, ExecutionPointKind::process_suspend,
        process.current_source);
    return;
  }
  if (fsim::runtime::simir::operation_holds<WaitSensitivity>(operation)) {
    clear_wait_timeout(process);
    if (process.program.static_sensitivity.empty()) {
      process.pc = instruction;
      fail(process, "WaitSensitivity requires a static sensitivity list");
    }
    process.waiting_on_static = true;
    process.status = ProcessStatus::waiting;
    notify_execution_point(
        process, instruction, ExecutionPointKind::process_suspend,
        process.current_source);
    return;
  }
  if (fsim::runtime::simir::operation_holds<WaitForever>(operation)) {
    clear_wait_timeout(process);
    process.status = ProcessStatus::waiting;
    notify_execution_point(
        process, instruction, ExecutionPointKind::process_suspend,
        process.current_source);
    return;
  }
  if (fsim::runtime::simir::operation_holds<Yield>(operation)) {
    clear_wait_timeout(process);
    process.status = ProcessStatus::waiting;
    queue_next_delta(process.program.id);
    notify_execution_point(
        process, instruction, ExecutionPointKind::process_suspend,
        process.current_source);
    return;
  }
  const auto read_boundary_register = [&](const RegisterId id) {
    return process.executor->read_register(id, 64);
  };
  if (const auto* class_allocate =
          fsim::runtime::simir::operation_get_if<ClassAllocate>(&operation)) {
    if (!class_allocate_hook) {
      fail(process, "class allocation service is unavailable");
    }
    std::vector<PackedLogic4> actuals;
    actuals.reserve(class_allocate->constructor_actuals.size());
    for (const auto actual : class_allocate->constructor_actuals) {
      actuals.push_back(read_boundary_register(actual));
    }
    const auto handle = class_allocate_hook(
        process.program.name,
        class_allocate->specialization_identity,
        class_allocate->declared_type,
        actuals,
        class_allocate->constructor_actual_names);
    write_process_register(
        process,
        class_allocate->destination,
        PackedLogic4::from_aval_bval(64, handle, 0));
    return;
  }
  if (const auto* property =
          fsim::runtime::simir::operation_get_if<ClassPropertyRead>(
              &operation)) {
    if (!class_property_read_hook) {
      fail(process, "class property service is unavailable");
    }
    const auto handle = read_boundary_register(
        property->receiver).low_word().aval;
    write_process_register(
        process,
        property->destination,
        resize_class_value(
            class_property_read_hook(handle, property->property_identity),
            property->width));
    return;
  }
  if (const auto* property =
          fsim::runtime::simir::operation_get_if<ClassPropertyWrite>(
              &operation)) {
    if (!class_property_write_hook) {
      fail(process, "class property service is unavailable");
    }
    class_property_write_hook(
        read_boundary_register(property->receiver).low_word().aval,
        property->property_identity,
        read_boundary_register(property->source));
    return;
  }
  if (const auto* method =
          fsim::runtime::simir::operation_get_if<ClassMethodCall>(
              &operation)) {
    if (!class_method_call_hook) {
      fail(process, "class method service is unavailable");
    }
    std::vector<PackedLogic4> actuals;
    actuals.reserve(method->actuals.size());
    for (const auto actual : method->actuals) {
      actuals.push_back(read_boundary_register(actual));
    }
    const auto handle = read_boundary_register(
        method->receiver).low_word().aval;
    write_process_register(
        process,
        method->destination,
        resize_class_value(
            class_method_call_hook(
                handle,
                method->method_identity,
                actuals,
                method->actual_names,
                method->actual_directions,
                method->virtual_dispatch),
            method->result_width));
    for (std::size_t index = 0; index < actuals.size(); ++index) {
      write_process_register(process, method->actuals[index], actuals[index]);
    }
    return;
  }
  if (const auto* property =
          fsim::runtime::simir::operation_get_if<ClassStaticPropertyRead>(
              &operation)) {
    if (!class_static_property_read_hook) {
      fail(process, "class static property service is unavailable");
    }
    write_process_register(
        process,
        property->destination,
        resize_class_value(
            class_static_property_read_hook(property->property_identity),
            property->width));
    return;
  }
  if (const auto* property =
          fsim::runtime::simir::operation_get_if<ClassStaticPropertyWrite>(
              &operation)) {
    if (!class_static_property_write_hook) {
      fail(process, "class static property service is unavailable");
    }
    class_static_property_write_hook(
        property->property_identity,
        read_boundary_register(property->source));
    return;
  }
  if (const auto* method =
          fsim::runtime::simir::operation_get_if<ClassStaticMethodCall>(
              &operation)) {
    if (!class_static_method_call_hook) {
      fail(process, "class static method service is unavailable");
    }
    std::vector<PackedLogic4> actuals;
    actuals.reserve(method->actuals.size());
    for (const auto actual : method->actuals) {
      actuals.push_back(read_boundary_register(actual));
    }
    write_process_register(
        process,
        method->destination,
        resize_class_value(
            class_static_method_call_hook(
                method->method_identity,
                actuals,
                method->actual_names,
                method->actual_directions),
            method->result_width));
    for (std::size_t index = 0; index < actuals.size(); ++index) {
      write_process_register(process, method->actuals[index], actuals[index]);
    }
    return;
  }
  if (handle_synchronization_boundary(process, instruction, operation)) {
    return;
  }
  if (handle_process_boundary(process, instruction, operation)) {
    return;
  }
  if (handle_fork_boundary(process, instruction, operation)) {
    return;
  }
  if (fsim::runtime::simir::operation_holds<Pause>(operation)) {
    clear_wait_timeout(process);
    scheduler.request_stop();
    queue_current(process.program.id);
    notify_execution_point(
        process, instruction, ExecutionPointKind::process_suspend,
        process.current_source);
    return;
  }
  if (fsim::runtime::simir::operation_holds<Stop>(operation)) {
    clear_wait_timeout(process);
    process.halted = true;
    stopped_by_design = true;
    scheduler.request_stop();
    notify_execution_point(
        process, instruction, ExecutionPointKind::process_suspend,
        process.current_source);
    return;
  }
  if (fsim::runtime::simir::operation_holds<Halt>(operation)) {
    clear_wait_timeout(process);
    notify_execution_point(
        process, instruction, ExecutionPointKind::process_suspend,
        process.current_source);
    if (process.fork_parent) {
      complete_fork_child(process);
    } else {
      complete_process(process, ProcessStatus::finished);
    }
    return;
  }

  process.pc = instruction;
  fail(
      process,
      "executor returned at an operation that is not a kernel boundary");
}

void Interpreter::Impl::execute(ProcessId id) {
  auto &process = get_process(id);
  if (!process.halted) {
    process.status = ProcessStatus::running;
  }
  if (process.executor) {
    ExecutionContext context{*this, id};
    while (!process.halted) {
      const auto boundary = process.executor->resume(context, process.pc);
      const bool debug_boundary =
          boundary.instruction < process.program.operations.size()
          && fsim::runtime::simir::operation_holds<DebugPoint>(
              process.program.operations[boundary.instruction]);
      const bool class_boundary =
          boundary.instruction < process.program.operations.size()
          && (fsim::runtime::simir::operation_holds<ClassAllocate>(
                  process.program.operations[boundary.instruction])
              || fsim::runtime::simir::operation_holds<ClassPropertyRead>(
                  process.program.operations[boundary.instruction])
              || fsim::runtime::simir::operation_holds<ClassPropertyWrite>(
                  process.program.operations[boundary.instruction])
              || fsim::runtime::simir::operation_holds<ClassMethodCall>(
                  process.program.operations[boundary.instruction])
              || fsim::runtime::simir::operation_holds<
                  ClassStaticPropertyRead>(
                  process.program.operations[boundary.instruction])
              || fsim::runtime::simir::operation_holds<
                  ClassStaticPropertyWrite>(
                  process.program.operations[boundary.instruction])
              || fsim::runtime::simir::operation_holds<ClassStaticMethodCall>(
                  process.program.operations[boundary.instruction]));
      const bool immediate_process_boundary =
          boundary.instruction < process.program.operations.size()
          && (fsim::runtime::simir::operation_holds<ProcessSelf>(
                  process.program.operations[boundary.instruction])
              || fsim::runtime::simir::operation_holds<ProcessStatusQuery>(
                  process.program.operations[boundary.instruction])
              || fsim::runtime::simir::operation_holds<ProcessCompleted>(
                  process.program.operations[boundary.instruction]));
      const bool synchronization_boundary =
          boundary.instruction < process.program.operations.size()
          && (fsim::runtime::simir::operation_holds<MailboxCreate>(
                  process.program.operations[boundary.instruction])
              || fsim::runtime::simir::operation_holds<MailboxPut>(
                  process.program.operations[boundary.instruction])
              || fsim::runtime::simir::operation_holds<MailboxGet>(
                  process.program.operations[boundary.instruction])
              || fsim::runtime::simir::operation_holds<MailboxNum>(
                  process.program.operations[boundary.instruction])
              || fsim::runtime::simir::operation_holds<SemaphoreCreate>(
                  process.program.operations[boundary.instruction])
              || fsim::runtime::simir::operation_holds<SemaphoreGet>(
                  process.program.operations[boundary.instruction])
              || fsim::runtime::simir::operation_holds<SemaphorePut>(
                  process.program.operations[boundary.instruction]));
      if (boundary.external.kind
          == ExternalSuspendKind::simir_boundary) {
        handle_boundary(
            process, boundary.instruction, boundary.next_instruction);
      } else {
        handle_external_boundary(
            process,
            boundary.instruction,
            boundary.next_instruction,
            boundary.external);
      }
      if ((class_boundary || immediate_process_boundary
           || (synchronization_boundary
               && process.status == ProcessStatus::running
               && !process.queued))
          && !scheduler.stop_requested()) {
        continue;
      }
      if (!debug_boundary || scheduler.stop_requested()) {
        return;
      }
    }
    return;
  }

  while (!process.halted) {
    if (process.pc >= process.program.operations.size()) {
      fail(process, "program counter is outside the operation stream");
    }

    const auto instruction = process.pc;
    const auto &operation = process.program.operations[instruction];
    bool boundary = false;
    const auto selected_offset =
        [&](const DynamicIndex& selection) -> std::uint32_t {
          try {
            return dynamic_index_offset(
                get_register(process, selection.index),
                selection);
          } catch (const std::invalid_argument& error) {
            fail(process, error.what());
          }
        };
    const auto known_string_index =
        [&](const RegisterId index_register,
            const bool signed_index,
            const std::size_t size) -> std::size_t {
          const auto& value =
              get_register(process, index_register);
          if (value.width() == 0 || value.width() > 64) {
            fail(
                process,
                "string index must be a nonempty value of at most 64 bits");
          }
          const auto word = value.low_word();
          if (word.bval != 0) {
            fail(process, "string index contains X or Z");
          }
          if (signed_index && word.width != 0
              && word.width < 64
              && ((word.aval >> (word.width - 1U)) & 1U) != 0) {
            fail(process, "string index is negative");
          }
          if (signed_index && word.width == 64
              && (word.aval >> 63U) != 0) {
            fail(process, "string index is negative");
          }
          if (word.aval >= size) {
            fail(process, "string index is outside the code-point range");
          }
          return static_cast<std::size_t>(word.aval);
        };
    fsim::runtime::simir::visit_operation(
        [&](const auto& op) {
          using OperationType = std::decay_t<decltype(op)>;
          if constexpr (std::is_same_v<OperationType, LoadConstant>) {
            get_register(process, op.destination) =
                coerce_value_kind(
                    op.value,
                    register_value_kind(
                        process, op.destination));
            ++process.pc;
          } else if constexpr (std::is_same_v<OperationType, ReadSignal>) {
            get_register(process, op.destination) =
                coerce_value_kind(
                    get_signal(op.signal).initial_value,
                    register_value_kind(
                        process, op.destination));
            ++process.pc;
          } else if constexpr (std::is_same_v<OperationType, SignalEvent>) {
            (void)get_signal(op.signal);
            const auto& event = signal_events[op.signal];
            const auto active =
                event
                && event->first == scheduler.now()
                && event->second == scheduler.delta();
            get_register(process, op.destination) =
                PackedLogic4(
                    1, active ? Logic4::one : Logic4::zero);
            ++process.pc;
          } else if constexpr (std::is_same_v<OperationType, SignalLastValue>) {
            (void)get_signal(op.signal);
            get_register(process, op.destination) =
                coerce_value_kind(
                    signal_last_values[op.signal],
                    register_value_kind(
                        process, op.destination));
            ++process.pc;
          } else if constexpr (std::is_same_v<OperationType, SignalLastEvent>) {
            (void)get_signal(op.signal);
            const auto& event = signal_events[op.signal];
            const auto elapsed =
                event
                    ? scheduler.now() - event->first
                    : std::numeric_limits<SimulationTick>::max();
            get_register(process, op.destination) =
                PackedLogic4::from_aval_bval(64, elapsed, 0);
            ++process.pc;
          } else if constexpr (
              std::is_same_v<OperationType, ReadSimulationTime>) {
            get_register(process, op.destination) =
                PackedLogic4::from_aval_bval(64, scheduler.now(), 0);
            ++process.pc;
          } else if constexpr (
              std::is_same_v<OperationType, VitalTimingCheck>) {
            ExecutionContext context{*this, id};
            PackedLogic4 result(1);
            result.fill(context.evaluate_vital_timing_check(
                instruction, op));
            get_register(process, op.destination) = std::move(result);
            ++process.pc;
          } else if constexpr (std::is_same_v<OperationType, VitalDelay>) {
            execute_vital_delay_operation(id, process, instruction, op);
            ++process.pc;
          } else if constexpr (std::is_same_v<OperationType, SignalActive>) {
            (void)get_signal(op.signal);
            const auto& transaction = signal_transactions[op.signal];
            const auto active =
                transaction
                && transaction->first == scheduler.now()
                && transaction->second == scheduler.delta();
            get_register(process, op.destination) =
                PackedLogic4(
                    1, active ? Logic4::one : Logic4::zero);
            ++process.pc;
          } else if constexpr (
              std::is_same_v<OperationType, SignalLastActive>) {
            const auto elapsed = signal_attribute_detail::last_active(
                *this, op.signal);
            get_register(process, op.destination) =
                PackedLogic4::from_aval_bval(64, elapsed, 0);
            ++process.pc;
          } else if constexpr (
              std::is_same_v<OperationType, SignalDriving>) {
            const auto driving = signal_attribute_detail::driving(
                *this, process.program.id, op.signal);
            get_register(process, op.destination) =
                PackedLogic4(
                    1, driving ? Logic4::one : Logic4::zero);
            ++process.pc;
          } else if constexpr (
              std::is_same_v<OperationType, SignalDrivingValue>) {
            if (!signal_attribute_detail::driving(
                    *this, process.program.id, op.signal)) {
              fail(process,
                   "VHDL 'driving_value queried a signal without a driver");
            }
            get_register(process, op.destination) =
                coerce_value_kind(
                    signal_attribute_detail::driving_value(
                        *this, process.program.id, op.signal),
                    register_value_kind(process, op.destination));
            ++process.pc;
          } else if constexpr (std::is_same_v<OperationType, CopyRegister>) {
            get_register(process, op.destination) =
                coerce_value_kind(
                    get_register(process, op.source),
                    register_value_kind(
                        process, op.destination));
            ++process.pc;
          } else if constexpr (std::is_same_v<OperationType, ClassAllocate>) {
            if (!class_allocate_hook) {
              fail(process, "class allocation service is unavailable");
            }
            std::vector<PackedLogic4> actuals;
            actuals.reserve(op.constructor_actuals.size());
            for (const auto actual : op.constructor_actuals) {
              actuals.push_back(get_register(process, actual));
            }
            const auto handle = class_allocate_hook(
                process.program.name,
                op.specialization_identity,
                op.declared_type,
                actuals,
                op.constructor_actual_names);
            get_register(process, op.destination) =
                PackedLogic4::from_aval_bval(64, handle, 0);
            ++process.pc;
          } else if constexpr (
              std::is_same_v<OperationType, ClassPropertyRead>) {
            if (!class_property_read_hook) {
              fail(process, "class property service is unavailable");
            }
            const auto handle =
                get_register(process, op.receiver).low_word().aval;
            get_register(process, op.destination) = resize_class_value(
                class_property_read_hook(handle, op.property_identity),
                op.width);
            ++process.pc;
          } else if constexpr (
              std::is_same_v<OperationType, ClassPropertyWrite>) {
            if (!class_property_write_hook) {
              fail(process, "class property service is unavailable");
            }
            const auto handle =
                get_register(process, op.receiver).low_word().aval;
            class_property_write_hook(
                handle, op.property_identity,
                get_register(process, op.source));
            ++process.pc;
          } else if constexpr (
              std::is_same_v<OperationType, ClassMethodCall>) {
            if (!class_method_call_hook) {
              fail(process, "class method service is unavailable");
            }
            std::vector<PackedLogic4> actuals;
            actuals.reserve(op.actuals.size());
            for (const auto actual : op.actuals) {
              actuals.push_back(get_register(process, actual));
            }
            const auto handle =
                get_register(process, op.receiver).low_word().aval;
            get_register(process, op.destination) = class_method_call_hook(
                handle,
                op.method_identity,
                actuals,
                op.actual_names,
                op.actual_directions,
                op.virtual_dispatch);
            for (std::size_t index = 0; index < actuals.size(); ++index) {
              get_register(process, op.actuals[index]) = actuals[index];
            }
            ++process.pc;
          } else if constexpr (
              std::is_same_v<OperationType, ClassStaticPropertyRead>) {
            if (!class_static_property_read_hook) {
              fail(process, "class static property service is unavailable");
            }
            get_register(process, op.destination) = resize_class_value(
                class_static_property_read_hook(op.property_identity),
                op.width);
            ++process.pc;
          } else if constexpr (
              std::is_same_v<OperationType, ClassStaticPropertyWrite>) {
            if (!class_static_property_write_hook) {
              fail(process, "class static property service is unavailable");
            }
            class_static_property_write_hook(
                op.property_identity, get_register(process, op.source));
            ++process.pc;
          } else if constexpr (
              std::is_same_v<OperationType, ClassStaticMethodCall>) {
            if (!class_static_method_call_hook) {
              fail(process, "class static method service is unavailable");
            }
            std::vector<PackedLogic4> actuals;
            actuals.reserve(op.actuals.size());
            for (const auto actual : op.actuals) {
              actuals.push_back(get_register(process, actual));
            }
            get_register(process, op.destination) =
                class_static_method_call_hook(
                    op.method_identity,
                    actuals,
                    op.actual_names,
                    op.actual_directions);
            for (std::size_t index = 0; index < actuals.size(); ++index) {
              get_register(process, op.actuals[index]) = actuals[index];
            }
            ++process.pc;
          } else if constexpr (std::is_same_v<OperationType, LoadStringConstant>) {
            if (op.value.size() > maximum_string_bytes) {
              fail(process, "string literal exceeds 4096-byte limit");
            }
            try {
              (void)systemverilog_string_length(op.value);
            } catch (const std::invalid_argument& error) {
              fail(process, error.what());
            }
            get_string_register(process, op.destination) = op.value;
            ++process.pc;
          } else if constexpr (std::is_same_v<OperationType, CopyStringRegister>) {
            get_string_register(process, op.destination) =
                get_string_register(process, op.source);
            ++process.pc;
          } else if constexpr (std::is_same_v<OperationType, ReadStringObject>) {
            get_string_register(process, op.destination) =
                get_string_object(op.object).initial_value;
            ++process.pc;
          } else if constexpr (std::is_same_v<OperationType, WriteStringObject>) {
            get_string_object(op.object).initial_value =
                get_string_register(process, op.source);
            ++process.pc;
          } else if constexpr (std::is_same_v<OperationType, ConcatenateStrings>) {
            std::string result;
            for (const auto operand : op.operands) {
              const auto& value =
                  get_string_register(process, operand);
              if (value.size()
                  > maximum_string_bytes - result.size()) {
                fail(
                    process,
                    "string concatenation exceeds 4096-byte limit");
              }
              result += value;
            }
            get_string_register(process, op.destination) =
                std::move(result);
            ++process.pc;
          } else if constexpr (std::is_same_v<OperationType, CompareStrings>) {
            bool equal{};
            try {
              equal = systemverilog_string_compare(
                          get_string_register(process, op.lhs),
                          get_string_register(process, op.rhs))
                  == 0;
            } catch (const std::invalid_argument& error) {
              fail(process, error.what());
            }
            get_register(process, op.destination) =
                PackedLogic4{
                    1,
                    equal != op.not_equal
                        ? Logic4::one
                        : Logic4::zero};
            ++process.pc;
          } else if constexpr (std::is_same_v<OperationType, StringLength>) {
            std::size_t size{};
            try {
              size = systemverilog_string_length(
                  get_string_register(process, op.source));
            } catch (const std::invalid_argument& error) {
              fail(process, error.what());
            }
            get_register(process, op.destination) =
                PackedLogic4::from_aval_bval(
                    32, static_cast<std::uint32_t>(size), 0);
            ++process.pc;
          } else if constexpr (std::is_same_v<OperationType, StringIndex>) {
            auto& source =
                get_string_register(process, op.source);
            std::size_t size{};
            try {
              size = systemverilog_string_length(source);
            } catch (const std::invalid_argument& error) {
              fail(process, error.what());
            }
            const auto index =
                known_string_index(
                    op.index, op.signed_index, size);
            std::uint32_t code_point{};
            try {
              code_point = systemverilog_string_at(source, index);
            } catch (const std::invalid_argument& error) {
              fail(process, error.what());
            }
            get_register(process, op.destination) =
                PackedLogic4::from_aval_bval(
                    32, code_point, 0);
            ++process.pc;
          } else if constexpr (
              std::is_same_v<OperationType, StringReplaceCodePoint>) {
            auto& target =
                get_string_register(process, op.target);
            std::size_t size{};
            try {
              size = systemverilog_string_length(target);
            } catch (const std::invalid_argument& error) {
              fail(process, error.what());
            }
            const auto index =
                known_string_index(
                    op.index, op.signed_index, size);
            const auto code_point =
                get_register(process, op.source).low_word();
            if (code_point.bval != 0) {
              fail(process, "string replacement code point contains X or Z");
            }
            try {
              systemverilog_string_replace(
                  target, index,
                  static_cast<std::uint32_t>(code_point.aval),
                  maximum_string_bytes);
            } catch (const std::exception& error) {
              fail(process, error.what());
            }
            ++process.pc;
          } else if constexpr (std::is_same_v<OperationType, StringMethod>) {
            execute_string(process, op);
          } else if constexpr (std::is_same_v<OperationType, UnaryNot>) {
            get_register(process, op.destination) =
                unary_not(get_register(process, op.source));
            ++process.pc;
          } else if constexpr (std::is_same_v<OperationType, LogicalNot>) {
            get_register(process, op.destination) =
                logical_not(get_register(process, op.source));
            ++process.pc;
          } else if constexpr (std::is_same_v<OperationType, LogicalBinary>) {
            get_register(process, op.destination) =
                logical_binary(
                    op.operation,
                    get_register(process, op.lhs),
                    get_register(process, op.rhs));
            ++process.pc;
          } else if constexpr (std::is_same_v<OperationType, Reduction>) {
            get_register(process, op.destination) =
                reduce_value(
                    op.operation,
                    get_register(process, op.source));
            ++process.pc;
          } else if constexpr (std::is_same_v<OperationType, CountOnes>) {
            get_register(process, op.destination) =
                count_ones_value(
                    get_register(process, op.source));
            ++process.pc;
          } else if constexpr (std::is_same_v<OperationType, CountBits>) {
            get_register(process, op.destination) =
                count_bits_value(
                    get_register(process, op.source),
                    op.state_mask);
            ++process.pc;
          } else if constexpr (std::is_same_v<OperationType, Shift>) {
            get_register(process, op.destination) =
                shift_value(
                    op.operation,
                    get_register(process, op.value),
                    get_register(process, op.amount),
                    op.signed_amount);
            ++process.pc;
          } else if constexpr (std::is_same_v<OperationType, Extract>) {
            try {
              get_register(process, op.destination) =
                  extract_value(
                      get_register(process, op.source),
                      op.offset,
                      op.width);
            } catch (const std::invalid_argument& error) {
              fail(process, error.what());
            }
            ++process.pc;
          } else if constexpr (std::is_same_v<OperationType, DynamicExtract>) {
            try {
              get_register(process, op.destination) =
                  extract_value(
                      get_register(process, op.source),
                      dynamic_index_offset(
                          get_register(process, op.selection.index),
                          op.selection),
                      1);
            } catch (const std::invalid_argument& error) {
              fail(process, error.what());
            }
            ++process.pc;
          } else if constexpr (std::is_same_v<OperationType, DynamicPartSelect>) {
            try {
              get_register(process, op.destination) =
                  dynamic_part_select_value(
                      get_register(process, op.source),
                      get_register(process, op.base),
                      op.left,
                      op.right,
                      op.base_offset,
                      op.width,
                      op.increasing,
                      op.source_descending,
                      op.two_state);
            } catch (const std::invalid_argument& error) {
              fail(process, error.what());
            }
            ++process.pc;
          } else if constexpr (std::is_same_v<OperationType, Insert>) {
            try {
              get_register(process, op.destination) =
                  insert_value(
                      get_register(process, op.target),
                      get_register(process, op.source),
                      op.offset);
            } catch (const std::invalid_argument& error) {
              fail(process, error.what());
            }
            ++process.pc;
          } else if constexpr (std::is_same_v<OperationType, DynamicInsert>) {
            try {
              get_register(process, op.destination) =
                  insert_value(
                      get_register(process, op.target),
                      get_register(process, op.source),
                      dynamic_index_offset(
                          get_register(process, op.selection.index),
                          op.selection));
            } catch (const std::invalid_argument& error) {
              fail(process, error.what());
            }
            ++process.pc;
          } else if constexpr (std::is_same_v<OperationType, DynamicPartInsert>) {
            try {
              get_register(process, op.destination) =
                  dynamic_part_insert_value(
                      get_register(process, op.target),
                      get_register(process, op.source),
                      get_register(
                          process, op.selection.base),
                      op.selection);
            } catch (const std::invalid_argument& error) {
              fail(process, error.what());
            }
            ++process.pc;
          } else if constexpr (std::is_same_v<OperationType, Concatenate>) {
            std::vector<PackedLogic4> operands;
            operands.reserve(op.operands.size());
            for (const auto operand : op.operands) {
              operands.push_back(get_register(process, operand));
            }
            try {
              get_register(process, op.destination) =
                  concatenate_values(operands, op.width);
            } catch (const std::invalid_argument& error) {
              fail(process, error.what());
            }
            ++process.pc;
          } else if constexpr (
              std::is_same_v<OperationType, SystemVerilogScalarBinary>) {
            const auto result = systemverilog_scalar_binary_payload(
                op.operation,
                get_register(process, op.lhs), op.lhs_kind,
                get_register(process, op.rhs), op.rhs_kind,
                op.result_kind);
            if (!result) {
              fail(
                  process,
                  "SystemVerilog scalar binary operation failed (error "
                      + std::to_string(static_cast<unsigned>(result.error))
                      + ", lhs kind "
                      + std::to_string(static_cast<unsigned>(op.lhs_kind))
                      + ", rhs kind "
                      + std::to_string(static_cast<unsigned>(op.rhs_kind))
                      + ")");
            }
            get_register(process, op.destination) = result.value;
            ++process.pc;
          } else if constexpr (std::is_same_v<OperationType, Binary>) {
            try {
              get_register(process, op.destination) =
                  binary_value(op.operation, get_register(process, op.lhs),
                               get_register(process, op.rhs));
            } catch (const std::invalid_argument &error) {
              fail(process, error.what());
            }
            ++process.pc;
          } else if constexpr (std::is_same_v<OperationType, IntegerUnary>) {
            try {
              get_register(process, op.destination) =
                  integer_unary_value(
                      op.operation,
                      get_register(process, op.source));
            } catch (const std::invalid_argument& error) {
              fail(process, error.what());
            }
            ++process.pc;
          } else if constexpr (std::is_same_v<OperationType, IntegerBinary>) {
            try {
              get_register(process, op.destination) =
                  integer_binary_value(
                      op.operation,
                      get_register(process, op.lhs),
                      get_register(process, op.rhs));
            } catch (const std::invalid_argument& error) {
              fail(process, error.what());
            }
            ++process.pc;
          } else if constexpr (std::is_same_v<OperationType, IntegerCheck>) {
            try {
              check_integer_range(
                  get_register(process, op.source),
                  op.lower,
                  op.upper);
            } catch (const std::invalid_argument& error) {
              fail(process, error.what());
            }
            ++process.pc;
          } else if constexpr (std::is_same_v<OperationType, ConditionalSelect>) {
            try {
              get_register(process, op.destination) =
                  conditional_value(
                      get_register(process, op.condition),
                      get_register(process, op.when_true),
                      get_register(process, op.when_false));
            } catch (const std::invalid_argument& error) {
              fail(process, error.what());
            }
            ++process.pc;
          } else if constexpr (std::is_same_v<OperationType, WriteBlocking>) {
            auto value = get_register(process, op.source);
            ++process.pc;
            commit_driver(
                process.program.id,
                op.signal,
                std::move(value));
          } else if constexpr (std::is_same_v<OperationType, WriteUpdate>) {
            auto value = get_register(process, op.source);
            ++process.pc;
            stage_update(
                process.program.id,
                op.signal,
                std::move(value));
          } else if constexpr (std::is_same_v<OperationType, WriteAfter>) {
            auto value = get_register(process, op.source);
            ++process.pc;
            scheduler.schedule_after(
                op.delay, SchedulerPhase::update, process.program.id,
                [this,
                 driver = process.program.id,
                 signal = op.signal,
                 value = std::move(value)](Scheduler &) mutable {
                  stage_update(
                      driver, signal, std::move(value));
                });
          } else if constexpr (std::is_same_v<OperationType, WriteInertial>) {
            auto value = get_register(process, op.source);
            ++process.pc;
            schedule_inertial(
                process.program.id,
                op.signal,
                std::move(value),
                std::nullopt,
                op.delays);
          } else if constexpr (std::is_same_v<OperationType, WriteProjected>) {
            auto value = get_register(process, op.source);
            ++process.pc;
            schedule_projected(
                process.program.id,
                op.signal,
                value,
                std::nullopt,
                op.delay,
                op.rejection,
                op.mode);
          } else if constexpr (std::is_same_v<OperationType, WriteProjectedWaveform>) {
            std::vector<ProjectedWaveformValue> elements;
            elements.reserve(op.elements.size());
            for (const auto& element : op.elements) {
              elements.push_back(
                  {get_register(process, element.source),
                   element.delay});
            }
            ++process.pc;
            schedule_projected_waveform(
                process.program.id,
                op.signal,
                elements,
                std::nullopt,
                op.rejection,
                op.mode);
          } else if constexpr (std::is_same_v<OperationType, WriteBlockingSlice>) {
            auto value = get_register(process, op.source);
            ++process.pc;
            commit_driver_slice(
                process.program.id,
                op.signal,
                std::move(value),
                op.offset);
          } else if constexpr (std::is_same_v<OperationType, WriteUpdateSlice>) {
            auto value = get_register(process, op.source);
            ++process.pc;
            stage_update_slice(
                process.program.id,
                op.signal,
                std::move(value),
                op.offset);
          } else if constexpr (std::is_same_v<OperationType, WriteAfterSlice>) {
            auto value = get_register(process, op.source);
            ++process.pc;
            scheduler.schedule_after(
                op.delay,
                SchedulerPhase::update,
                process.program.id,
                [this,
                 driver = process.program.id,
                 signal = op.signal,
                 offset = op.offset,
                 value = std::move(value)](
                    Scheduler&) mutable {
                  stage_update_slice(
                      driver,
                      signal,
                      std::move(value),
                      offset);
                });
          } else if constexpr (std::is_same_v<OperationType, WriteInertialSlice>) {
            auto value = get_register(process, op.source);
            ++process.pc;
            schedule_inertial(
                process.program.id,
                op.signal,
                std::move(value),
                op.offset,
                op.delays);
          } else if constexpr (std::is_same_v<OperationType, WriteProjectedSlice>) {
            auto value = get_register(process, op.source);
            ++process.pc;
            schedule_projected(
                process.program.id,
                op.signal,
                value,
                op.offset,
                op.delay,
                op.rejection,
                op.mode);
          } else if constexpr (std::is_same_v<OperationType, WriteProjectedWaveformSlice>) {
            std::vector<ProjectedWaveformValue> elements;
            elements.reserve(op.elements.size());
            for (const auto& element : op.elements) {
              elements.push_back(
                  {get_register(process, element.source),
                   element.delay});
            }
            ++process.pc;
            schedule_projected_waveform(
                process.program.id,
                op.signal,
                elements,
                op.offset,
                op.rejection,
                op.mode);
          } else if constexpr (std::is_same_v<OperationType, WriteBlockingDynamicSlice>) {
            auto value = get_register(process, op.source);
            const auto offset = selected_offset(op.selection);
            ++process.pc;
            commit_driver_slice(
                process.program.id,
                op.signal,
                std::move(value),
                offset);
          } else if constexpr (std::is_same_v<OperationType, WriteUpdateDynamicSlice>) {
            auto value = get_register(process, op.source);
            const auto offset = selected_offset(op.selection);
            ++process.pc;
            stage_update_slice(
                process.program.id,
                op.signal,
                std::move(value),
                offset);
          } else if constexpr (std::is_same_v<OperationType, WriteAfterDynamicSlice>) {
            auto value = get_register(process, op.source);
            const auto offset = selected_offset(op.selection);
            ++process.pc;
            scheduler.schedule_after(
                op.delay,
                SchedulerPhase::update,
                process.program.id,
                [this,
                 driver = process.program.id,
                 signal = op.signal,
                 offset,
                 value = std::move(value)](
                    Scheduler&) mutable {
                  stage_update_slice(
                      driver,
                      signal,
                      std::move(value),
                      offset);
                });
          } else if constexpr (std::is_same_v<OperationType, WriteBlockingDynamicPartSlice>) {
            try {
              const auto write = dynamic_part_write_value(
                  get_register(process, op.source),
                  get_register(process, op.selection.base),
                  op.selection);
              ++process.pc;
              if (write) {
                commit_driver_slice(
                    process.program.id,
                    op.signal,
                    write->value,
                    write->offset);
              }
            } catch (const std::invalid_argument& error) {
              fail(process, error.what());
            }
          } else if constexpr (std::is_same_v<OperationType, WriteUpdateDynamicPartSlice>) {
            try {
              const auto write = dynamic_part_write_value(
                  get_register(process, op.source),
                  get_register(process, op.selection.base),
                  op.selection);
              ++process.pc;
              if (write) {
                stage_update_slice(
                    process.program.id,
                    op.signal,
                    write->value,
                    write->offset);
              }
            } catch (const std::invalid_argument& error) {
              fail(process, error.what());
            }
          } else if constexpr (std::is_same_v<OperationType, WriteAfterDynamicPartSlice>) {
            try {
              auto write = dynamic_part_write_value(
                  get_register(process, op.source),
                  get_register(process, op.selection.base),
                  op.selection);
              ++process.pc;
              if (write) {
                scheduler.schedule_after(
                    op.delay,
                    SchedulerPhase::update,
                    process.program.id,
                    [this,
                     driver = process.program.id,
                     signal = op.signal,
                     write = std::move(*write)](
                        Scheduler&) mutable {
                      stage_update_slice(
                          driver,
                          signal,
                          std::move(write.value),
                          write.offset);
                    });
              }
            } catch (const std::invalid_argument& error) {
              fail(process, error.what());
            }
          } else if constexpr (std::is_same_v<OperationType, ForceSignalSlice>) {
            try {
              const auto offset = op.selection
                  ? selected_offset(*op.selection)
                  : op.offset;
              force_slice(
                  op.signal,
                  get_register(process, op.source),
                  offset);
            } catch (const std::invalid_argument& error) {
              fail(process, error.what());
            }
            ++process.pc;
          } else if constexpr (std::is_same_v<OperationType, ReleaseSignalSlice>) {
            try {
              const auto offset = op.selection
                  ? selected_offset(*op.selection)
                  : op.offset;
              release_slice(
                  op.signal, offset, op.width);
            } catch (const std::invalid_argument& error) {
              fail(process, error.what());
            }
            ++process.pc;
          } else if constexpr (std::is_same_v<OperationType, WriteInertialDynamicSlice>) {
            auto value = get_register(process, op.source);
            const auto offset = selected_offset(op.selection);
            ++process.pc;
            schedule_inertial(
                process.program.id,
                op.signal,
                std::move(value),
                offset,
                op.delays);
          } else if constexpr (std::is_same_v<OperationType, WriteProjectedDynamicSlice>) {
            auto value = get_register(process, op.source);
            const auto offset = selected_offset(op.selection);
            ++process.pc;
            schedule_projected(
                process.program.id,
                op.signal,
                value,
                offset,
                op.delay,
                op.rejection,
                op.mode);
          } else if constexpr (std::is_same_v<OperationType, WriteProjectedWaveformDynamicSlice>) {
            std::vector<ProjectedWaveformValue> elements;
            elements.reserve(op.elements.size());
            for (const auto& element : op.elements) {
              elements.push_back(
                  {get_register(process, element.source),
                   element.delay});
            }
            const auto offset = selected_offset(op.selection);
            ++process.pc;
            schedule_projected_waveform(
                process.program.id,
                op.signal,
                elements,
                offset,
                op.rejection,
                op.mode);
          } else if constexpr (std::is_same_v<OperationType, WaitFor>) {
            (void)op;
            boundary = true;
          } else if constexpr (std::is_same_v<OperationType, WaitOn>) {
            (void)op;
            boundary = true;
          } else if constexpr (std::is_same_v<OperationType, WaitSensitivity>) {
            boundary = true;
          } else if constexpr (std::is_same_v<OperationType, WaitForever>) {
            boundary = true;
          } else if constexpr (std::is_same_v<OperationType, Yield>) {
            boundary = true;
          } else if constexpr (std::is_same_v<OperationType, Fork>) {
            boundary = true;
          } else if constexpr (std::is_same_v<OperationType, ForkEnd>) {
            boundary = true;
          } else if constexpr (std::is_same_v<OperationType, WaitFork>) {
            boundary = true;
          } else if constexpr (std::is_same_v<OperationType, DisableFork>) {
            boundary = true;
          } else if constexpr (std::is_same_v<OperationType, ProcessSelf>) {
            boundary = true;
          } else if constexpr (
              std::is_same_v<OperationType, ProcessStatusQuery>) {
            boundary = true;
          } else if constexpr (
              std::is_same_v<OperationType, ProcessCompleted>) {
            boundary = true;
          } else if constexpr (
              std::is_same_v<OperationType, ProcessAwait>) {
            boundary = true;
          } else if constexpr (
              std::is_same_v<OperationType, ProcessKill>) {
            boundary = true;
          } else if constexpr (
              std::is_same_v<OperationType, MailboxCreate>
              || std::is_same_v<OperationType, MailboxPut>
              || std::is_same_v<OperationType, MailboxGet>
              || std::is_same_v<OperationType, MailboxNum>
              || std::is_same_v<OperationType, SemaphoreCreate>
              || std::is_same_v<OperationType, SemaphoreGet>
              || std::is_same_v<OperationType, SemaphorePut>) {
            boundary = true;
          } else if constexpr (std::is_same_v<OperationType, Jump>) {
            if (op.target >= process.program.operations.size()) {
              fail(process, "jump target is outside the operation stream");
            }
            process.pc = op.target;
          } else if constexpr (std::is_same_v<OperationType, Call>) {
            const auto pointer =
                get_register(process, op.stack.pointer).low_word();
            if (pointer.bval != 0) {
              fail(process, "call-stack pointer is unknown");
            }
            if (pointer.aval >= op.stack.capacity) {
              fail(process, "call-stack capacity is exhausted");
            }
            if (op.target >= process.program.operations.size()
                || op.return_target
                    >= process.program.operations.size()) {
              fail(process, "call target is outside the operation stream");
            }
            get_register(
                process,
                static_cast<RegisterId>(
                    op.stack.entries + pointer.aval)) =
                PackedLogic4::from_aval_bval(
                    32, op.return_target, 0);
            get_register(process, op.stack.pointer) =
                PackedLogic4::from_aval_bval(
                    32, pointer.aval + 1U, 0);
            process.pc = op.target;
          } else if constexpr (std::is_same_v<OperationType, Return>) {
            const auto pointer =
                get_register(process, op.stack.pointer).low_word();
            if (pointer.bval != 0) {
              fail(process, "call-stack pointer is unknown");
            }
            if (pointer.aval == 0
                || pointer.aval > op.stack.capacity) {
              fail(process, "call-stack underflow");
            }
            const auto next_pointer = pointer.aval - 1U;
            const auto target =
                get_register(
                    process,
                    static_cast<RegisterId>(
                        op.stack.entries + next_pointer)).low_word();
            if (target.bval != 0
                || target.aval
                    >= process.program.operations.size()) {
              fail(process, "call-stack return target is invalid");
            }
            get_register(process, op.stack.pointer) =
                PackedLogic4::from_aval_bval(
                    32, next_pointer, 0);
            process.pc =
                static_cast<InstructionIndex>(target.aval);
          } else if constexpr (std::is_same_v<OperationType, Branch>) {
            const auto &condition = get_register(process, op.condition);
            if (condition.width() != 1) {
              fail(process, "branch condition must be scalar");
            }
            const auto value = condition.get(0);
            InstructionIndex target{};
            if (value != Logic4::zero && value != Logic4::one) {
              if (op.unknown_policy
                  == UnknownBranchPolicy::when_false) {
                target = op.when_false;
              } else {
                fail(
                    process,
                    "branch condition is unknown or high impedance");
              }
            } else {
              target =
                  value == Logic4::one ? op.when_true : op.when_false;
            }
            if (target >= process.program.operations.size()) {
              fail(process, "branch target is outside the operation stream");
            }
            process.pc = target;
          } else if constexpr (std::is_same_v<OperationType, DebugPoint>) {
            boundary = true;
          } else if constexpr (std::is_same_v<OperationType, Assert>) {
            const auto &condition = get_register(process, op.condition);
            if (condition.width() != 1 ||
                condition.get(0) != Logic4::one) {
              const auto message =
                  op.message.empty()
                      ? std::string_view{"assertion failed"}
                      : std::string_view{op.message};
              if (op.severity != AssertionSeverity::failure
                  && report_hook) {
                report_hook(
                    process.program.id,
                    message,
                    op.severity,
                    op.source,
                    scheduler.now(),
                    scheduler.delta());
              }
              if (op.severity == AssertionSeverity::failure) {
                throw AssertionError(
                    process.program.id,
                    process.pc,
                    std::string{message},
                    op.severity,
                    op.source);
              }
            }
            ++process.pc;
          } else if constexpr (std::is_same_v<OperationType, Display>) {
            if (op.postponed) {
              scheduler.schedule(
                  SchedulerPhase::postponed,
                  process.program.id,
                  [this,
                   process_id = process.program.id,
                   text = op.text,
                   newline = op.newline](Scheduler& runtime) {
                    if (output_hook) {
                      output_hook(
                          process_id,
                          text,
                          newline,
                          runtime.now(),
                          runtime.delta());
                    }
                  });
            } else if (output_hook) {
              output_hook(
                  process.program.id,
                  op.text,
                  op.newline,
                  scheduler.now(),
                  scheduler.delta());
            }
            ++process.pc;
          } else if constexpr (std::is_same_v<OperationType, FormatDisplay>) {
            auto text = make_formatted_output(
                op.prefix,
                op.suffix,
                op.format,
                get_register(process, op.source),
                op.signed_decimal,
                op.suppress_leading_zero,
                op.minimum_width,
                op.left_justify,
                op.zero_pad,
                op.scalar_kind);
            if (op.postponed) {
              scheduler.schedule(
                  SchedulerPhase::postponed,
                  process.program.id,
                  [this,
                   process_id = process.program.id,
                   text = std::move(text),
                   newline = op.newline](Scheduler& runtime) {
                    if (output_hook) {
                      output_hook(
                          process_id,
                          text,
                          newline,
                          runtime.now(),
                          runtime.delta());
                    }
                  });
            } else if (output_hook) {
              output_hook(
                  process.program.id,
                  text,
                  op.newline,
                  scheduler.now(),
                  scheduler.delta());
            }
            ++process.pc;
          } else if constexpr (std::is_same_v<OperationType, StringDisplay>) {
            auto text =
                op.prefix
                + get_string_register(process, op.source)
                + op.suffix;
            if (op.postponed) {
              scheduler.schedule(
                  SchedulerPhase::postponed,
                  process.program.id,
                  [this,
                   process_id = process.program.id,
                   text = std::move(text),
                   newline = op.newline](Scheduler& runtime) {
                    if (output_hook) {
                      output_hook(
                          process_id,
                          text,
                          newline,
                          runtime.now(),
                          runtime.delta());
                    }
                  });
            } else if (output_hook) {
              output_hook(
                  process.program.id,
                  text,
                  op.newline,
                  scheduler.now(),
                  scheduler.delta());
            }
            ++process.pc;
          } else if constexpr (std::is_same_v<OperationType, StringReport>) {
            const auto& encoded = get_register(process, op.severity);
            if (encoded.width() != 2
                || encoded.get(0) == Logic4::x
                || encoded.get(0) == Logic4::z
                || encoded.get(1) == Logic4::x
                || encoded.get(1) == Logic4::z) {
              fail(
                  process,
                  "VHDL severity expression produced an invalid value");
            }
            const auto ordinal =
                (encoded.get(0) == Logic4::one ? 1U : 0U)
                | (encoded.get(1) == Logic4::one ? 2U : 0U);
            const auto severity =
                static_cast<AssertionSeverity>(ordinal);
            const auto& message =
                get_string_register(process, op.message);
            if (severity == AssertionSeverity::failure) {
              if (op.standalone && report_hook) {
                report_hook(
                    process.program.id,
                    message,
                    severity,
                    op.source,
                    scheduler.now(),
                    scheduler.delta());
              }
              throw AssertionError(
                  process.program.id,
                  process.pc,
                  message.empty()
                      ? (op.standalone
                             ? "report failure"
                             : "assertion failed")
                      : message,
                  severity,
                  op.source,
                  op.standalone);
            }
            if (report_hook) {
              report_hook(
                  process.program.id,
                  message,
                  severity,
                  op.source,
                  scheduler.now(),
                  scheduler.delta());
            }
            ++process.pc;
          } else if constexpr (std::is_same_v<OperationType, TimeDisplay>) {
            auto text = make_time_output(
                op.prefix,
                op.suffix,
                scheduler.now(),
                op.minimum_width,
                op.left_justify,
                op.zero_pad);
            if (op.postponed) {
              scheduler.schedule(
                  SchedulerPhase::postponed,
                  process.program.id,
                  [this,
                   process_id = process.program.id,
                   text = std::move(text),
                   newline = op.newline](Scheduler& runtime) {
                    if (output_hook) {
                      output_hook(
                          process_id,
                          text,
                          newline,
                          runtime.now(),
                          runtime.delta());
                    }
                  });
            } else if (output_hook) {
              output_hook(
                  process.program.id,
                  text,
                  op.newline,
                  scheduler.now(),
                  scheduler.delta());
            }
            ++process.pc;
          } else if constexpr (std::is_same_v<OperationType, MonitorInstall>) {
            install_monitor(process.program.id, op);
            ++process.pc;
          } else if constexpr (std::is_same_v<OperationType, MonitorControl>) {
            set_monitor_enabled(op.enabled);
            ++process.pc;
          } else if constexpr (std::is_same_v<OperationType, RandomValue>) {
            const auto maximum =
                op.maximum
                    ? std::optional<PackedLogic4>{
                          get_register(process, *op.maximum)}
                    : std::nullopt;
            const auto minimum =
                op.minimum
                    ? std::optional<PackedLogic4>{
                          get_register(process, *op.minimum)}
                    : std::nullopt;
            get_register(process, op.destination) =
                random_value(
                    process.program.id,
                    op.kind,
                    maximum,
                    minimum);
            ++process.pc;
          } else if constexpr (
              std::is_same_v<OperationType, ScopeRandomize>) {
            SystemVerilogScopeRandomizeRequest request;
            request.limits.maximum_domain_values =
                op.maximum_domain_values;
            request.selection =
                (static_cast<std::uint64_t>(next_random(process)) << 32U)
                | next_random(process);
            request.variables.reserve(op.targets.size());
            for (const auto& target : op.targets) {
              request.variables.push_back({
                  target.canonical_identity,
                  {target.domain_kind == ScopeRandomizeDomainKind::enumeration
                       ? SystemVerilogConstraintDomainKind::Enumeration
                       : target.domain_kind
                                 == ScopeRandomizeDomainKind::integer
                             ? SystemVerilogConstraintDomainKind::Integer
                             : SystemVerilogConstraintDomainKind::BitVector,
                   target.width,
                   target.signed_value,
                   target.nominal_type},
                  target.domain,
                  &get_register(process, target.target)});
            }
            const auto result = randomize_systemverilog_scope(request);
            get_register(process, op.destination) =
                PackedLogic4::from_aval_bval(
                    32, result.language_result(), 0);
            ++process.pc;
          } else if constexpr (std::is_same_v<OperationType, Report>) {
            if (report_hook) {
              report_hook(
                  process.program.id,
                  op.message,
                  op.severity,
                  op.source,
                  scheduler.now(),
                  scheduler.delta());
            }
            if (op.severity == AssertionSeverity::failure) {
              throw AssertionError(
                  process.program.id,
                  process.pc,
                  op.message.empty() ? "report failure" : op.message,
                  op.severity,
                  op.source,
                  true);
            }
            ++process.pc;
          } else if constexpr (std::is_same_v<OperationType, Pause>) {
            boundary = true;
          } else if constexpr (std::is_same_v<OperationType, Stop>) {
            boundary = true;
          } else if constexpr (std::is_same_v<OperationType, Halt>) {
            boundary = true;
          } else {
            if constexpr (requires {
                            execute_file(process, op);
                          }) {
              execute_file(process, op);
            } else if constexpr (requires {
                                   execute_container(process, op);
                                 }) {
              execute_container(process, op);
            } else {
              static_assert(
                  sizeof(op) == 0,
                  "unhandled SimIR operation");
            }
          }
        },
        operation);

    if (boundary) {
      const bool debug_boundary =
          fsim::runtime::simir::operation_holds<DebugPoint>(operation);
      const bool immediate_process_boundary =
          fsim::runtime::simir::operation_holds<ProcessSelf>(operation)
          || fsim::runtime::simir::operation_holds<ProcessStatusQuery>(
              operation)
          || fsim::runtime::simir::operation_holds<ProcessCompleted>(
              operation);
      const bool synchronization_boundary =
          fsim::runtime::simir::operation_holds<MailboxCreate>(operation)
          || fsim::runtime::simir::operation_holds<MailboxPut>(operation)
          || fsim::runtime::simir::operation_holds<MailboxGet>(operation)
          || fsim::runtime::simir::operation_holds<MailboxNum>(operation)
          || fsim::runtime::simir::operation_holds<SemaphoreCreate>(operation)
          || fsim::runtime::simir::operation_holds<SemaphoreGet>(operation)
          || fsim::runtime::simir::operation_holds<SemaphorePut>(operation);
      handle_boundary(process, instruction, instruction + 1);
      if ((immediate_process_boundary
           || (synchronization_boundary
               && process.status == ProcessStatus::running
               && !process.queued))
          && !scheduler.stop_requested()) {
        continue;
      }
      if (!debug_boundary || scheduler.stop_requested()) {
        return;
      }
    }
  }
}
} // namespace fsim::runtime::simir
