// SPDX-License-Identifier: Apache-2.0
#include "simir_internal.hpp"
#include "fsim/runtime/string_methods.hpp"
namespace fsim::runtime::simir {
struct Interpreter::Impl::ExecutionContext final
    : ProcessExecutionContext {
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
  [[nodiscard]] std::string
  read_string_object(const StringObjectId object) const override {
    return owner.get_string_object(object).initial_value;
  }
  void write_string_object(
      const StringObjectId object,
      const std::string_view value) override {
    if (value.size() > maximum_string_bytes) {
      throw std::length_error{
          "SimIR string object exceeds byte limit"};
    }
    owner.get_string_object(object).initial_value = value;
  }
  [[nodiscard]] ContainerValue read_container_object(
      const ContainerObjectId object) const override {
    return owner.read_container_object_value(object);
  }
  void write_container_object(
      const ContainerObjectId object,
      const ContainerValue& value) override {
    owner.write_container_object_value(object, value);
  }
  [[nodiscard]] FileHandle open_file(
      const std::string_view path,
      const std::string_view mode) override {
    return owner.open_file(process, path, mode);
  }
  void close_file(const FileHandle handle) override {
    owner.close_file(process, handle);
  }
  void write_file(
      const FileHandle handle,
      const std::string_view text,
      const bool newline) override {
    owner.write_file(process, handle, text, newline);
  }
  void write_file_formatted(
      const FileHandle handle,
      const std::string_view prefix,
      const std::string_view suffix,
      const OutputFormat format,
      const PackedLogic4& value,
      const bool signed_decimal,
      const bool suppress_leading_zero,
      const std::uint32_t minimum_width,
      const bool left_justify,
      const bool zero_pad) override {
    owner.write_file(
        process,
        handle,
        make_formatted_output(
            prefix,
            suffix,
            format,
            value,
            signed_decimal,
            suppress_leading_zero,
            minimum_width,
            left_justify,
            zero_pad),
        false);
  }
  [[nodiscard]] std::string read_file_line(
      const FileHandle handle,
      std::uint32_t& count) override {
    return owner.read_file_line(process, handle, count);
  }
  [[nodiscard]] std::int32_t read_file_character(
      const FileHandle handle) override {
    return owner.read_file_character(process, handle);
  }
  [[nodiscard]] std::int32_t unread_file_character(
      const FileHandle handle,
      const std::int32_t character) override {
    return owner.unread_file_character(process, handle, character);
  }
  [[nodiscard]] bool file_end_of_file(
      const FileHandle handle) override {
    return owner.file_end_of_file(process, handle);
  }
  [[nodiscard]] std::string file_error(
      const FileHandle handle,
      bool& has_error) override {
    return owner.file_error(process, handle, has_error);
  }
  [[nodiscard]] std::int32_t position_file(
      const FileHandle handle,
      const FilePositionKind kind,
      const std::int32_t offset,
      const std::int32_t origin) override {
    return owner.position_file(process, handle, kind, offset, origin);
  }
  void flush_file(const std::optional<FileHandle> handle) override {
    owner.flush_file(process, handle);
  }
  [[nodiscard]] Logic9Word
  read_signal_logic9_word(const SignalId signal) const override {
    return owner.get_signal(signal).initial_value.logic9_low_word();
  }
  void write_blocking(
      const SignalId signal, PackedLogic4 value) override {
    owner.commit_driver(process, signal, std::move(value));
  }
  void write_blocking_word(
      const SignalId signal,
      const Logic4Word value) override {
    owner.commit_driver(
        process,
        signal,
        PackedLogic4::from_aval_bval(
            value.width, value.aval, value.bval));
  }
  void write_blocking_slice(
      const SignalId signal,
      PackedLogic4 value,
      const std::size_t offset) override {
    owner.commit_driver_slice(
        process, signal, std::move(value), offset);
  }
  void write_blocking_slice_word(
      const SignalId signal,
      const Logic4Word value,
      const std::uint32_t offset) override {
    owner.commit_driver_slice(
        process,
        signal,
        PackedLogic4::from_aval_bval(
            value.width, value.aval, value.bval),
        offset);
  }
  void force_signal_slice(
      const SignalId signal,
      PackedLogic4 value,
      const std::size_t offset) override {
    owner.force_slice(signal, std::move(value), offset);
  }
  void release_signal_slice(
      const SignalId signal,
      const std::size_t offset,
      const std::size_t width) override {
    owner.release_slice(signal, offset, width);
  }

  void write_update(
      const SignalId signal, PackedLogic4 value) override {
    owner.stage_update(process, signal, std::move(value));
  }

  void write_update_word(
      const SignalId signal,
      const Logic4Word value) override {
    owner.stage_update(
        process,
        signal,
        PackedLogic4::from_aval_bval(
            value.width, value.aval, value.bval));
  }

  void write_update_slice(
      const SignalId signal,
      PackedLogic4 value,
      const std::size_t offset) override {
    owner.stage_update_slice(
        process, signal, std::move(value), offset);
  }

  void write_update_slice_word(
      const SignalId signal,
      const Logic4Word value,
      const std::uint32_t offset) override {
    owner.stage_update_slice(
        process,
        signal,
        PackedLogic4::from_aval_bval(
            value.width, value.aval, value.bval),
        offset);
  }

  void write_after(
      const SignalId signal,
      PackedLogic4 value,
      const SimulationTick delay) override {
    owner.scheduler.schedule_after(
        delay,
        SchedulerPhase::update,
        process,
        [&owner = owner, driver = process, signal,
         value = std::move(value)](
            Scheduler&) mutable {
          owner.stage_update(
              driver, signal, std::move(value));
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
        [&owner = owner, driver = process, signal,
         value = std::move(packed)](
            Scheduler&) mutable {
          owner.stage_update(
              driver, signal, std::move(value));
        });
  }

  void write_after_slice(
      const SignalId signal,
      PackedLogic4 value,
      const std::size_t offset,
      const SimulationTick delay) override {
    owner.scheduler.schedule_after(
        delay,
        SchedulerPhase::update,
        process,
        [&owner = owner,
         driver = process,
         signal,
         value = std::move(value),
         offset](Scheduler&) mutable {
          owner.stage_update_slice(
              driver, signal, std::move(value), offset);
        });
  }

  void write_after_slice_word(
      const SignalId signal,
      const Logic4Word value,
      const std::uint32_t offset,
      const SimulationTick delay) override {
    write_after_slice(
        signal,
        PackedLogic4::from_aval_bval(
            value.width, value.aval, value.bval),
        offset,
        delay);
  }

  void write_inertial(
      const SignalId signal,
      PackedLogic4 value,
      const TransitionDelays& delays) override {
    owner.schedule_inertial(
        process,
        signal,
        std::move(value),
        std::nullopt,
        delays);
  }

  void write_inertial_slice(
      const SignalId signal,
      PackedLogic4 value,
      const std::size_t offset,
      const TransitionDelays& delays) override {
    owner.schedule_inertial(
        process,
        signal,
        std::move(value),
        offset,
        delays);
  }

  void write_projected(
      const SignalId signal,
      PackedLogic4 value,
      const SimulationTick delay,
      const SimulationTick rejection,
      const ProjectedDelayMode mode) override {
    owner.schedule_projected(
        process,
        signal,
        value,
        std::nullopt,
        delay,
        rejection,
        mode);
  }

  void write_projected_slice(
      const SignalId signal,
      PackedLogic4 value,
      const std::size_t offset,
      const SimulationTick delay,
      const SimulationTick rejection,
      const ProjectedDelayMode mode) override {
    owner.schedule_projected(
        process,
        signal,
        value,
        offset,
        delay,
        rejection,
        mode);
  }

  void write_projected_waveform(
      const SignalId signal,
      std::vector<ProjectedWaveformValue> elements,
      const SimulationTick rejection,
      const ProjectedDelayMode mode) override {
    owner.schedule_projected_waveform(
        process,
        signal,
        elements,
        std::nullopt,
        rejection,
        mode);
  }

  void write_projected_waveform_slice(
      const SignalId signal,
      std::vector<ProjectedWaveformValue> elements,
      const std::size_t offset,
      const SimulationTick rejection,
      const ProjectedDelayMode mode) override {
    owner.schedule_projected_waveform(
        process,
        signal,
        elements,
        offset,
        rejection,
        mode);
  }

  void notify_event(
      const SignalId event,
      const SimulationTick delay,
      const EventNotificationKind kind) override {
    owner.notify_event(event, delay, kind, process);
  }

  void cancel_event(const SignalId event) override {
    owner.cancel_event(event);
  }

  [[nodiscard]] bool
  signal_event(const SignalId signal) const override {
    (void)owner.get_signal(signal);
    const auto& event = owner.signal_events[signal];
    return event
        && event->first == owner.scheduler.now()
        && event->second == owner.scheduler.delta();
  }

  [[nodiscard]] Logic4Word
  signal_last_value_word(const SignalId signal) const override {
    (void)owner.get_signal(signal);
    return owner.signal_last_values[signal].low_word();
  }

  [[nodiscard]] Logic9Word
  signal_last_value_logic9_word(
      const SignalId signal) const override {
    (void)owner.get_signal(signal);
    return owner.signal_last_values[signal].logic9_low_word();
  }

  [[nodiscard]] SimulationTick
  signal_last_event(const SignalId signal) const override {
    (void)owner.get_signal(signal);
    const auto& event = owner.signal_events[signal];
    return event
        ? owner.scheduler.now() - event->first
        : std::numeric_limits<SimulationTick>::max();
  }

  [[nodiscard]] bool
  signal_active(const SignalId signal) const override {
    (void)owner.get_signal(signal);
    const auto& transaction = owner.signal_transactions[signal];
    return transaction
        && transaction->first == owner.scheduler.now()
        && transaction->second == owner.scheduler.delta();
  }

  void request_channel_update(
      const std::uint64_t channel) override {
    owner.request_channel_update(process, channel);
  }

  void display(
      const std::string_view text,
      const bool newline) override {
    if (owner.output_hook) {
      owner.output_hook(
          process,
          text,
          newline,
          owner.scheduler.now(),
          owner.scheduler.delta());
    }
  }

  void postpone_display(
      const std::string_view text,
      const bool newline) override {
    owner.scheduler.schedule(
        SchedulerPhase::postponed,
        process,
        [&owner = owner,
         process = process,
         text = std::string{text},
         newline](Scheduler& scheduler) {
          if (owner.output_hook) {
            owner.output_hook(
                process,
                text,
                newline,
                scheduler.now(),
                scheduler.delta());
          }
    });
  }

  [[nodiscard]] SimulationTick current_time() const noexcept override {
    return owner.scheduler.now();
  }

  void display_formatted(
      const std::string_view prefix,
      const std::string_view suffix,
      const OutputFormat format,
      const PackedLogic4& value,
      const bool newline,
      const bool postponed,
      const bool signed_decimal,
      const bool suppress_leading_zero,
      const std::uint32_t minimum_width,
      const bool left_justify,
      const bool zero_pad) override {
    auto text =
        make_formatted_output(
            prefix,
            suffix,
            format,
            value,
            signed_decimal,
            suppress_leading_zero,
            minimum_width,
            left_justify,
            zero_pad);
    if (postponed) {
      owner.scheduler.schedule(
          SchedulerPhase::postponed,
          process,
          [&owner = owner,
           process = process,
           text = std::move(text),
           newline](Scheduler& scheduler) {
            if (owner.output_hook) {
              owner.output_hook(
                  process,
                  text,
                  newline,
                  scheduler.now(),
                  scheduler.delta());
            }
          });
    } else if (owner.output_hook) {
      owner.output_hook(
          process,
          text,
          newline,
          owner.scheduler.now(),
          owner.scheduler.delta());
    }
  }

  void display_time(
      const std::string_view prefix,
      const std::string_view suffix,
      const bool newline,
      const bool postponed,
      const std::uint32_t minimum_width,
      const bool left_justify,
      const bool zero_pad) override {
    auto text = make_time_output(
        prefix,
        suffix,
        owner.scheduler.now(),
        minimum_width,
        left_justify,
        zero_pad);
    if (postponed) {
      owner.scheduler.schedule(
          SchedulerPhase::postponed,
          process,
          [&owner = owner,
           process = process,
           text = std::move(text),
           newline](Scheduler& scheduler) {
            if (owner.output_hook) {
              owner.output_hook(
                  process,
                  text,
                  newline,
                  scheduler.now(),
                  scheduler.delta());
            }
          });
    } else if (owner.output_hook) {
      owner.output_hook(
          process,
          text,
          newline,
          owner.scheduler.now(),
          owner.scheduler.delta());
    }
  }

  void install_monitor(
      const MonitorInstall& registration) override {
    owner.install_monitor(process, registration);
  }

  void set_monitor_enabled(const bool enabled) override {
    owner.set_monitor_enabled(enabled);
  }

  [[nodiscard]] PackedLogic4 random_value(
      const RandomKind kind,
      const std::optional<PackedLogic4>& maximum,
      const std::optional<PackedLogic4>& minimum) override {
    return owner.random_value(
        process, kind, maximum, minimum);
  }

  void report(
      const std::string_view message,
      const AssertionSeverity severity,
      const SourceLocation& source) override {
    if (owner.report_hook) {
      owner.report_hook(
          process,
          message,
          severity,
          source,
          owner.scheduler.now(),
          owner.scheduler.delta());
    }
  }

  [[nodiscard]] bool
  execution_points_enabled() const noexcept override {
    return static_cast<bool>(owner.execution_point_hook);
  }
};

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
        process, instruction, kind, process.current_source);
    if (scheduler.stop_requested()) {
      queue_current(process.program.id);
    }
    return;
  }
  if (const auto* wait = fsim::runtime::simir::operation_get_if<WaitFor>(&operation)) {
    clear_wait_timeout(process);
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
    notify_execution_point(
        process, instruction, ExecutionPointKind::process_suspend,
        process.current_source);
    return;
  }
  if (fsim::runtime::simir::operation_holds<WaitForever>(operation)) {
    clear_wait_timeout(process);
    notify_execution_point(
        process, instruction, ExecutionPointKind::process_suspend,
        process.current_source);
    return;
  }
  if (fsim::runtime::simir::operation_holds<Yield>(operation)) {
    clear_wait_timeout(process);
    queue_next_delta(process.program.id);
    notify_execution_point(
        process, instruction, ExecutionPointKind::process_suspend,
        process.current_source);
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
      process.halted = true;
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
  if (process.executor) {
    ExecutionContext context{*this, id};
    while (!process.halted) {
      const auto boundary = process.executor->resume(context, process.pc);
      const bool debug_boundary =
          boundary.instruction < process.program.operations.size()
          && fsim::runtime::simir::operation_holds<DebugPoint>(
              process.program.operations[boundary.instruction]);
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
            fail(process, "string index is outside the byte range");
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
          } else if constexpr (std::is_same_v<OperationType, CopyRegister>) {
            get_register(process, op.destination) =
                coerce_value_kind(
                    get_register(process, op.source),
                    register_value_kind(
                        process, op.destination));
            ++process.pc;
          } else if constexpr (std::is_same_v<OperationType, LoadStringConstant>) {
            if (op.value.size() > maximum_string_bytes) {
              fail(process, "string literal exceeds 4096-byte limit");
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
            const bool equal =
                get_string_register(process, op.lhs)
                == get_string_register(process, op.rhs);
            get_register(process, op.destination) =
                PackedLogic4{
                    1,
                    equal != op.not_equal
                        ? Logic4::one
                        : Logic4::zero};
            ++process.pc;
          } else if constexpr (std::is_same_v<OperationType, StringLength>) {
            const auto size =
                get_string_register(process, op.source).size();
            get_register(process, op.destination) =
                PackedLogic4::from_aval_bval(
                    32, static_cast<std::uint32_t>(size), 0);
            ++process.pc;
          } else if constexpr (std::is_same_v<OperationType, StringIndex>) {
            auto& source =
                get_string_register(process, op.source);
            const auto index =
                known_string_index(
                    op.index, op.signed_index, source.size());
            get_register(process, op.destination) =
                PackedLogic4::from_aval_bval(
                    8,
                    static_cast<unsigned char>(source[index]),
                    0);
            ++process.pc;
          } else if constexpr (std::is_same_v<OperationType, StringReplaceByte>) {
            auto& target =
                get_string_register(process, op.target);
            const auto index =
                known_string_index(
                    op.index, op.signed_index, target.size());
            const auto byte =
                get_register(process, op.source).low_word();
            if (byte.bval != 0) {
              fail(process, "string replacement byte contains X or Z");
            }
            target[index] =
                static_cast<char>(byte.aval & UINT64_C(0xff));
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
              force_slice(
                  op.signal,
                  get_register(process, op.source),
                  op.offset);
            } catch (const std::invalid_argument& error) {
              fail(process, error.what());
            }
            ++process.pc;
          } else if constexpr (std::is_same_v<OperationType, ReleaseSignalSlice>) {
            try {
              release_slice(
                  op.signal, op.offset, op.width);
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
      handle_boundary(process, instruction, instruction + 1);
      if (!debug_boundary || scheduler.stop_requested()) {
        return;
      }
    }
  }
}
} // namespace fsim::runtime::simir
