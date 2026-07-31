// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "fsim/runtime/simir.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace fsim::runtime::simir {


[[nodiscard]] std::string error_text(ProcessId process,
                                     InstructionIndex instruction,
                                     const std::string &message);

[[nodiscard]] std::optional<SignalId> output_signal(
    const Operation& operation);

[[nodiscard]] std::string format_output_value(
    const PackedLogic4& value,
    const OutputFormat format,
    const bool signed_decimal,
    const bool suppress_leading_zero);

[[nodiscard]] std::string make_formatted_output(
    const std::string_view prefix,
    const std::string_view suffix,
    const OutputFormat format,
    const PackedLogic4& value,
    const bool signed_decimal,
    const bool suppress_leading_zero,
    const std::uint32_t minimum_width,
    const bool left_justify,
    const bool zero_pad);

[[nodiscard]] std::string make_time_output(
    const std::string_view prefix,
    const std::string_view suffix,
    const SimulationTick tick,
    const std::uint32_t minimum_width,
    const bool left_justify,
    const bool zero_pad);

[[nodiscard]] bool edge_matches(EdgeKind edge, Logic4 old_value,
                                Logic4 new_value) noexcept;

[[nodiscard]] PackedLogic4 unary_not(const PackedLogic4 &source);

[[nodiscard]] Logic4 truth_value(const PackedLogic4& source);

[[nodiscard]] PackedLogic4 logical_not(const PackedLogic4& source);

[[nodiscard]] PackedLogic4 logical_binary(
    const LogicalBinaryOperator operation,
    const PackedLogic4& lhs,
    const PackedLogic4& rhs);

[[nodiscard]] PackedLogic4 reduce_value(
    const ReductionOperator operation,
    const PackedLogic4& source);

[[nodiscard]] PackedLogic4 count_ones_value(
    const PackedLogic4& source);

[[nodiscard]] PackedLogic4 count_bits_value(
    const PackedLogic4& source,
    const std::uint8_t state_mask);

[[nodiscard]] constexpr ShiftOperator reverse_shift(
    const ShiftOperator operation) noexcept;

[[nodiscard]] PackedLogic4 shift_value(
    ShiftOperator operation,
    const PackedLogic4& value,
    const PackedLogic4& amount_value,
    const bool signed_amount);

[[nodiscard]] PackedLogic4 extract_value(
    const PackedLogic4& source,
    const std::size_t offset,
    const std::size_t width);

[[nodiscard]] PackedLogic4 insert_value(
    PackedLogic4 target,
    const PackedLogic4& source,
    const std::size_t offset);

[[nodiscard]] std::uint32_t dynamic_index_offset(
    const PackedLogic4& index,
    const DynamicIndex& selection);

[[nodiscard]] PackedLogic4 concatenate_values(
    const std::vector<PackedLogic4>& operands,
    const std::size_t expected_width);

[[nodiscard]] bool has_unknown(const PackedLogic4& value);

[[nodiscard]] bool is_zero(const PackedLogic4& value);

[[nodiscard]] bool is_one(const PackedLogic4& value);

[[nodiscard]] bool is_all_ones(const PackedLogic4& value);

[[nodiscard]] int compare_known_unsigned(
    const PackedLogic4& lhs,
    const PackedLogic4& rhs);

[[nodiscard]] PackedLogic4 subtract_known(
    const PackedLogic4& lhs,
    const PackedLogic4& rhs);

[[nodiscard]] PackedLogic4 add_known(
    const PackedLogic4& lhs,
    const PackedLogic4& rhs);

[[nodiscard]] PackedLogic4 negate_known(
    const PackedLogic4& value);

[[nodiscard]] PackedLogic4 multiply_known(
    const PackedLogic4& lhs,
    const PackedLogic4& rhs);

[[nodiscard]] PackedLogic4 power_known(
    const PackedLogic4& base,
    const PackedLogic4& exponent,
    const bool signed_exponent);

[[nodiscard]] PackedLogic4 divide_known(
    const PackedLogic4& dividend,
    const PackedLogic4& divisor,
    const bool return_remainder);

struct SignedDivision {
  PackedLogic4 quotient;
  PackedLogic4 remainder;
};

[[nodiscard]] SignedDivision divide_known_signed(
    const PackedLogic4& dividend,
    const PackedLogic4& divisor);

[[nodiscard]] PackedLogic4 binary_value(BinaryOperator operation,
                                        const PackedLogic4 &lhs,
                                        const PackedLogic4 &rhs);

[[nodiscard]] std::int32_t checked_integer_operand(
    const PackedLogic4& value);

[[nodiscard]] PackedLogic4 packed_integer(const std::int64_t value);

[[nodiscard]] PackedLogic4 integer_unary_value(
    const IntegerUnaryOperator operation,
    const PackedLogic4& source);

[[nodiscard]] PackedLogic4 integer_binary_value(
    const IntegerBinaryOperator operation,
    const PackedLogic4& lhs_value,
    const PackedLogic4& rhs_value);

void check_integer_range(
    const PackedLogic4& source,
    const std::int32_t lower,
    const std::int32_t upper);

[[nodiscard]] PackedLogic4 conditional_value(
    const PackedLogic4& condition,
    const PackedLogic4& when_true,
    const PackedLogic4& when_false);

void validate_container_value(const ContainerValue& value);
[[nodiscard]] PackedLogic4 default_container_element(
    const ContainerType& type);

struct Interpreter::Impl {
  struct ExecutionContext;

  struct ProcessState {
    Process program;
    InstructionIndex pc{};
    std::vector<PackedLogic4> registers;
    std::vector<std::string> string_registers;
    std::vector<ContainerValue> container_registers;
    std::unique_ptr<ProcessExecutor> executor;
    std::vector<Sensitivity> dynamic_sensitivity;
    std::vector<bool> dynamic_triggered;
    SourceLocation current_source;
    bool queued{};
    bool waiting_on_static{};
    bool waiting_on_signal{};
    bool dynamic_wait_all{};
    std::optional<InstructionIndex> wait_timeout_origin;
    std::optional<SimulationTick> wait_timeout_deadline;
    std::optional<RegisterId> wait_timeout_result;
    std::uint64_t wait_timeout_generation{};
    std::uint64_t random_state{};
    bool halted{};
  };

  struct Fanout {
    ProcessId process{};
    EdgeKind edge = EdgeKind::any;
  };

  struct PendingUpdate {
    SignalId signal{};
    std::optional<ProcessId> driver;
    std::optional<std::size_t> offset;
    PackedLogic4 value;
  };

  enum class PendingEventKind : std::uint8_t {
    none,
    delta,
    timed,
  };

  struct EventState {
    PendingEventKind kind{PendingEventKind::none};
    SimulationTick due{};
    std::uint64_t generation{};
  };

  struct InertialDriverKey {
    ProcessId process{};
    SignalId signal{};
    std::uint32_t offset{};
    std::uint32_t width{};

    friend bool operator==(
        const InertialDriverKey&,
        const InertialDriverKey&) = default;
  };

  struct InertialDriverKeyHash {
    [[nodiscard]] std::size_t operator()(
        const InertialDriverKey& key) const noexcept;
  };

  struct PendingInertialWrite {
    ScheduledTaskHandle handle;
    PackedLogic4 source_value;
  };

  struct ProjectedDriverKey {
    ProcessId process{};
    SignalId signal{};
    std::uint32_t offset{};

    friend bool operator==(
        const ProjectedDriverKey&,
        const ProjectedDriverKey&) = default;
  };

  struct ProjectedDriverKeyHash {
    [[nodiscard]] std::size_t operator()(
        const ProjectedDriverKey& key) const noexcept;
  };

  struct ProjectedTransaction {
    std::uint64_t id{};
    SimulationTick time{};
    PackedLogic4 value;
    ScheduledTaskHandle handle;
  };

  struct ProjectedDriverState {
    std::vector<ProjectedTransaction> transactions;
  };

  struct FileState {
    ProcessId owner{};
    std::filesystem::path path;
    std::string mode;
    std::unique_ptr<std::fstream> stream;
    std::string last_error;
    bool closed{};
    bool readable{};
    bool writable{};
  };

  explicit Impl(
      SchedulerOptions options,
      const std::uint64_t seed);

  Scheduler scheduler;
  std::uint64_t root_seed{1};
  std::vector<Signal> signals;
  std::vector<StringObject> string_objects;
  std::vector<ContainerObject> container_objects;
  std::filesystem::path file_root;
  std::map<FileHandle, FileState> files;
  FileHandle next_file_handle{1};
  std::vector<PackedLogic4> driven_values;
  std::vector<std::map<ProcessId, PackedLogic4>> driver_values;
  std::vector<std::optional<PackedLogic4>> external_driver_values;
  std::vector<PackedLogic4> signal_last_values;
  std::vector<std::optional<PackedLogic4>> forced_values;
  std::vector<ProcessState> processes;
  std::vector<std::vector<Fanout>> static_fanout;
  std::vector<std::vector<Fanout>> dynamic_fanout;
  std::vector<EventState> event_states;
  std::vector<std::optional<std::pair<
      SimulationTick, std::uint64_t>>> signal_events;
  std::vector<std::optional<std::pair<
      SimulationTick, std::uint64_t>>> signal_transactions;
  std::vector<PendingUpdate> pending_updates;
  std::unordered_set<std::uint64_t> pending_channel_updates;
  std::unordered_map<
      InertialDriverKey,
      PendingInertialWrite,
      InertialDriverKeyHash> pending_inertial_writes;
  std::unordered_map<
      ProjectedDriverKey,
      ProjectedDriverState,
      ProjectedDriverKeyHash> projected_drivers;
  std::uint64_t next_projected_transaction_id{1};
  SignalChangeHook signal_change_hook;
  ExecutionPointHook execution_point_hook;
  OutputHook output_hook;
  ReportHook report_hook;
  std::optional<MonitorInstall> monitor;
  ProcessId monitor_process{};
  bool monitor_enabled{true};
  std::uint64_t monitor_generation{};
  std::optional<std::pair<SimulationTick, std::uint64_t>>
      monitor_publication;
  bool update_commit_scheduled{};
  bool started{};
  bool stopped_by_design{};
  bool finals_ran{};

  [[nodiscard]] Signal &get_signal(SignalId id);

  [[nodiscard]] const Signal &get_signal(SignalId id) const;

  [[nodiscard]] ProcessState &get_process(ProcessId id);

  [[nodiscard]] PackedLogic4 &get_register(ProcessState &process,
                                           RegisterId id);

  [[nodiscard]] std::string& get_string_register(
      ProcessState& process,
      StringRegisterId id);

  [[nodiscard]] StringObject& get_string_object(StringObjectId id);

  [[nodiscard]] const StringObject&
  get_string_object(StringObjectId id) const;

  [[nodiscard]] ContainerValue& get_container_register(
      ProcessState& process, ContainerRegisterId id);
  [[nodiscard]] ContainerObject& get_container_object(
      ContainerObjectId id);
  [[nodiscard]] const ContainerObject& get_container_object(
      ContainerObjectId id) const;
  [[nodiscard]] const ContainerValue&
  read_container_object_value(ContainerObjectId id);
  void write_container_object_value(
      ContainerObjectId id, const ContainerValue& value);

  void set_file_root(std::filesystem::path root);
  [[nodiscard]] FileHandle open_file(
      ProcessId process,
      std::string_view path,
      std::string_view mode);
  [[nodiscard]] FileState& checked_file(
      ProcessId process, FileHandle handle);
  void close_file(ProcessId process, FileHandle handle);
  void write_file(
      ProcessId process,
      FileHandle handle,
      std::string_view text,
      bool newline);
  [[nodiscard]] std::string read_file_line(
      ProcessId process,
      FileHandle handle,
      std::uint32_t& count);
  [[nodiscard]] bool file_end_of_file(
      ProcessId process, FileHandle handle);
  [[nodiscard]] std::string file_error(
      ProcessId process, FileHandle handle, bool& has_error);
  [[nodiscard]] FileHandle known_file_handle(
      ProcessState&, RegisterId);
  void execute_file(ProcessState&, const FileOpen&);
  void execute_file(ProcessState&, const FileClose&);
  void execute_file(ProcessState&, const FileWriteLiteral&);
  void execute_file(ProcessState&, const FileWriteFormatted&);
  void execute_file(ProcessState&, const FileWriteString&);
  void execute_file(ProcessState&, const FileReadLine&);
  void execute_file(ProcessState&, const FileEndOfFile&);
  void execute_file(ProcessState&, const FileErrorStatus&);

  void execute_container(ProcessState&, const ResizeContainer&);
  void execute_container(ProcessState&, const CopyContainerRegister&);
  void execute_container(ProcessState&, const ReadContainerObject&);
  void execute_container(ProcessState&, const WriteContainerObject&);
  void execute_container(ProcessState&, const ContainerSize&);
  void execute_container(ProcessState&, const ContainerReduction&);
  void execute_container(ProcessState&, const OrderContainer&);
  void execute_container(ProcessState&, const LocateContainer&);
  void execute_container(ProcessState&, const ContainerRead&);
  void execute_container(ProcessState&, const ContainerWrite&);
  void execute_container(ProcessState&, const DeleteContainer&);
  void execute_container(ProcessState&, const ContainerExists&);
  void execute_container(ProcessState&, const TraverseContainer&);
  void execute_container(ProcessState&, const LoadMemory&);
  void execute_container(ProcessState&, const PushContainer&);
  void execute_container(ProcessState&, const PopContainer&);

  [[nodiscard]] static ValueKind register_value_kind(
      const ProcessState& process,
      const RegisterId id);

  [[nodiscard]] static PackedLogic4 coerce_value_kind(
      PackedLogic4 value,
      const ValueKind kind);

  [[nodiscard]] PackedLogic4 normalize_signal_value(
      const SignalId signal,
      PackedLogic4 value) const;

  void remove_dynamic_wait(ProcessState &process);

  void write_process_register(
      ProcessState& process,
      const RegisterId destination,
      const PackedLogic4& value);

  void clear_wait_timeout(ProcessState& process);

  void set_wait_timeout_result(
      ProcessState& process,
      const bool timed_out);

  void begin_wait_timeout(
      ProcessState& process,
      const InstructionIndex origin,
      const SimulationTick delay,
      const std::optional<RegisterId> result);

  void rearm_wait_timeout(
      ProcessState& process,
      const InstructionIndex instruction,
      const InstructionIndex origin,
      const std::optional<RegisterId> result);

  void mark_dynamic_event_resume(
      ProcessState& process);

  [[nodiscard]] bool dynamic_wait_satisfied(
      ProcessState& process,
      const SignalId signal,
      const EdgeKind edge);

  void handle_boundary(ProcessState& process,
                       InstructionIndex instruction,
                       InstructionIndex next_instruction);
  void handle_external_boundary(
      ProcessState& process,
      InstructionIndex instruction,
      InstructionIndex next_instruction,
      const ExternalSuspension& suspension);
  void request_channel_update(
      ProcessId process, std::uint64_t channel);
  void execute(ProcessId id);

  void queue_at(ProcessId id, SimulationTick time);

  void queue_next_delta(ProcessId id);

  void queue_current(ProcessId id);

  void queue_active_current(ProcessId id);

  void trigger_event(const SignalId event);

  [[nodiscard]] std::uint64_t invalidate_event(
      const SignalId event);

  void cancel_event(const SignalId event);

  void notify_event(
      const SignalId event,
      const SimulationTick delay,
      const EventNotificationKind kind,
      const StableOrder order);

  void notify_execution_point(
      ProcessState& process,
      const InstructionIndex instruction,
      const ExecutionPointKind kind,
      const SourceLocation& source);

  [[nodiscard]] bool monitor_watches(
      const SignalId signal) const;

  [[nodiscard]] std::string render_monitor() const;

  void schedule_monitor_publication();

  void install_monitor(
      const ProcessId process,
      const MonitorInstall& registration);

  void set_monitor_enabled(const bool enabled);

  [[nodiscard]] static std::uint64_t initial_random_state(
      const std::uint64_t seed,
      const ProcessId process) noexcept;

  [[nodiscard]] static std::uint32_t next_random(
      ProcessState& process) noexcept;

  [[nodiscard]] static std::optional<std::uint32_t>
  known_random_bound(const PackedLogic4& value);

  [[nodiscard]] PackedLogic4 random_value(
      const ProcessId process_id,
      const RandomKind kind,
      const std::optional<PackedLogic4>& maximum,
      const std::optional<PackedLogic4>& minimum);

  void publish(SignalId signal_id, PackedLogic4 value);

  void commit(SignalId signal_id, PackedLogic4 value);

  [[nodiscard]] PackedLogic4 initial_driver_value(
      const SignalId signal_id) const;

  PackedLogic4& driver_slot(
      const ProcessId process,
      const SignalId signal_id);

  [[nodiscard]] PackedLogic4 resolved_driver_value(
      const SignalId signal_id) const;

  PackedLogic4& external_driver_slot(
      const SignalId signal_id);

  void register_driver(
      const ProcessId process,
      const SignalId signal_id);

  void set_driver(
      const ProcessId process,
      const SignalId signal_id,
      PackedLogic4 value);

  void commit_driver(
      const ProcessId process,
      const SignalId signal_id,
      PackedLogic4 value);

  [[nodiscard]] const PackedLogic4& current_driver_value(
      const ProcessId process,
      const SignalId signal_id) const;

  void commit_slice(
      const SignalId signal_id,
      PackedLogic4 value,
      const std::size_t offset);

  void commit_driver_slice(
      const ProcessId process,
      const SignalId signal_id,
      PackedLogic4 value,
      const std::size_t offset);

  void schedule_update_commit();

  void stage_update(
      const std::optional<ProcessId> driver,
      SignalId signal_id,
      PackedLogic4 staged_value);

  void stage_update(
      const SignalId signal_id,
      PackedLogic4 staged_value);

  void stage_update(
      const ProcessId process,
      const SignalId signal_id,
      PackedLogic4 staged_value);

  void stage_update_slice(
      const std::optional<ProcessId> driver,
      const SignalId signal_id,
      PackedLogic4 value,
      const std::size_t offset);

  void stage_update_slice(
      const SignalId signal_id,
      PackedLogic4 value,
      const std::size_t offset);

  void stage_update_slice(
      const ProcessId process,
      const SignalId signal_id,
      PackedLogic4 value,
      const std::size_t offset);

  void schedule_inertial(
      const ProcessId process,
      const SignalId signal,
      PackedLogic4 value,
      const std::optional<std::size_t> offset,
      const TransitionDelays& delays);

  void schedule_projected_scalar_waveform(
      const ProcessId process,
      const SignalId signal,
      const std::uint32_t offset,
      const std::vector<
          std::pair<PackedLogic4, SimulationTick>>& elements,
      const SimulationTick rejection,
      const ProjectedDelayMode mode);

  void schedule_projected_waveform(
      const ProcessId process,
      const SignalId signal,
      const std::vector<ProjectedWaveformValue>& elements,
      const std::optional<std::size_t> offset,
      const SimulationTick rejection,
      const ProjectedDelayMode mode);

  void schedule_projected(
      const ProcessId process,
      const SignalId signal,
      const PackedLogic4& value,
      const std::optional<std::size_t> offset,
      const SimulationTick delay,
      const SimulationTick rejection,
      const ProjectedDelayMode mode);

  [[noreturn]] void fail(const ProcessState &process,
                         const std::string &message) const;
};

} // namespace fsim::runtime::simir
