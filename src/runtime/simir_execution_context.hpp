// SPDX-License-Identifier: Apache-2.0
// Internal execution-context declaration shared by the compiled execution units.
#pragma once

#include "simir_internal.hpp"
#include "simir_signal_attributes.hpp"

namespace fsim::runtime::simir {
struct Interpreter::Impl::ExecutionContext final
    : ProcessExecutionContext {
    Impl& owner;
    ProcessId process;
    ExecutionContext(Impl& owner_value, const ProcessId process_value);
    [[nodiscard]] std::uint64_t static_trigger_mask() const noexcept override;
    [[nodiscard]] PackedLogic4
    read_signal(const SignalId signal) const override;
    [[nodiscard]] Logic4Word
    read_signal_word(const SignalId signal) const override;
    [[nodiscard]] std::span<const std::uint64_t>
    direct_signal_aval() const noexcept override;
    [[nodiscard]] std::span<const std::uint64_t>
    direct_signal_bval() const noexcept override;
    [[nodiscard]] std::span<std::uint64_t>
    direct_code_coverage_counters() noexcept override;
    [[nodiscard]] CodeCoverageCounterRuntimeStatus record_code_coverage_counter(
        const ::fsim::runtime::CodeCoverageCounterId counter) override;
    [[nodiscard]] std::span<const std::uint64_t>
    direct_signal_logic9_plane0() const noexcept override;
    [[nodiscard]] std::span<const std::uint64_t>
    direct_signal_logic9_plane1() const noexcept override;
    [[nodiscard]] std::span<const std::uint64_t>
    direct_signal_logic9_plane2() const noexcept override;
    [[nodiscard]] std::span<const std::uint64_t>
    direct_signal_logic9_plane3() const noexcept override;
    [[nodiscard]] std::span<const std::uint64_t>
    direct_wide_signal_aval() const noexcept override;
    [[nodiscard]] std::span<const std::uint64_t>
    direct_wide_signal_bval() const noexcept override;
    [[nodiscard]] std::span<const std::uint64_t>
    direct_wide_signal_logic9_plane2() const noexcept override;
    [[nodiscard]] std::span<const std::uint64_t>
    direct_wide_signal_logic9_plane3() const noexcept override;
    [[nodiscard]] std::span<const std::uint32_t>
    direct_wide_signal_offsets() const noexcept override;
    [[nodiscard]] std::span<const ProcessId>
    direct_single_driver_processes() const noexcept override;
    [[nodiscard]] std::span<const ProcessId>
    stable_single_writer_processes() const noexcept override;
    [[nodiscard]] std::uint64_t
    signal_writer_revision() const noexcept override;
    void read_signal_planes(
        const SignalId signal,
        const std::span<std::uint64_t> aval,
        const std::span<std::uint64_t> bval,
        const std::span<std::uint64_t> logic9_plane2,
        const std::span<std::uint64_t> logic9_plane3) const override;
    [[nodiscard]] std::string
    read_string_object(const StringObjectId object) const override;
    void write_string_object(
        const StringObjectId object,
        const std::string_view value) override;
    [[nodiscard]] ContainerValue read_container_object(
        const ContainerObjectId object) const override;
    [[nodiscard]] bool copy_container_object(
        const ContainerObjectId object,
        ContainerValue& destination) const override;
    [[nodiscard]] const ContainerValue* borrow_container_object(
        const ContainerObjectId object) const override;
    [[nodiscard]] bool container_object_has_type(
        const ContainerObjectId object,
        const ContainerType& type) const override;
    [[nodiscard]] bool read_container_object_element(
        const ContainerObjectId object,
        const std::size_t ordinal,
        PackedLogic4& result) const override;
    void write_container_object(
        const ContainerObjectId object,
        const ContainerValue& value) override;
    void write_container_object_element(
        const ContainerObjectId object,
        const PackedLogic4& index,
        const bool signed_index,
        const bool linear_index,
        const PackedLogic4& value,
        const ProcessId generated_process,
        const InstructionIndex instruction,
        const bool nonblocking) override;
    [[nodiscard]] FileHandle open_file(
        const std::string_view path,
        const std::string_view mode) override;
    void close_file(const FileHandle handle) override;
    void write_file(
        const FileHandle handle,
        const std::string_view text,
        const bool newline) override;
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
        const SystemVerilogScalarKind scalar_kind) override;
    [[nodiscard]] std::string read_file_line(
        const FileHandle handle,
        std::uint32_t& count) override;
    [[nodiscard]] std::int32_t read_file_character(
        const FileHandle handle) override;
    [[nodiscard]] std::int32_t unread_file_character(
        const FileHandle handle,
        const std::int32_t character) override;
    [[nodiscard]] bool file_end_of_file(
        const FileHandle handle) override;
    [[nodiscard]] std::string file_error(
        const FileHandle handle,
        bool& has_error) override;
    [[nodiscard]] std::int32_t position_file(
        const FileHandle handle,
        const FilePositionKind kind,
        const std::int32_t offset,
        const std::int32_t origin) override;
    void flush_file(const std::optional<FileHandle> handle) override;
    [[nodiscard]] Logic9Word
    read_signal_logic9_word(const SignalId signal) const override;
    void write_blocking(
        const SignalId signal, PackedLogic4 value) override;
    void write_blocking_word(
        const SignalId signal,
        const Logic4Word value) override;
    void write_blocking_slice(
        const SignalId signal,
        PackedLogic4 value,
        const std::size_t offset) override;
    void write_blocking_slice_word(
        const SignalId signal,
        const Logic4Word value,
        const std::uint32_t offset) override;
    void force_signal_slice(
        const SignalId signal,
        PackedLogic4 value,
        const std::size_t offset) override;
    void release_signal_slice(
        const SignalId signal,
        const std::size_t offset,
        const std::size_t width) override;
    void force_driver_signal_slice(
        const SignalId signal,
        PackedLogic4 value,
        const std::size_t offset) override;
    void release_driver_signal_slice(
        const SignalId signal,
        const std::size_t offset,
        const std::size_t width) override;

    void write_update(
        const SignalId signal, PackedLogic4 value) override;

    void write_update_word(
        const SignalId signal,
        const Logic4Word value) override;

    void write_update_slice(
        const SignalId signal,
        PackedLogic4 value,
        const std::size_t offset) override;

    void write_update_slice_word(
        const SignalId signal,
        const Logic4Word value,
        const std::uint32_t offset) override;

    void write_update_words(
        const std::span<const ProcessUpdateWord> updates) override;

    void write_validated_update_words(
        const std::span<const ProcessUpdateWord> updates) override;

    [[nodiscard]] const void* direct_update_domain() const noexcept override;

    bool write_validated_update_slot_batches(
        const std::span<const ProcessUpdateSlotBatch> batches) override;

    bool write_validated_logic9_update_batch(
        const ProcessLogic9UpdateBatch& batch) override;

    bool write_validated_logic9_update_batches(
        const std::span<const ProcessLogic9UpdateBatch> batches) override;

    [[nodiscard]] bool supports_direct_word_updates() const noexcept override;

    void write_after(
        const SignalId signal,
        PackedLogic4 value,
        const SimulationTick delay) override;

    void write_after_word(
        const SignalId signal,
        const Logic4Word value,
        const SimulationTick delay) override;

    void write_after_slice(
        const SignalId signal,
        PackedLogic4 value,
        const std::size_t offset,
        const SimulationTick delay) override;

    void write_after_slice_word(
        const SignalId signal,
        const Logic4Word value,
        const std::uint32_t offset,
        const SimulationTick delay) override;

    void write_inertial(
        const SignalId signal,
        PackedLogic4 value,
        const TransitionDelays& delays) override;

    void write_inertial_slice(
        const SignalId signal,
        PackedLogic4 value,
        const std::size_t offset,
        const TransitionDelays& delays) override;

    void write_projected(
        const SignalId signal,
        PackedLogic4 value,
        const SimulationTick delay,
        const SimulationTick rejection,
        const ProjectedDelayMode mode) override;

    void write_projected_slice(
        const SignalId signal,
        PackedLogic4 value,
        const std::size_t offset,
        const SimulationTick delay,
        const SimulationTick rejection,
        const ProjectedDelayMode mode) override;

    void write_projected_waveform(
        const SignalId signal,
        std::vector<ProjectedWaveformValue> elements,
        const SimulationTick rejection,
        const ProjectedDelayMode mode) override;

    void write_projected_waveform_slice(
        const SignalId signal,
        std::vector<ProjectedWaveformValue> elements,
        const std::size_t offset,
        const SimulationTick rejection,
        const ProjectedDelayMode mode) override;

    void notify_event(
        const SignalId event,
        const SimulationTick delay,
        const EventNotificationKind kind) override;

    void cancel_event(const SignalId event) override;

    [[nodiscard]] bool
    signal_event(const SignalId signal) const override;

    [[nodiscard]] Logic4Word
    signal_last_value_word(const SignalId signal) const override;

    [[nodiscard]] Logic9Word
    signal_last_value_logic9_word(
        const SignalId signal) const override;

    [[nodiscard]] SimulationTick
    signal_last_event(const SignalId signal) const override;

    [[nodiscard]] bool
    signal_active(const SignalId signal) const override;

    [[nodiscard]] SimulationTick signal_last_active(
        const SignalId signal) const override;
    [[nodiscard]] bool signal_driving(
        const SignalId signal) const override;
    [[nodiscard]] Logic4Word signal_driving_value_word(
        const SignalId signal) const override;
    [[nodiscard]] Logic9Word signal_driving_value_logic9_word(
        const SignalId signal) const override;

    void request_channel_update(
        const std::uint64_t channel) override;

    void display(
        const std::string_view text,
        const bool newline) override;

    void postpone_display(
        const std::string_view text,
        const bool newline) override;

    [[nodiscard]] SimulationTick current_time() const noexcept override;
    [[nodiscard]] SystemVerilogTimeFormat
    systemverilog_time_format() const override;

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
        const SystemVerilogScalarKind scalar_kind) override;

    void display_time(
        const std::string_view prefix,
        const std::string_view suffix,
        const bool newline,
        const bool postponed,
        const std::uint32_t minimum_width,
        const bool left_justify,
        const bool zero_pad,
        const bool use_timeformat_width) override;

    void install_monitor(const MonitorInstall& registration) override;
    void set_monitor_enabled(const bool enabled) override;
    [[nodiscard]] PackedLogic4 random_value(
        const RandomKind kind,
        const std::optional<PackedLogic4>& maximum,
        const std::optional<PackedLogic4>& minimum) override;
    void report(const std::string_view message,
        const AssertionSeverity severity,
        const SourceLocation& source) override;
    void vhdl_report(
        const InstructionIndex instruction,
        const std::string_view message,
        const AssertionSeverity severity,
        const SourceLocation& source,
        const bool standalone) override;
    [[nodiscard]] Logic9 evaluate_vital_timing_check(
        const InstructionIndex instruction, const VitalTimingCheck& operation) override;
    void execute_vital_delay(const InstructionIndex instruction,
        const VitalDelay& operation, const VitalDelayRuntimeValues& values) override;
    [[nodiscard]] bool execution_points_enabled() const noexcept override;
};

} // namespace fsim::runtime::simir
