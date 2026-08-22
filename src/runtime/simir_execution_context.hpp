// SPDX-License-Identifier: Apache-2.0
// Internal execution-context definition shared only by simir_execution.cpp.

#include "simir_internal.hpp"
#include "simir_signal_attributes.hpp"

namespace fsim::runtime::simir {
struct Interpreter::Impl::ExecutionContext final
    : ProcessExecutionContext {
    Impl& owner;
    ProcessId process;
    ExecutionContext(Impl& owner_value, const ProcessId process_value)
        : owner(owner_value)
        , process(process_value)
    {
    }
    [[nodiscard]] std::uint64_t static_trigger_mask() const noexcept override
    {
        return owner.processes[process].static_trigger_mask;
    }
    [[nodiscard]] PackedLogic4
    read_signal(const SignalId signal) const override
    {
        return owner.get_signal(signal).initial_value;
    }
    [[nodiscard]] Logic4Word
    read_signal_word(const SignalId signal) const override
    {
        // Native lowering selects this callback only for validated nonempty
        // Logic4 signals no wider than one ABI word. Avoid repeating the
        // public checked conversion on every generated signal read.
        return owner.get_signal(signal).initial_value.unchecked_low_word();
    }
    [[nodiscard]] std::span<const std::uint64_t>
    direct_signal_aval() const noexcept override
    {
        return owner.direct_signal_aval;
    }
    [[nodiscard]] std::span<const std::uint64_t>
    direct_signal_bval() const noexcept override
    {
        return owner.direct_signal_bval;
    }
    [[nodiscard]] std::span<std::uint64_t>
    direct_code_coverage_counters() noexcept override
    {
        return owner.code_coverage_counters.mutable_values();
    }
    [[nodiscard]] CodeCoverageCounterRuntimeStatus record_code_coverage_counter(
        const ::fsim::runtime::CodeCoverageCounterId counter) override
    {
        const auto update = owner.code_coverage_counters.record(counter);
        if (update == CodeCoverageCounterUpdate::Unavailable) {
            return CodeCoverageCounterRuntimeStatus::Unavailable;
        }
        if (update == CodeCoverageCounterUpdate::OutOfRange) {
            return CodeCoverageCounterRuntimeStatus::OutOfRange;
        }
        if (update == CodeCoverageCounterUpdate::FirstOverflow
            && owner.code_coverage_overflow_hook) {
            owner.code_coverage_overflow_hook(counter);
        }
        return CodeCoverageCounterRuntimeStatus::Recorded;
    }
    [[nodiscard]] std::span<const std::uint64_t>
    direct_signal_logic9_plane0() const noexcept override
    {
        return owner.direct_signal_logic9_plane0;
    }
    [[nodiscard]] std::span<const std::uint64_t>
    direct_signal_logic9_plane1() const noexcept override
    {
        return owner.direct_signal_logic9_plane1;
    }
    [[nodiscard]] std::span<const std::uint64_t>
    direct_signal_logic9_plane2() const noexcept override
    {
        return owner.direct_signal_logic9_plane2;
    }
    [[nodiscard]] std::span<const std::uint64_t>
    direct_signal_logic9_plane3() const noexcept override
    {
        return owner.direct_signal_logic9_plane3;
    }
    [[nodiscard]] std::span<const std::uint64_t>
    direct_wide_signal_aval() const noexcept override
    {
        return owner.direct_wide_signal_aval;
    }
    [[nodiscard]] std::span<const std::uint64_t>
    direct_wide_signal_bval() const noexcept override
    {
        return owner.direct_wide_signal_bval;
    }
    [[nodiscard]] std::span<const std::uint64_t>
    direct_wide_signal_logic9_plane2() const noexcept override
    {
        return owner.direct_wide_signal_logic9_plane2;
    }
    [[nodiscard]] std::span<const std::uint64_t>
    direct_wide_signal_logic9_plane3() const noexcept override
    {
        return owner.direct_wide_signal_logic9_plane3;
    }
    [[nodiscard]] std::span<const std::uint32_t>
    direct_wide_signal_offsets() const noexcept override
    {
        return owner.direct_wide_signal_offsets;
    }
    [[nodiscard]] std::span<const ProcessId>
    direct_single_driver_processes() const noexcept override
    {
        return owner.direct_single_driver_processes;
    }
    [[nodiscard]] std::span<const ProcessId>
    stable_single_writer_processes() const noexcept override
    {
        return owner.stable_single_writer_processes;
    }
    [[nodiscard]] std::uint64_t
    signal_writer_revision() const noexcept override
    {
        return owner.signal_writer_revision;
    }
    void read_signal_planes(
        const SignalId signal,
        const std::span<std::uint64_t> aval,
        const std::span<std::uint64_t> bval,
        const std::span<std::uint64_t> logic9_plane2,
        const std::span<std::uint64_t> logic9_plane3) const override
    {
        const auto& value = owner.get_signal(signal).initial_value;
        const auto expected_words = (value.width() + 63U) / 64U;
        if (aval.size() != expected_words || bval.size() != expected_words
            || (!value.is_logic9()
                && (!logic9_plane2.empty() || !logic9_plane3.empty()))
            || (value.is_logic9()
                && (logic9_plane2.size() != expected_words
                    || logic9_plane3.size() != expected_words))) {
            throw std::logic_error {
                "arbitrary-width signal destination planes have an invalid size"
            };
        }
        std::ranges::copy(value.aval_words(), aval.begin());
        std::ranges::copy(value.bval_words(), bval.begin());
        if (value.is_logic9()) {
            std::ranges::copy(
                value.logic9_plane_words(2), logic9_plane2.begin());
            std::ranges::copy(
                value.logic9_plane_words(3), logic9_plane3.begin());
        }
    }
    [[nodiscard]] std::string
    read_string_object(const StringObjectId object) const override
    {
        return owner.get_string_object(object).initial_value;
    }
    void write_string_object(
        const StringObjectId object,
        const std::string_view value) override
    {
        if (value.size() > maximum_string_bytes) {
            throw std::length_error {
                "SimIR string object exceeds byte limit"
            };
        }
        (void)systemverilog_string_length(value);
        owner.get_string_object(object).initial_value = value;
    }
    [[nodiscard]] ContainerValue read_container_object(
        const ContainerObjectId object) const override
    {
        return owner.read_container_object_value(object);
    }
    [[nodiscard]] bool copy_container_object(
        const ContainerObjectId object,
        ContainerValue& destination) const override
    {
        const auto& source = owner.read_container_object_value(object);
        if (destination.type != source.type) {
            return false;
        }
        destination = source;
        return true;
    }
    [[nodiscard]] const ContainerValue* borrow_container_object(
        const ContainerObjectId object) const override
    {
        return &owner.read_container_object_value(object);
    }
    [[nodiscard]] bool container_object_has_type(
        const ContainerObjectId object,
        const ContainerType& type) const override
    {
        return owner.get_container_object(object).initial_value.type == type;
    }
    [[nodiscard]] bool read_container_object_element(
        const ContainerObjectId object,
        const std::size_t ordinal,
        PackedLogic4& result) const override
    {
        return owner.read_container_object_element(object, ordinal, result);
    }
    void write_container_object(
        const ContainerObjectId object,
        const ContainerValue& value) override
    {
        owner.write_container_object_value(object, value);
    }
    void write_container_object_element(
        const ContainerObjectId object,
        const PackedLogic4& index,
        const bool signed_index,
        const bool linear_index,
        const PackedLogic4& value,
        const ProcessId generated_process,
        const InstructionIndex instruction) override
    {
        owner.write_container_object_element_value(
            object, index, signed_index, linear_index, value,
            generated_process, instruction);
    }
    [[nodiscard]] FileHandle open_file(
        const std::string_view path,
        const std::string_view mode) override
    {
        return owner.open_file(process, path, mode);
    }
    void close_file(const FileHandle handle) override
    {
        owner.close_file(process, handle);
    }
    void write_file(
        const FileHandle handle,
        const std::string_view text,
        const bool newline) override
    {
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
        const SystemVerilogScalarKind scalar_kind) override
    {
        if (format == OutputFormat::time) {
            const auto decoded = decode_systemverilog_scalar_payload(
                value, scalar_kind);
            const auto tick = decoded ? decoded.value.as_time() : std::nullopt;
            if (!tick) {
                throw std::runtime_error {
                    "invalid time payload for formatted file output"
                };
            }
            owner.write_file(
                process,
                handle,
                make_time_output(
                    prefix,
                    suffix,
                    *tick,
                    owner.time_format,
                    minimum_width == 0 && !suppress_leading_zero,
                    minimum_width,
                    left_justify,
                    zero_pad),
                false);
            return;
        }
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
        std::uint32_t& count) override
    {
        return owner.read_file_line(process, handle, count);
    }
    [[nodiscard]] std::int32_t read_file_character(
        const FileHandle handle) override
    {
        return owner.read_file_character(process, handle);
    }
    [[nodiscard]] std::int32_t unread_file_character(
        const FileHandle handle,
        const std::int32_t character) override
    {
        return owner.unread_file_character(process, handle, character);
    }
    [[nodiscard]] bool file_end_of_file(
        const FileHandle handle) override
    {
        return owner.file_end_of_file(process, handle);
    }
    [[nodiscard]] std::string file_error(
        const FileHandle handle,
        bool& has_error) override
    {
        return owner.file_error(process, handle, has_error);
    }
    [[nodiscard]] std::int32_t position_file(
        const FileHandle handle,
        const FilePositionKind kind,
        const std::int32_t offset,
        const std::int32_t origin) override
    {
        return owner.position_file(process, handle, kind, offset, origin);
    }
    void flush_file(const std::optional<FileHandle> handle) override
    {
        owner.flush_file(process, handle);
    }
    [[nodiscard]] Logic9Word
    read_signal_logic9_word(const SignalId signal) const override
    {
        return owner.get_signal(signal).initial_value.logic9_low_word();
    }
    void write_blocking(
        const SignalId signal, PackedLogic4 value) override
    {
        owner.commit_driver(process, signal, std::move(value));
    }
    void write_blocking_word(
        const SignalId signal,
        const Logic4Word value) override
    {
        if (owner.can_publish_blocking_word(signal)) {
            if (owner.signals[signal].initial_value.width()
                != value.width) {
                throw std::invalid_argument(
                    "SimIR signal assignment width mismatch");
            }
            owner.publish_native_word(signal, value);
            return;
        }
        owner.commit_driver(
            process,
            signal,
            PackedLogic4::from_aval_bval(
                value.width, value.aval, value.bval));
    }
    void write_blocking_slice(
        const SignalId signal,
        PackedLogic4 value,
        const std::size_t offset) override
    {
        owner.commit_driver_slice(
            process, signal, std::move(value), offset);
    }
    void write_blocking_slice_word(
        const SignalId signal,
        const Logic4Word value,
        const std::uint32_t offset) override
    {
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
        const std::size_t offset) override
    {
        owner.force_slice(signal, std::move(value), offset);
    }
    void release_signal_slice(
        const SignalId signal,
        const std::size_t offset,
        const std::size_t width) override
    {
        owner.release_slice(signal, offset, width);
    }
    void force_driver_signal_slice(
        const SignalId signal,
        PackedLogic4 value,
        const std::size_t offset) override
    {
        owner.force_driver_slice(
            process, signal, std::move(value), offset);
    }
    void release_driver_signal_slice(
        const SignalId signal,
        const std::size_t offset,
        const std::size_t width) override
    {
        owner.release_driver_slice(process, signal, offset, width);
    }

    void write_update(
        const SignalId signal, PackedLogic4 value) override
    {
        owner.stage_update(process, signal, std::move(value));
    }

    void write_update_word(
        const SignalId signal,
        const Logic4Word value) override
    {
        owner.stage_update(
            process,
            signal,
            PackedLogic4::from_aval_bval(
                value.width, value.aval, value.bval));
    }

    void write_update_slice(
        const SignalId signal,
        PackedLogic4 value,
        const std::size_t offset) override
    {
        owner.stage_update_slice(
            process, signal, std::move(value), offset);
    }

    void write_update_slice_word(
        const SignalId signal,
        const Logic4Word value,
        const std::uint32_t offset) override
    {
        owner.stage_update_slice(
            process,
            signal,
            PackedLogic4::from_aval_bval(
                value.width, value.aval, value.bval),
            offset);
    }

    void write_update_words(
        const std::span<const ProcessUpdateWord> updates) override
    {
        owner.stage_update_words(process, updates);
    }

    void write_validated_update_words(
        const std::span<const ProcessUpdateWord> updates) override
    {
        owner.stage_validated_update_words(process, updates);
    }

    [[nodiscard]] const void* direct_update_domain() const noexcept override
    {
        return &owner;
    }

    bool write_validated_update_slot_batches(
        const std::span<const ProcessUpdateSlotBatch> batches) override
    {
        return owner.stage_validated_update_slot_batches(batches);
    }

    bool write_validated_logic9_update_batch(
        const ProcessLogic9UpdateBatch& batch) override
    {
        return owner.stage_validated_logic9_update_batch(batch);
    }

    bool write_validated_logic9_update_batches(
        const std::span<const ProcessLogic9UpdateBatch> batches) override
    {
        return owner.stage_validated_logic9_update_batches(batches);
    }

    [[nodiscard]] bool supports_direct_word_updates() const noexcept override
    {
        return owner.module_paths.empty();
    }

    void write_after(
        const SignalId signal,
        PackedLogic4 value,
        const SimulationTick delay) override
    {
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
        const SimulationTick delay) override
    {
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
        const SimulationTick delay) override
    {
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
        const SimulationTick delay) override
    {
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
        const TransitionDelays& delays) override
    {
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
        const TransitionDelays& delays) override
    {
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
        const ProjectedDelayMode mode) override
    {
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
        const ProjectedDelayMode mode) override
    {
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
        const ProjectedDelayMode mode) override
    {
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
        const ProjectedDelayMode mode) override
    {
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
        const EventNotificationKind kind) override
    {
        owner.notify_event(event, delay, kind, process);
    }

    void cancel_event(const SignalId event) override
    {
        owner.cancel_event(event);
    }

    [[nodiscard]] bool
    signal_event(const SignalId signal) const override
    {
        (void)owner.get_signal(signal);
        const auto& event = owner.signal_events[signal];
        return event
            && event->first == owner.scheduler.now()
            && event->second == owner.scheduler.delta();
    }

    [[nodiscard]] Logic4Word
    signal_last_value_word(const SignalId signal) const override
    {
        (void)owner.get_signal(signal);
        return owner.signal_last_values[signal].low_word();
    }

    [[nodiscard]] Logic9Word
    signal_last_value_logic9_word(
        const SignalId signal) const override
    {
        (void)owner.get_signal(signal);
        return owner.signal_last_values[signal].logic9_low_word();
    }

    [[nodiscard]] SimulationTick
    signal_last_event(const SignalId signal) const override
    {
        (void)owner.get_signal(signal);
        const auto& event = owner.signal_events[signal];
        return event
            ? owner.scheduler.now() - event->first
            : std::numeric_limits<SimulationTick>::max();
    }

    [[nodiscard]] bool
    signal_active(const SignalId signal) const override
    {
        (void)owner.get_signal(signal);
        const auto& transaction = owner.signal_transactions[signal];
        return transaction
            && transaction->first == owner.scheduler.now()
            && transaction->second == owner.scheduler.delta();
    }

    [[nodiscard]] SimulationTick signal_last_active(
        const SignalId signal) const override
    {
        return signal_attribute_detail::last_active(owner, signal);
    }
    [[nodiscard]] bool signal_driving(
        const SignalId signal) const override
    {
        return signal_attribute_detail::driving(owner, process, signal);
    }
    [[nodiscard]] Logic4Word signal_driving_value_word(
        const SignalId signal) const override
    {
        return signal_attribute_detail::driving_value(
            owner, process, signal)
            .low_word();
    }
    [[nodiscard]] Logic9Word signal_driving_value_logic9_word(
        const SignalId signal) const override
    {
        return signal_attribute_detail::driving_value(
            owner, process, signal)
            .logic9_low_word();
    }

    void request_channel_update(
        const std::uint64_t channel) override
    {
        owner.request_channel_update(process, channel);
    }

    void display(
        const std::string_view text,
        const bool newline) override
    {
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
        const bool newline) override
    {
        owner.scheduler.schedule(
            SchedulerPhase::postponed,
            process,
            [&owner = owner,
                process = process,
                text = std::string { text },
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

    [[nodiscard]] SimulationTick current_time() const noexcept override
    {
        return owner.scheduler.now();
    }
    [[nodiscard]] SystemVerilogTimeFormat
    systemverilog_time_format() const override
    {
        return owner.time_format;
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
        const SystemVerilogScalarKind scalar_kind) override
    {
        auto text = make_formatted_output(
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
        const bool zero_pad,
        const bool use_timeformat_width) override
    {
        auto text = make_time_output(
            prefix,
            suffix,
            owner.scheduler.now(),
            owner.time_format,
            use_timeformat_width,
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

    void install_monitor(const MonitorInstall& registration) override
    {
        owner.install_monitor(process, registration);
    }
    void set_monitor_enabled(const bool enabled) override
    {
        owner.set_monitor_enabled(enabled);
    }
    [[nodiscard]] PackedLogic4 random_value(
        const RandomKind kind,
        const std::optional<PackedLogic4>& maximum,
        const std::optional<PackedLogic4>& minimum) override
    {
        return owner.random_value(process, kind, maximum, minimum);
    }
    void report(const std::string_view message,
        const AssertionSeverity severity,
        const SourceLocation& source) override
    {
        if (!owner.report_hook)
            return;
        owner.report_hook(process, message, severity, source,
            owner.scheduler.now(), owner.scheduler.delta());
    }
    [[nodiscard]] Logic9 evaluate_vital_timing_check(
        const InstructionIndex instruction, const VitalTimingCheck& operation) override
    {
        return owner.execute_vital_timing_check(process, instruction, operation);
    }
    void execute_vital_delay(const InstructionIndex instruction,
        const VitalDelay& operation, const VitalDelayRuntimeValues& values) override
    {
        owner.execute_vital_delay(process, instruction, operation, values);
    }
    [[nodiscard]] bool execution_points_enabled() const noexcept override
    {
        return static_cast<bool>(owner.execution_point_hook);
    }
};

} // namespace fsim::runtime::simir
