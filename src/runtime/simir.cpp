// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/simir.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <set>
#include <sstream>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace fsim::runtime::simir {
namespace {

[[nodiscard]] std::string error_text(ProcessId process,
                                     InstructionIndex instruction,
                                     const std::string &message) {
  std::ostringstream result;
  result << "SimIR process " << process << ", instruction " << instruction
         << ": " << message;
  return result.str();
}

template <class... Ts> struct Overloaded : Ts... {
  using Ts::operator()...;
};
template <class... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;

[[nodiscard]] bool edge_matches(EdgeKind edge, Logic4 old_value,
                                Logic4 new_value) noexcept {
  if (old_value == new_value) {
    return false;
  }
  if (edge == EdgeKind::any) {
    return true;
  }
  if (edge == EdgeKind::posedge) {
    return (old_value == Logic4::zero &&
            (new_value == Logic4::one || new_value == Logic4::x ||
             new_value == Logic4::z)) ||
           ((old_value == Logic4::x || old_value == Logic4::z) &&
            new_value == Logic4::one);
  }
  return (old_value == Logic4::one &&
          (new_value == Logic4::zero || new_value == Logic4::x ||
           new_value == Logic4::z)) ||
         ((old_value == Logic4::x || old_value == Logic4::z) &&
          new_value == Logic4::zero);
}

[[nodiscard]] PackedLogic4 unary_not(const PackedLogic4 &source) {
  PackedLogic4 result(source.width(), Logic4::x);
  for (std::size_t index = 0; index < source.width(); ++index) {
    result.set(index, logic_not(source.get(index)));
  }
  return result;
}

[[nodiscard]] PackedLogic4 logical_not(const PackedLogic4& source) {
  bool has_unknown = false;
  for (std::size_t index = 0; index < source.width(); ++index) {
    const auto value = source.get(index);
    if (value == Logic4::one) {
      return PackedLogic4(1, Logic4::zero);
    }
    has_unknown =
        has_unknown || value == Logic4::x || value == Logic4::z;
  }
  return PackedLogic4(
      1, has_unknown ? Logic4::x : Logic4::one);
}

[[nodiscard]] PackedLogic4 binary_value(BinaryOperator operation,
                                        const PackedLogic4 &lhs,
                                        const PackedLogic4 &rhs) {
  if (lhs.width() != rhs.width()) {
    throw std::invalid_argument("binary operands have different widths");
  }
  if (operation == BinaryOperator::equal) {
    auto result = PackedLogic4(1, Logic4::one);
    for (std::size_t index = 0; index < lhs.width(); ++index) {
      const auto left = lhs.get(index);
      const auto right = rhs.get(index);
      const bool left_known = left == Logic4::zero || left == Logic4::one;
      const bool right_known = right == Logic4::zero || right == Logic4::one;
      if (!left_known || !right_known) {
        result.set(0, Logic4::x);
        return result;
      }
      if (left != right) {
        result.set(0, Logic4::zero);
        return result;
      }
    }
    return result;
  }
  if (operation == BinaryOperator::case_equal) {
    auto result = PackedLogic4(1, Logic4::one);
    for (std::size_t index = 0; index < lhs.width(); ++index) {
      if (lhs.get(index) != rhs.get(index)) {
        result.set(0, Logic4::zero);
        break;
      }
    }
    return result;
  }
  if (operation == BinaryOperator::not_equal
      || operation == BinaryOperator::less_unsigned
      || operation == BinaryOperator::less_equal_unsigned
      || operation == BinaryOperator::greater_unsigned
      || operation == BinaryOperator::greater_equal_unsigned) {
    for (std::size_t index = 0; index < lhs.width(); ++index) {
      const auto left = lhs.get(index);
      const auto right = rhs.get(index);
      if (left == Logic4::x || left == Logic4::z
          || right == Logic4::x || right == Logic4::z) {
        return PackedLogic4(1, Logic4::x);
      }
    }
    bool less = false;
    bool greater = false;
    for (std::size_t index = lhs.width(); index-- > 0;) {
      if (lhs.get(index) == rhs.get(index)) {
        continue;
      }
      less = lhs.get(index) == Logic4::zero;
      greater = !less;
      break;
    }
    bool result = false;
    switch (operation) {
    case BinaryOperator::not_equal:
      result = less || greater;
      break;
    case BinaryOperator::less_unsigned:
      result = less;
      break;
    case BinaryOperator::less_equal_unsigned:
      result = less || !greater;
      break;
    case BinaryOperator::greater_unsigned:
      result = greater;
      break;
    case BinaryOperator::greater_equal_unsigned:
      result = greater || !less;
      break;
    case BinaryOperator::bit_and:
    case BinaryOperator::bit_or:
    case BinaryOperator::bit_xor:
    case BinaryOperator::add_unsigned:
    case BinaryOperator::equal:
    case BinaryOperator::case_equal:
      break;
    }
    return PackedLogic4(
        1, result ? Logic4::one : Logic4::zero);
  }

  PackedLogic4 result(lhs.width(), Logic4::zero);
  if (operation == BinaryOperator::add_unsigned) {
    auto carry = Logic4::zero;
    for (std::size_t index = 0; index < lhs.width(); ++index) {
      const auto left = lhs.get(index);
      const auto right = rhs.get(index);
      result.set(index,
                 logic_xor(logic_xor(left, right), carry));
      carry = logic_or(logic_and(left, right),
                       logic_and(carry, logic_or(left, right)));
    }
    return result;
  }

  for (std::size_t index = 0; index < lhs.width(); ++index) {
    switch (operation) {
    case BinaryOperator::bit_and:
      result.set(index, logic_and(lhs.get(index), rhs.get(index)));
      break;
    case BinaryOperator::bit_or:
      result.set(index, logic_or(lhs.get(index), rhs.get(index)));
      break;
    case BinaryOperator::bit_xor:
      result.set(index, logic_xor(lhs.get(index), rhs.get(index)));
      break;
    case BinaryOperator::add_unsigned:
    case BinaryOperator::equal:
    case BinaryOperator::case_equal:
    case BinaryOperator::not_equal:
    case BinaryOperator::less_unsigned:
    case BinaryOperator::less_equal_unsigned:
    case BinaryOperator::greater_unsigned:
    case BinaryOperator::greater_equal_unsigned:
      break;
    }
  }
  return result;
}

[[nodiscard]] PackedLogic4 conditional_value(
    const PackedLogic4& condition,
    const PackedLogic4& when_true,
    const PackedLogic4& when_false) {
  if (condition.width() != 1) {
    throw std::invalid_argument(
        "conditional-select condition is not scalar");
  }
  if (when_true.width() != when_false.width()) {
    throw std::invalid_argument(
        "conditional-select values have different widths");
  }
  if (condition.get(0) == Logic4::one) {
    return when_true;
  }
  if (condition.get(0) == Logic4::zero) {
    return when_false;
  }
  PackedLogic4 result(when_true.width(), Logic4::x);
  for (std::size_t index = 0; index < result.width(); ++index) {
    if (when_true.get(index) == when_false.get(index)) {
      result.set(index, when_true.get(index));
    }
  }
  return result;
}

} // namespace

InterpreterError::InterpreterError(ProcessId process,
                                   InstructionIndex instruction,
                                   std::string message)
    : std::runtime_error(error_text(process, instruction, message)),
      process_(process), instruction_(instruction) {}

AssertionError::AssertionError(ProcessId process,
                               InstructionIndex instruction,
                               std::string message,
                               AssertionSeverity severity,
                               SourceLocation source)
    : InterpreterError(
          process, instruction,
          message.empty() ? "assertion failed" : std::move(message)),
      severity_(severity), source_(std::move(source)) {}

struct Interpreter::Impl {
  struct ProcessState {
    Process program;
    InstructionIndex pc{};
    std::vector<PackedLogic4> registers;
    std::unique_ptr<ProcessExecutor> executor;
    std::vector<Sensitivity> dynamic_sensitivity;
    SourceLocation current_source;
    bool queued{};
    bool waiting_on_static{};
    bool waiting_on_signal{};
    bool halted{};
  };

  struct Fanout {
    ProcessId process{};
    EdgeKind edge = EdgeKind::any;
  };

  explicit Impl(SchedulerOptions options) : scheduler(options) {}

  Scheduler scheduler;
  std::vector<Signal> signals;
  std::vector<PackedLogic4> driven_values;
  std::vector<std::optional<PackedLogic4>> forced_values;
  std::vector<ProcessState> processes;
  std::vector<std::vector<Fanout>> static_fanout;
  std::vector<std::vector<Fanout>> dynamic_fanout;
  std::unordered_map<SignalId, PackedLogic4> pending_updates;
  SignalChangeHook signal_change_hook;
  ExecutionPointHook execution_point_hook;
  bool update_commit_scheduled{};
  bool started{};
  bool stopped_by_design{};

  [[nodiscard]] Signal &get_signal(SignalId id) {
    if (id >= signals.size()) {
      throw std::out_of_range("invalid SimIR signal ID");
    }
    return signals[id];
  }

  [[nodiscard]] const Signal &get_signal(SignalId id) const {
    if (id >= signals.size()) {
      throw std::out_of_range("invalid SimIR signal ID");
    }
    return signals[id];
  }

  [[nodiscard]] ProcessState &get_process(ProcessId id) {
    if (id >= processes.size()) {
      throw std::out_of_range("invalid SimIR process ID");
    }
    return processes[id];
  }

  [[nodiscard]] PackedLogic4 &get_register(ProcessState &process,
                                           RegisterId id) {
    if (id >= process.registers.size()) {
      throw InterpreterError(process.program.id, process.pc,
                             "invalid register ID");
    }
    return process.registers[id];
  }

  void remove_dynamic_wait(ProcessState &process) {
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
    process.waiting_on_signal = false;
  }

  void handle_boundary(ProcessState& process,
                       InstructionIndex instruction,
                       InstructionIndex next_instruction);
  void execute(ProcessId id);

  void queue_at(ProcessId id, SimulationTick time) {
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

  void queue_next_delta(ProcessId id) {
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

  void queue_current(ProcessId id) {
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

  void notify_execution_point(
      ProcessState& process,
      const InstructionIndex instruction,
      const ExecutionPointKind kind,
      const SourceLocation& source) {
    if (execution_point_hook) {
      execution_point_hook(
          scheduler,
          ExecutionPoint{
              process.program.id, instruction, kind, source});
    }
  }

  void publish(SignalId signal_id, PackedLogic4 value) {
    auto &signal = get_signal(signal_id);
    if (signal.initial_value.width() != value.width()) {
      throw std::invalid_argument("SimIR signal assignment width mismatch");
    }
    if (signal.initial_value == value) {
      return;
    }
    const auto old_value = signal.initial_value;
    signal.initial_value = std::move(value);
    scheduler.note_signal_change(signal_id);
    if (signal_change_hook) {
      signal_change_hook(signal_id, signal.initial_value, scheduler.now());
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
      queue_next_delta(sensitivity.process);
    }
  }

  void commit(SignalId signal_id, PackedLogic4 value) {
    (void)get_signal(signal_id);
    if (driven_values[signal_id].width() != value.width()) {
      throw std::invalid_argument("SimIR signal assignment width mismatch");
    }
    driven_values[signal_id] = value;
    if (!forced_values[signal_id].has_value()) {
      publish(signal_id, std::move(value));
    }
  }

  void stage_update(SignalId signal_id, PackedLogic4 staged_value) {
    (void)get_signal(signal_id);
    if (driven_values[signal_id].width() != staged_value.width()) {
      throw std::invalid_argument("SimIR signal assignment width mismatch");
    }
    pending_updates.insert_or_assign(signal_id, std::move(staged_value));
    if (update_commit_scheduled) {
      return;
    }
    update_commit_scheduled = true;
    scheduler.schedule(
        SchedulerPhase::update,
        std::numeric_limits<StableOrder>::max(),
        [this](Scheduler&) {
          std::vector<std::pair<SignalId, PackedLogic4>> updates;
          updates.reserve(pending_updates.size());
          for (auto& [signal, value] : pending_updates) {
            updates.emplace_back(signal, std::move(value));
          }
          pending_updates.clear();
          update_commit_scheduled = false;
          std::sort(
              updates.begin(),
              updates.end(),
              [](const auto& lhs, const auto& rhs) {
                return lhs.first < rhs.first;
              });
          for (auto& [signal, value] : updates) {
            commit(signal, std::move(value));
          }
        });
  }

  [[noreturn]] void fail(const ProcessState &process,
                         const std::string &message) const {
    throw InterpreterError(process.program.id, process.pc, message);
  }
};

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
  if (const auto* point = std::get_if<DebugPoint>(&operation)) {
    process.current_source = point->source;
    auto kind = ExecutionPointKind::statement;
    switch (point->kind) {
    case DebugPointKind::statement:
      kind = ExecutionPointKind::statement;
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
        process, instruction, kind, process.current_source);
    if (scheduler.stop_requested()) {
      queue_current(process.program.id);
    }
    return;
  }
  if (const auto* wait = std::get_if<WaitFor>(&operation)) {
    if (wait->delay == 0) {
      process.queued = true;
      scheduler.schedule(
          SchedulerPhase::inactive,
          process.program.id,
          [this, id = process.program.id](Scheduler&) {
            auto& state = get_process(id);
            state.queued = false;
            execute(id);
          });
      notify_execution_point(
          process, instruction, ExecutionPointKind::process_suspend,
          process.current_source);
      return;
    }
    if (wait->delay
        > std::numeric_limits<SimulationTick>::max() - scheduler.now()) {
      process.pc = instruction;
      fail(process, "simulation time overflow in WaitFor");
    }
    queue_at(process.program.id, scheduler.now() + wait->delay);
    notify_execution_point(
        process, instruction, ExecutionPointKind::process_suspend,
        process.current_source);
    return;
  }
  if (const auto* wait = std::get_if<WaitOn>(&operation)) {
    if (wait->signals.empty()) {
      process.pc = instruction;
      fail(process, "WaitOn requires at least one signal");
    }
    if (!wait->edges.empty()
        && wait->edges.size() != wait->signals.size()) {
      process.pc = instruction;
      fail(process, "WaitOn edge count must match its signal count");
    }
    process.waiting_on_signal = true;
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
    notify_execution_point(
        process, instruction, ExecutionPointKind::process_suspend,
        process.current_source);
    return;
  }
  if (std::holds_alternative<WaitSensitivity>(operation)) {
    if (process.program.static_sensitivity.empty()) {
      process.pc = instruction;
      fail(process, "WaitSensitivity requires a static sensitivity list");
    }
    process.waiting_on_static = true;
    notify_execution_point(
        process, instruction, ExecutionPointKind::process_suspend,
        process.current_source);
    return;
  }
  if (std::holds_alternative<Yield>(operation)) {
    queue_next_delta(process.program.id);
    notify_execution_point(
        process, instruction, ExecutionPointKind::process_suspend,
        process.current_source);
    return;
  }
  if (std::holds_alternative<Stop>(operation)) {
    process.halted = true;
    stopped_by_design = true;
    scheduler.request_stop();
    notify_execution_point(
        process, instruction, ExecutionPointKind::process_suspend,
        process.current_source);
    return;
  }
  if (std::holds_alternative<Halt>(operation)) {
    process.halted = true;
    notify_execution_point(
        process, instruction, ExecutionPointKind::process_suspend,
        process.current_source);
    return;
  }

  process.pc = instruction;
  fail(
      process,
      "executor returned at an operation that is not a kernel boundary");
}

void Interpreter::Impl::execute(ProcessId id) {
  auto &process = get_process(id);
  if (process.executor) {
    struct ExecutionContext final : ProcessExecutionContext {
      Impl& owner;
      ProcessId process;

      ExecutionContext(Impl& owner_value, const ProcessId process_value)
          : owner(owner_value), process(process_value) {}

      [[nodiscard]] PackedLogic4
      read_signal(const SignalId signal) const override {
        return owner.get_signal(signal).initial_value;
      }

      [[nodiscard]] Logic4Word
      read_signal_word(const SignalId signal) const override {
        return owner.get_signal(signal).initial_value.low_word();
      }

      void write_blocking(
          const SignalId signal, PackedLogic4 value) override {
        owner.commit(signal, std::move(value));
      }

      void write_blocking_word(
          const SignalId signal,
          const Logic4Word value) override {
        owner.commit(
            signal,
            PackedLogic4::from_aval_bval(
                value.width, value.aval, value.bval));
      }

      void write_update(
          const SignalId signal, PackedLogic4 value) override {
        owner.stage_update(signal, std::move(value));
      }

      void write_update_word(
          const SignalId signal,
          const Logic4Word value) override {
        owner.stage_update(
            signal,
            PackedLogic4::from_aval_bval(
                value.width, value.aval, value.bval));
      }

      void write_after(
          const SignalId signal,
          PackedLogic4 value,
          const SimulationTick delay) override {
        owner.scheduler.schedule_after(
            delay,
            SchedulerPhase::update,
            process,
            [&owner = owner, signal, value = std::move(value)](
                Scheduler&) mutable {
              owner.stage_update(signal, std::move(value));
            });
      }

      void write_after_word(
          const SignalId signal,
          const Logic4Word value,
          const SimulationTick delay) override {
        auto packed = PackedLogic4::from_aval_bval(
            value.width, value.aval, value.bval);
        owner.scheduler.schedule_after(
            delay,
            SchedulerPhase::update,
            process,
            [&owner = owner, signal, value = std::move(packed)](
                Scheduler&) mutable {
              owner.stage_update(signal, std::move(value));
            });
      }

      [[nodiscard]] bool
      execution_points_enabled() const noexcept override {
        return static_cast<bool>(owner.execution_point_hook);
      }
    };

    ExecutionContext context{*this, id};
    while (!process.halted) {
      const auto boundary = process.executor->resume(context, process.pc);
      handle_boundary(
          process, boundary.instruction, boundary.next_instruction);
      if (!std::holds_alternative<DebugPoint>(
              process.program.operations[boundary.instruction])
          || scheduler.stop_requested()) {
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
    std::visit(
        Overloaded{
            [&](const LoadConstant &op) {
              get_register(process, op.destination) = op.value;
              ++process.pc;
            },
            [&](const ReadSignal &op) {
              get_register(process, op.destination) =
                  get_signal(op.signal).initial_value;
              ++process.pc;
            },
            [&](const CopyRegister& op) {
              get_register(process, op.destination) =
                  get_register(process, op.source);
              ++process.pc;
            },
            [&](const UnaryNot &op) {
              get_register(process, op.destination) =
                  unary_not(get_register(process, op.source));
              ++process.pc;
            },
            [&](const LogicalNot& op) {
              get_register(process, op.destination) =
                  logical_not(get_register(process, op.source));
              ++process.pc;
            },
            [&](const Binary &op) {
              try {
                get_register(process, op.destination) =
                    binary_value(op.operation, get_register(process, op.lhs),
                                 get_register(process, op.rhs));
              } catch (const std::invalid_argument &error) {
                fail(process, error.what());
              }
              ++process.pc;
            },
            [&](const ConditionalSelect& op) {
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
            },
            [&](const WriteBlocking &op) {
              auto value = get_register(process, op.source);
              ++process.pc;
              commit(op.signal, std::move(value));
            },
            [&](const WriteUpdate &op) {
              auto value = get_register(process, op.source);
              ++process.pc;
              stage_update(op.signal, std::move(value));
            },
            [&](const WriteAfter &op) {
              auto value = get_register(process, op.source);
              ++process.pc;
              scheduler.schedule_after(
                  op.delay, SchedulerPhase::update, process.program.id,
                  [this, signal = op.signal,
                   value = std::move(value)](Scheduler &) mutable {
                    stage_update(signal, std::move(value));
                  });
            },
            [&](const WaitFor &op) {
              (void)op;
              boundary = true;
            },
            [&](const WaitOn &op) {
              (void)op;
              boundary = true;
            },
            [&](const WaitSensitivity &) {
              boundary = true;
            },
            [&](const Yield &) {
              boundary = true;
            },
            [&](const Jump &op) {
              if (op.target >= process.program.operations.size()) {
                fail(process, "jump target is outside the operation stream");
              }
              process.pc = op.target;
            },
            [&](const Branch &op) {
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
            },
            [&](const DebugPoint&) {
              boundary = true;
            },
            [&](const Assert &op) {
              const auto &condition = get_register(process, op.condition);
              if (condition.width() != 1 ||
                  condition.get(0) != Logic4::one) {
                throw AssertionError(
                    process.program.id, process.pc, op.message, op.severity,
                    op.source);
              }
              ++process.pc;
            },
            [&](const Stop &) {
              boundary = true;
            },
            [&](const Halt &) {
              boundary = true;
            }},
        operation);

    if (boundary) {
      handle_boundary(process, instruction, instruction + 1);
      if (!std::holds_alternative<DebugPoint>(operation)
          || scheduler.stop_requested()) {
        return;
      }
    }
  }
}

Interpreter::Interpreter(SchedulerOptions options)
    : impl_(std::make_unique<Impl>(options)) {}
Interpreter::~Interpreter() = default;
Interpreter::Interpreter(Interpreter &&) noexcept = default;
Interpreter &Interpreter::operator=(Interpreter &&) noexcept = default;

SignalId Interpreter::add_signal(Signal signal) {
  if (impl_->started) {
    throw std::logic_error("cannot add a SimIR signal after start");
  }
  const auto id = static_cast<SignalId>(impl_->signals.size());
  if (static_cast<std::size_t>(id) != impl_->signals.size()) {
    throw std::length_error("too many SimIR signals");
  }
  impl_->driven_values.push_back(signal.initial_value);
  impl_->forced_values.emplace_back();
  impl_->signals.push_back(std::move(signal));
  impl_->static_fanout.emplace_back();
  impl_->dynamic_fanout.emplace_back();
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

  Impl::ProcessState state;
  state.registers.assign(process.register_count, PackedLogic4{});
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
    impl_->queue_at(id, impl_->scheduler.now());
  }
}

RunResult Interpreter::run(std::optional<SimulationTick> until) {
  start();
  return impl_->scheduler.run(until);
}

void Interpreter::deposit_signal(SignalId signal, PackedLogic4 value) {
  impl_->commit(signal, std::move(value));
}

void Interpreter::force_signal(SignalId signal, PackedLogic4 value) {
  if (impl_->get_signal(signal).initial_value.width() != value.width()) {
    throw std::invalid_argument("SimIR signal force width mismatch");
  }
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

} // namespace fsim::runtime::simir
