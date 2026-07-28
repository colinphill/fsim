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

/// SystemVerilog logical negation. The source may be a packed vector; the
/// destination is a scalar four-state truth value.
struct LogicalNot {
  RegisterId destination{};
  RegisterId source{};
};

enum class LogicalBinaryOperator : std::uint8_t {
  logical_and,
  logical_or,
};

/// SystemVerilog logical conjunction/disjunction. Each operand is reduced to
/// a scalar truth value independently, so operand widths may differ.
struct LogicalBinary {
  LogicalBinaryOperator operation{
      LogicalBinaryOperator::logical_and};
  RegisterId destination{};
  RegisterId lhs{};
  RegisterId rhs{};
};

enum class ReductionOperator : std::uint8_t {
  bit_and,
  bit_or,
  bit_xor,
  one_hot,
  one_hot_or_zero,
};

/// SystemVerilog unary reduction over every bit of one packed operand.
struct Reduction {
  ReductionOperator operation{ReductionOperator::bit_and};
  RegisterId destination{};
  RegisterId source{};
};

/// Count exact `1` elements of one packed operand. `X` and `Z` do not
/// contribute. The destination is a 32-bit two-state SystemVerilog `int`.
struct CountOnes {
  RegisterId destination{};
  RegisterId source{};
};

/// Count elements whose exact four-state value is selected by state_mask.
/// Bits 0 through 3 select `0`, `1`, `X`, and `Z`, respectively.
struct CountBits {
  RegisterId destination{};
  RegisterId source{};
  std::uint8_t state_mask{};
};

enum class ShiftOperator : std::uint8_t {
  logical_left,
  logical_right,
  arithmetic_right,
  arithmetic_left,
  rotate_left,
  rotate_right,
};

/// Packed shift with independently sized value and shift-count operands.
struct Shift {
  ShiftOperator operation{ShiftOperator::logical_left};
  RegisterId destination{};
  RegisterId value{};
  RegisterId amount{};
};

/// Extract a contiguous normalized bit range from one packed value.
struct Extract {
  RegisterId destination{};
  RegisterId source{};
  std::uint32_t offset{};
  std::uint32_t width{1};
};

/// Replace a contiguous normalized range in a packed value.
struct Insert {
  RegisterId destination{};
  RegisterId target{};
  RegisterId source{};
  std::uint32_t offset{};
};

/// Concatenate packed operands in source order. The first operand occupies
/// the most-significant result bits.
struct Concatenate {
  RegisterId destination{};
  std::vector<RegisterId> operands;
  std::uint32_t width{};
};

enum class BinaryOperator : std::uint8_t {
  bit_and,
  bit_or,
  bit_xor,
  add_unsigned,
  subtract_unsigned,
  multiply_unsigned,
  power_unsigned,
  divide_unsigned,
  modulo_unsigned,
  add_signed,
  subtract_signed,
  multiply_signed,
  power_signed,
  divide_signed,
  remainder_signed,
  modulo_signed,
  equal,
  case_equal,
  // Statement-level wildcard matching. Both operands may supply wildcards;
  // the result is always a known scalar.
  casez_equal,
  casex_equal,
  // SystemVerilog ==? masks X/Z only in the right operand and may return X
  // for an unmasked X/Z in the left operand.
  wildcard_equal,
  not_equal,
  less_unsigned,
  less_equal_unsigned,
  greater_unsigned,
  greater_equal_unsigned,
  less_signed,
  less_equal_signed,
  greater_signed,
  greater_equal_signed,
};

struct Binary {
  BinaryOperator operation = BinaryOperator::bit_and;
  RegisterId destination{};
  RegisterId lhs{};
  RegisterId rhs{};
};

/// Select between equal-width values using SystemVerilog conditional
/// semantics. An X/Z condition merges matching bits and produces X for
/// differing bits.
struct ConditionalSelect {
  RegisterId destination{};
  RegisterId condition{};
  RegisterId when_true{};
  RegisterId when_false{};
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

/// Replace a contiguous packed range immediately in the active phase.
struct WriteBlockingSlice {
  SignalId signal{};
  RegisterId source{};
  std::uint32_t offset{};
};

/// Stage a contiguous packed range for the common update phase.
struct WriteUpdateSlice {
  SignalId signal{};
  RegisterId source{};
  std::uint32_t offset{};
};

/// Stage a contiguous packed range after a simulation-time delay.
struct WriteAfterSlice {
  SignalId signal{};
  RegisterId source{};
  std::uint32_t offset{};
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
  // An optional timeout races the listed signal events. A condition-wait
  // lowering uses timeout_result to distinguish timeout resumption from an
  // event resumption. A rearmed wait preserves the absolute deadline
  // established by the operation at timeout_origin instead of restarting the
  // timeout after a false condition.
  std::optional<SimulationTick> timeout;
  std::optional<RegisterId> timeout_result;
  std::optional<InstructionIndex> timeout_origin;
};

/// Suspend until this process's static sensitivity condition is met.
struct WaitSensitivity {};

/// Suspend permanently without completing the process. This models bare
/// waits and dependency-free condition waits while preserving
/// debugger-visible suspended state.
struct WaitForever {};

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
    std::variant<LoadConstant, ReadSignal, CopyRegister, UnaryNot, LogicalNot,
                 LogicalBinary, Reduction, CountOnes, CountBits, Shift,
                 Extract, Concatenate, Binary, Insert, ConditionalSelect,
                 WriteBlocking, WriteUpdate, WriteAfter, WriteBlockingSlice,
                 WriteUpdateSlice, WriteAfterSlice, WaitFor, WaitOn,
                 WaitSensitivity, WaitForever, Yield, Jump, Branch,
                 DebugPoint, Assert, Stop, Halt>;

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
  bool initialize{true};
};

/// Narrow signal/update surface available to an alternate process executor.
///
/// The simulation kernel retains all scheduler, wait, fanout, force, and
/// lifecycle ownership. An executor may evaluate ordinary operations through
/// this interface, then must return at a validated SimIR boundary operation.
/// Executors must not retain a ProcessExecutionContext beyond the resume()
/// call that supplies it.
enum class EventNotificationKind : std::uint8_t {
  immediate,
  delta,
  timed,
  delayed,
};

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
  virtual void write_blocking_slice(
      SignalId signal,
      PackedLogic4 value,
      std::size_t offset) = 0;
  virtual void write_blocking_slice_word(
      SignalId signal,
      const Logic4Word value,
      std::uint32_t offset) {
    write_blocking_slice(
        signal,
        PackedLogic4::from_aval_bval(
            value.width, value.aval, value.bval),
        offset);
  }

  virtual void write_update(SignalId signal, PackedLogic4 value) = 0;
  virtual void write_update_word(
      SignalId signal, const Logic4Word value) {
    write_update(
        signal,
        PackedLogic4::from_aval_bval(
            value.width, value.aval, value.bval));
  }
  virtual void write_update_slice(
      SignalId signal,
      PackedLogic4 value,
      std::size_t offset) = 0;
  virtual void write_update_slice_word(
      SignalId signal,
      const Logic4Word value,
      std::uint32_t offset) {
    write_update_slice(
        signal,
        PackedLogic4::from_aval_bval(
            value.width, value.aval, value.bval),
        offset);
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
  virtual void write_after_slice(
      SignalId signal,
      PackedLogic4 value,
      std::size_t offset,
      SimulationTick delay) = 0;
  virtual void write_after_slice_word(
      SignalId signal,
      const Logic4Word value,
      std::uint32_t offset,
      SimulationTick delay) {
    write_after_slice(
        signal,
        PackedLogic4::from_aval_bval(
            value.width, value.aval, value.bval),
        offset,
        delay);
  }

  /// Notify a kernel-owned event identity from an alternate language
  /// executor. Immediate notifications re-enter the active worklist at the
  /// current timestamp. Delta notifications enter the next delta; non-zero
  /// delays enter the active worklist at the requested future timestamp.
  virtual void notify_event(
      SignalId,
      SimulationTick,
      EventNotificationKind) {
    throw std::logic_error{
        "alternate process executor does not support event notification"};
  }
  virtual void cancel_event(SignalId) {
    throw std::logic_error{
        "alternate process executor does not support event cancellation"};
  }

  /// True only in the evaluation delta caused by the signal's most recent
  /// committed value change.
  [[nodiscard]] virtual bool signal_event(SignalId) const {
    return false;
  }

  /// Request one alternate-language primitive-channel update. `channel` is a
  /// stable executor-owned identity. The kernel deduplicates it until the
  /// corresponding update callback finishes and invokes that callback in the
  /// common update phase.
  virtual void request_channel_update(std::uint64_t) {
    throw std::logic_error{
        "alternate process executor does not support channel updates"};
  }

  /// True when an embedding debugger currently requests source boundaries.
  [[nodiscard]] virtual bool execution_points_enabled() const noexcept {
    return false;
  }
};

enum class ExternalSuspendKind : std::uint8_t {
  simir_boundary,
  wait_for,
  wait_on,
  wait_sensitivity,
  yield,
  halt,
};

/// A dynamic suspension selected by an alternate language executor.
///
/// This is deliberately expressed in common-kernel terms. The executor may
/// choose the boundary at run time (for example SystemC `next_trigger`) but
/// cannot schedule or own fanout itself.
struct ExternalSuspension {
  ExternalSuspendKind kind{ExternalSuspendKind::simir_boundary};
  SimulationTick delay{};
  std::vector<Sensitivity> sensitivity;
  bool wait_all{};
};

/// Describes the boundary at which an alternate executor returned control.
///
/// `instruction` identifies a WaitFor, WaitOn, WaitSensitivity, WaitForever,
/// Yield, Stop, or Halt operation. `next_instruction` is the executor's
/// persistent resume PC and must be exactly the following operation for the
/// current SimIR.
/// `external` overrides the placeholder SimIR boundary for an executor whose
/// suspension kind is selected dynamically.
struct ProcessResumeResult {
  ProcessResumeResult() = default;
  constexpr ProcessResumeResult(
      const InstructionIndex boundary_instruction,
      const InstructionIndex resume_instruction) noexcept
      : instruction(boundary_instruction),
        next_instruction(resume_instruction) {}

  InstructionIndex instruction{};
  InstructionIndex next_instruction{};
  ExternalSuspension external;
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

  /// Execute a previously requested primitive-channel update. Only
  /// alternate-language executors which expose such channels override this.
  virtual void update_channel(
      std::uint64_t,
      ProcessExecutionContext&) {
    throw std::logic_error{
        "alternate process executor has no primitive-channel callback"};
  }

  [[nodiscard]] virtual PackedLogic4
  read_register(RegisterId, std::size_t) const {
    throw std::logic_error{
        "alternate process executor does not expose register values"};
  }

  virtual void write_register(
      RegisterId, const PackedLogic4&) {
    throw std::logic_error{
        "alternate process executor does not expose writable registers"};
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
