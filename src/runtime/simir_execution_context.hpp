// SPDX-License-Identifier: Apache-2.0
// Included inside namespace fsim::runtime::simir.

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
    (void)systemverilog_string_length(value);
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
      const bool zero_pad,
      const SystemVerilogScalarKind scalar_kind) override {
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
            zero_pad,
            scalar_kind),
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
  void force_driver_signal_slice(
      const SignalId signal,
      PackedLogic4 value,
      const std::size_t offset) override {
    owner.force_driver_slice(
        process, signal, std::move(value), offset);
  }
  void release_driver_signal_slice(
      const SignalId signal,
      const std::size_t offset,
      const std::size_t width) override {
    owner.release_driver_slice(process, signal, offset, width);
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
    if (owner.route_module_path_update(
            process, signal, value, std::nullopt, nullptr, delay)) {
      return;
    }
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
    if (owner.route_module_path_update(
            process, signal, packed, std::nullopt, nullptr, delay)) {
      return;
    }
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
    if (owner.route_module_path_update(
            process, signal, value, offset, nullptr, delay)) {
      return;
    }
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

  [[nodiscard]] SimulationTick signal_last_active(
      const SignalId signal) const override {
    return signal_attribute_detail::last_active(owner, signal);
  }
  [[nodiscard]] bool signal_driving(
      const SignalId signal) const override {
    return signal_attribute_detail::driving(owner, process, signal);
  }
  [[nodiscard]] Logic4Word signal_driving_value_word(
      const SignalId signal) const override {
    return signal_attribute_detail::driving_value(
        owner, process, signal).low_word();
  }
  [[nodiscard]] Logic9Word signal_driving_value_logic9_word(
      const SignalId signal) const override {
    return signal_attribute_detail::driving_value(
        owner, process, signal).logic9_low_word();
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
      const bool zero_pad,
      const SystemVerilogScalarKind scalar_kind) override {
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
            zero_pad,
            scalar_kind);
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

  void install_monitor(const MonitorInstall& registration) override {
    owner.install_monitor(process, registration);
  }
  void set_monitor_enabled(const bool enabled) override {
    owner.set_monitor_enabled(enabled);
  }
  [[nodiscard]] PackedLogic4 random_value(
      const RandomKind kind,
      const std::optional<PackedLogic4>& maximum,
      const std::optional<PackedLogic4>& minimum) override {
    return owner.random_value(process, kind, maximum, minimum);
  }
  void report(const std::string_view message,
              const AssertionSeverity severity,
              const SourceLocation& source) override {
    if (!owner.report_hook) return;
    owner.report_hook(process, message, severity, source,
                      owner.scheduler.now(), owner.scheduler.delta());
  }
  [[nodiscard]] Logic9 evaluate_vital_timing_check(
      const InstructionIndex instruction, const VitalTimingCheck& operation) override {
    return owner.execute_vital_timing_check(process, instruction, operation);
  }
  void execute_vital_delay(const InstructionIndex instruction,
      const VitalDelay& operation, const VitalDelayRuntimeValues& values) override {
    owner.execute_vital_delay(process, instruction, operation, values);
  }
  [[nodiscard]] bool execution_points_enabled() const noexcept override {
    return static_cast<bool>(owner.execution_point_hook);
  }
};
