// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/packed_value.hpp"
#include "fsim/runtime/scheduler.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace fsim::runtime::simir {

using RegisterId = std::uint32_t;
using SignalId = std::uint32_t;
using ProcessId = std::uint32_t;
using InstructionIndex = std::uint32_t;

struct LoadConstant {
  RegisterId destination{};
  PackedLogic4 value;
};

struct ReadSignal {
  RegisterId destination{};
  SignalId signal{};
};

struct CopyRegister {
  RegisterId destination{};
  RegisterId source{};
};

struct UnaryNot {
  RegisterId destination{};
  RegisterId source{};
};

enum class BinaryOperator : std::uint8_t {
  bit_and,
  bit_or,
  bit_xor,
  add_unsigned,
  equal,
  case_equal,
};

struct Binary {
  BinaryOperator operation = BinaryOperator::bit_and;
  RegisterId destination{};
  RegisterId lhs{};
  RegisterId rhs{};
};

/// Commit a new value immediately in the active phase.
struct WriteBlocking {
  SignalId signal{};
  RegisterId source{};
};

/// Queue a new value for the current timestamp's update phase.
struct WriteUpdate {
  SignalId signal{};
  RegisterId source{};
};

/// Queue a new value for a future timestamp's update phase.
struct WriteAfter {
  SignalId signal{};
  RegisterId source{};
  SimulationTick delay{};
};

struct WaitFor {
  SimulationTick delay{};
};

enum class EdgeKind : std::uint8_t {
  any,
  posedge,
  negedge,
};

/// Suspend until a listed signal has its corresponding edge. An empty edge
/// list means any change for every signal.
struct WaitOn {
  WaitOn() = default;
  explicit WaitOn(std::vector<SignalId> waited_signals)
      : signals(std::move(waited_signals)) {}
  WaitOn(
      std::vector<SignalId> waited_signals,
      std::vector<EdgeKind> waited_edges)
      : signals(std::move(waited_signals)),
        edges(std::move(waited_edges)) {}

  std::vector<SignalId> signals;
  std::vector<EdgeKind> edges;
};

/// Suspend until this process's static sensitivity condition is met.
struct WaitSensitivity {};

/// Suspend and resume in the active phase of the next delta cycle.
struct Yield {};

struct Jump {
  InstructionIndex target{};
};

enum class UnknownBranchPolicy : std::uint8_t {
  error,
  when_false,
};

/// Branch on a scalar one; zero selects when_false. X/Z handling follows the
/// operation's explicit language policy.
struct Branch {
  RegisterId condition{};
  InstructionIndex when_true{};
  InstructionIndex when_false{};
  UnknownBranchPolicy unknown_policy{UnknownBranchPolicy::error};
};

enum class AssertionSeverity : std::uint8_t {
  note,
  warning,
  error,
  failure,
};

struct SourceLocation {
  std::string path;
  std::uint32_t line{1};
  std::uint32_t column{1};

  friend bool operator==(const SourceLocation&,
                         const SourceLocation&) = default;
};

enum class DebugPointKind : std::uint8_t {
  statement,
  wait,
  assertion,
  process_entry,
};

struct DebugPoint {
  DebugPointKind kind{DebugPointKind::statement};
  SourceLocation source;
};

struct Assert {
  RegisterId condition{};
  std::string message;
  AssertionSeverity severity{AssertionSeverity::error};
  SourceLocation source;
};

/// Stop the complete simulation, as requested by `$finish` or an equivalent
/// language construct.
struct Stop {};

struct Halt {};

using Operation =
    std::variant<LoadConstant, ReadSignal, CopyRegister, UnaryNot, Binary,
                 WriteBlocking, WriteUpdate, WriteAfter, WaitFor, WaitOn,
                 WaitSensitivity, Yield, Jump, Branch, DebugPoint, Assert,
                 Stop, Halt>;

struct Signal {
  std::string name;
  PackedLogic4 initial_value;
};

struct Sensitivity {
  SignalId signal{};
  EdgeKind edge = EdgeKind::any;

  bool operator==(const Sensitivity&) const = default;
};

struct DebugLocal {
  std::string name;
  std::string type_name;
  RegisterId register_id{};
  std::size_t width{};
  SourceLocation source;
};

struct Process {
  ProcessId id{};
  std::string name;
  std::size_t register_count{};
  std::vector<DebugLocal> debug_locals;
  std::vector<Sensitivity> static_sensitivity;
  std::vector<Operation> operations;
};

/// Narrow signal/update surface available to an alternate process executor.
///
/// The simulation kernel retains all scheduler, wait, fanout, force, and
/// lifecycle ownership. An executor may evaluate ordinary operations through
/// this interface, then must return at a validated SimIR boundary operation.
/// Executors must not retain a ProcessExecutionContext beyond the resume()
/// call that supplies it.
class ProcessExecutionContext {
public:
  virtual ~ProcessExecutionContext() = default;

  [[nodiscard]] virtual PackedLogic4 read_signal(SignalId signal) const = 0;
  virtual void write_blocking(SignalId signal, PackedLogic4 value) = 0;

  /// Allocation-free single-word access used by generated scalar/vector code.
  ///
  /// The default implementations preserve compatibility for alternate
  /// executors that only implement the object interface. The kernel overrides
  /// these methods to access its signal storage directly.
  [[nodiscard]] virtual Logic4Word
  read_signal_word(SignalId signal) const {
    return read_signal(signal).low_word();
  }
  virtual void write_blocking_word(
      SignalId signal, const Logic4Word value) {
    write_blocking(
        signal,
        PackedLogic4::from_aval_bval(
            value.width, value.aval, value.bval));
  }

  virtual void write_update(SignalId signal, PackedLogic4 value) = 0;
  virtual void write_update_word(
      SignalId signal, const Logic4Word value) {
    write_update(
        signal,
        PackedLogic4::from_aval_bval(
            value.width, value.aval, value.bval));
  }

  virtual void write_after(SignalId signal, PackedLogic4 value,
                           SimulationTick delay) = 0;
  virtual void write_after_word(
      SignalId signal, const Logic4Word value,
      SimulationTick delay) {
    write_after(
        signal,
        PackedLogic4::from_aval_bval(
            value.width, value.aval, value.bval),
        delay);
  }

  /// True when an embedding debugger currently requests source boundaries.
  [[nodiscard]] virtual bool execution_points_enabled() const noexcept {
    return false;
  }
};

/// Describes the boundary at which an alternate executor returned control.
///
/// `instruction` identifies a WaitFor, WaitOn, WaitSensitivity, Yield, Stop,
/// or Halt operation. `next_instruction` is the executor's persistent resume
/// PC and must be exactly the following operation for the current SimIR.
struct ProcessResumeResult {
  InstructionIndex instruction{};
  InstructionIndex next_instruction{};
};

enum class ExecutionPointKind : std::uint8_t {
  statement,
  wait,
  assertion,
  process_entry,
  process_suspend,
};

struct ExecutionPoint {
  ProcessId process{};
  InstructionIndex instruction{};
  ExecutionPointKind kind{ExecutionPointKind::statement};
  SourceLocation source;
};

class ProcessExecutor {
public:
  virtual ~ProcessExecutor() = default;

  /// Execute from start_instruction until the next SimIR kernel boundary.
  ///
  /// Implementations own their register/frame storage. Exceptions must be
  /// raised only in C++ after any generated plain-C call has returned.
  [[nodiscard]] virtual ProcessResumeResult
  resume(ProcessExecutionContext& context,
         InstructionIndex start_instruction) = 0;

  [[nodiscard]] virtual PackedLogic4
  read_register(RegisterId, std::size_t) const {
    throw std::logic_error{
        "alternate process executor does not expose register values"};
  }
};

class InterpreterError : public std::runtime_error {
public:
  InterpreterError(ProcessId process, InstructionIndex instruction,
                   std::string message);

  [[nodiscard]] ProcessId process() const noexcept { return process_; }
  [[nodiscard]] InstructionIndex instruction() const noexcept {
    return instruction_;
  }

private:
  ProcessId process_{};
  InstructionIndex instruction_{};
};

class AssertionError final : public InterpreterError {
public:
  AssertionError(ProcessId process, InstructionIndex instruction,
                 std::string message, AssertionSeverity severity,
                 SourceLocation source);

  [[nodiscard]] AssertionSeverity severity() const noexcept {
    return severity_;
  }
  [[nodiscard]] const SourceLocation& source() const noexcept {
    return source_;
  }

private:
  AssertionSeverity severity_{AssertionSeverity::error};
  SourceLocation source_;
};

/// Small reference interpreter for differential testing of generated code.
class Interpreter {
public:
  using SignalChangeHook =
      std::function<void(SignalId, const PackedLogic4 &, SimulationTick)>;
  using ExecutionPointHook =
      std::function<void(Scheduler&, const ExecutionPoint&)>;

  explicit Interpreter(SchedulerOptions options = {});
  ~Interpreter();
  Interpreter(Interpreter &&) noexcept;
  Interpreter &operator=(Interpreter &&) noexcept;
  Interpreter(const Interpreter &) = delete;
  Interpreter &operator=(const Interpreter &) = delete;

  [[nodiscard]] SignalId add_signal(Signal signal);
  [[nodiscard]] ProcessId add_process(Process process);

  /// Replace one process's reference evaluator with an alternate executor.
  ///
  /// The interpreter remains the sole scheduler and signal store. Installation
  /// is allowed only before start and at most once per process.
  void set_process_executor(
      ProcessId process, std::unique_ptr<ProcessExecutor> executor);

  void start();
  [[nodiscard]] RunResult
  run(std::optional<SimulationTick> until = std::nullopt);

  /// Deposit immediately and activate sensitive processes in the next delta.
  void deposit_signal(SignalId signal, PackedLogic4 value);

  /// Override the visible value while preserving subsequently driven values.
  /// Releasing the force publishes the most recent underlying driven value.
  void force_signal(SignalId signal, PackedLogic4 value);
  void release_signal(SignalId signal);
  [[nodiscard]] bool signal_is_forced(SignalId signal) const;

  /// Schedule an external drive in the update phase of an absolute timestamp.
  void schedule_signal_at(SignalId signal, PackedLogic4 value,
                          SimulationTick time, StableOrder order = 0);

  /// Schedule an external drive relative to the scheduler's current time.
  void schedule_signal_after(SignalId signal, PackedLogic4 value,
                             SimulationTick delay, StableOrder order = 0);

  [[nodiscard]] const PackedLogic4 &signal_value(SignalId signal) const;
  [[nodiscard]] PackedLogic4 read_debug_local(
      ProcessId process, std::size_t local_index) const;
  /// True once a language-level Stop operation (`$finish` or equivalent) has
  /// executed. External scheduler stop requests do not set this flag.
  [[nodiscard]] bool stopped_by_design() const noexcept;
  [[nodiscard]] Scheduler &scheduler() noexcept;
  [[nodiscard]] const Scheduler &scheduler() const noexcept;
  void set_signal_change_hook(SignalChangeHook hook);
  void set_execution_point_hook(ExecutionPointHook hook);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace fsim::runtime::simir
