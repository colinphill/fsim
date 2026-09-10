// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/simir.hpp"

namespace fsim::runtime::simir {

[[nodiscard]] std::uint64_t ProcessExecutionContext::static_trigger_mask() const noexcept
{
    return Process::full_static_trigger_mask;
}

[[nodiscard]] bool ProcessExecutionContext::supports_direct_word_updates() const noexcept
{
    return false;
}

[[nodiscard]] std::string
ProcessExecutionContext::read_string_object(StringObjectId) const
{
    throw std::logic_error {
        "alternate process executor cannot read string objects"
    };
}

void ProcessExecutionContext::write_string_object(StringObjectId, std::string_view)
{
    throw std::logic_error {
        "alternate process executor cannot write string objects"
    };
}

[[nodiscard]] ContainerValue
ProcessExecutionContext::read_container_object(ContainerObjectId) const
{
    throw std::logic_error {
        "alternate process executor does not support container objects"
    };
}

[[nodiscard]] bool ProcessExecutionContext::copy_container_object(
    const ContainerObjectId object,
    ContainerValue& destination) const
{
    auto source = read_container_object(object);
    if (destination.type != source.type) {
        return false;
    }
    destination = std::move(source);
    return true;
}

[[nodiscard]] const ContainerValue*
ProcessExecutionContext::borrow_container_object(ContainerObjectId) const
{
    return nullptr;
}

[[nodiscard]] bool ProcessExecutionContext::container_object_has_type(
    const ContainerObjectId object,
    const ContainerType& type) const
{
    return read_container_object(object).type == type;
}

[[nodiscard]] bool ProcessExecutionContext::read_container_object_element(
    ContainerObjectId,
    std::size_t,
    PackedLogic4&) const
{
    return false;
}

void ProcessExecutionContext::write_container_object(
    ContainerObjectId, const ContainerValue&)
{
    throw std::logic_error {
        "alternate process executor does not support container objects"
    };
}

void ProcessExecutionContext::write_container_object_element(
    ContainerObjectId,
    const PackedLogic4&,
    bool,
    bool,
    const PackedLogic4&,
    ProcessId,
    InstructionIndex,
    bool)
{
    throw std::logic_error {
        "alternate process executor does not support direct container-object element writes"
    };
}

[[nodiscard]] FileHandle ProcessExecutionContext::open_file(
    std::string_view, std::string_view)
{
    throw std::logic_error {
        "alternate process executor does not support file open"
    };
}

void ProcessExecutionContext::close_file(FileHandle)
{
    throw std::logic_error {
        "alternate process executor does not support file close"
    };
}

void ProcessExecutionContext::write_file(
    FileHandle, std::string_view, bool)
{
    throw std::logic_error {
        "alternate process executor does not support file writes"
    };
}

void ProcessExecutionContext::write_file_formatted(
    FileHandle,
    std::string_view,
    std::string_view,
    OutputFormat,
    const PackedLogic4&,
    bool,
    bool,
    std::uint32_t,
    bool,
    bool,
    SystemVerilogScalarKind)
{
    throw std::logic_error {
        "alternate process executor does not support formatted file writes"
    };
}

[[nodiscard]] std::string ProcessExecutionContext::read_file_line(
    FileHandle, std::uint32_t&)
{
    throw std::logic_error { "alternate process executor does not support file reads" };
}

[[nodiscard]] std::int32_t ProcessExecutionContext::read_file_character(FileHandle)
{
    throw std::logic_error { "alternate process executor does not support character reads" };
}

[[nodiscard]] std::int32_t ProcessExecutionContext::unread_file_character(
    FileHandle, std::int32_t)
{
    throw std::logic_error { "alternate process executor does not support character pushback" };
}

[[nodiscard]] bool ProcessExecutionContext::file_end_of_file(FileHandle)
{
    throw std::logic_error { "alternate process executor does not support file status" };
}

[[nodiscard]] std::string ProcessExecutionContext::file_error(
    FileHandle, bool&)
{
    throw std::logic_error {
        "alternate process executor does not support file errors"
    };
}

[[nodiscard]] std::int32_t ProcessExecutionContext::position_file(
    FileHandle, FilePositionKind, std::int32_t, std::int32_t)
{
    throw std::logic_error {
        "alternate process executor does not support file positioning"
    };
}

void ProcessExecutionContext::flush_file(std::optional<FileHandle>)
{
    throw std::logic_error {
        "alternate process executor does not support file flushing"
    };
}

[[nodiscard]] Logic4Word
ProcessExecutionContext::read_signal_word(SignalId signal) const
{
    return read_signal(signal).low_word();
}

[[nodiscard]] std::span<const std::uint64_t>
ProcessExecutionContext::direct_signal_aval() const noexcept
{
    return { };
}

[[nodiscard]] std::span<const std::uint64_t>
ProcessExecutionContext::direct_signal_bval() const noexcept
{
    return { };
}

[[nodiscard]] std::span<std::uint64_t>
ProcessExecutionContext::direct_code_coverage_counters() noexcept
{
    return { };
}

[[nodiscard]] CodeCoverageCounterRuntimeStatus
    ProcessExecutionContext::record_code_coverage_counter(::fsim::runtime::CodeCoverageCounterId)
{
    return CodeCoverageCounterRuntimeStatus::Unavailable;
}

[[nodiscard]] std::span<const std::uint64_t>
ProcessExecutionContext::direct_signal_logic9_plane0() const noexcept
{
    return { };
}

[[nodiscard]] std::span<const std::uint64_t>
ProcessExecutionContext::direct_signal_logic9_plane1() const noexcept
{
    return { };
}

[[nodiscard]] std::span<const std::uint64_t>
ProcessExecutionContext::direct_signal_logic9_plane2() const noexcept
{
    return { };
}

[[nodiscard]] std::span<const std::uint64_t>
ProcessExecutionContext::direct_signal_logic9_plane3() const noexcept
{
    return { };
}

[[nodiscard]] std::span<const std::uint64_t>
ProcessExecutionContext::direct_wide_signal_aval() const noexcept
{
    return { };
}

[[nodiscard]] std::span<const std::uint64_t>
ProcessExecutionContext::direct_wide_signal_bval() const noexcept
{
    return { };
}

[[nodiscard]] std::span<const std::uint64_t>
ProcessExecutionContext::direct_wide_signal_logic9_plane2() const noexcept
{
    return { };
}

[[nodiscard]] std::span<const std::uint64_t>
ProcessExecutionContext::direct_wide_signal_logic9_plane3() const noexcept
{
    return { };
}

[[nodiscard]] std::span<const std::uint32_t>
ProcessExecutionContext::direct_wide_signal_offsets() const noexcept
{
    return { };
}

[[nodiscard]] std::span<const ProcessId>
ProcessExecutionContext::direct_single_driver_processes() const noexcept
{
    return { };
}

[[nodiscard]] std::span<const ProcessId>
ProcessExecutionContext::stable_single_writer_processes() const noexcept
{
    return { };
}

[[nodiscard]] std::uint64_t
ProcessExecutionContext::signal_writer_revision() const noexcept
{
    return 0U;
}

[[nodiscard]] Logic9Word
ProcessExecutionContext::read_signal_logic9_word(SignalId signal) const
{
    return read_signal(signal).logic9_low_word();
}

void ProcessExecutionContext::read_signal_planes(
    SignalId signal,
    std::span<std::uint64_t> aval,
    std::span<std::uint64_t> bval,
    std::span<std::uint64_t> logic9_plane2,
    std::span<std::uint64_t> logic9_plane3) const
{
    const auto value = read_signal(signal);
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

void ProcessExecutionContext::write_blocking_word(
    SignalId signal, const Logic4Word value)
{
    write_blocking(
        signal,
        PackedLogic4::from_aval_bval(
            value.width, value.aval, value.bval));
}

void ProcessExecutionContext::write_blocking_slice_word(
    SignalId signal,
    const Logic4Word value,
    std::uint32_t offset)
{
    write_blocking_slice(
        signal,
        PackedLogic4::from_aval_bval(
            value.width, value.aval, value.bval),
        offset);
}

void ProcessExecutionContext::force_signal_slice(
    SignalId,
    PackedLogic4,
    std::size_t)
{
    throw std::logic_error {
        "alternate process executor does not support procedural force"
    };
}

void ProcessExecutionContext::release_signal_slice(
    SignalId,
    std::size_t,
    std::size_t)
{
    throw std::logic_error {
        "alternate process executor does not support procedural release"
    };
}

void ProcessExecutionContext::force_driver_signal_slice(
    SignalId,
    PackedLogic4,
    std::size_t)
{
    throw std::logic_error {
        "alternate process executor does not support driver-value force"
    };
}

void ProcessExecutionContext::release_driver_signal_slice(
    SignalId,
    std::size_t,
    std::size_t)
{
    throw std::logic_error {
        "alternate process executor does not support driver-value release"
    };
}

void ProcessExecutionContext::write_update_word(
    SignalId signal, const Logic4Word value)
{
    write_update(
        signal,
        PackedLogic4::from_aval_bval(
            value.width, value.aval, value.bval));
}

void ProcessExecutionContext::write_update_slice_word(
    SignalId signal,
    const Logic4Word value,
    std::uint32_t offset)
{
    write_update_slice(
        signal,
        PackedLogic4::from_aval_bval(
            value.width, value.aval, value.bval),
        offset);
}

void ProcessExecutionContext::write_update_words(
    const std::span<const ProcessUpdateWord> updates)
{
    for (const auto& update : updates) {
        if (update.slice) {
            write_update_slice_word(
                update.signal, update.value, update.offset);
        } else {
            write_update_word(update.signal, update.value);
        }
    }
}

void ProcessExecutionContext::write_validated_update_words(
    const std::span<const ProcessUpdateWord> updates)
{
    write_update_words(updates);
}

[[nodiscard]] const void*
ProcessExecutionContext::direct_update_domain() const noexcept
{
    return nullptr;
}

bool ProcessExecutionContext::write_validated_update_slot_batches(
    std::span<const ProcessUpdateSlotBatch>)
{
    return false;
}

bool ProcessExecutionContext::write_validated_logic9_update_batch(
    const ProcessLogic9UpdateBatch&)
{
    return false;
}

bool ProcessExecutionContext::write_validated_logic9_update_batches(
    std::span<const ProcessLogic9UpdateBatch>)
{
    return false;
}

void ProcessExecutionContext::write_after_word(
    SignalId signal, const Logic4Word value,
    SimulationTick delay)
{
    write_after(
        signal,
        PackedLogic4::from_aval_bval(
            value.width, value.aval, value.bval),
        delay);
}

void ProcessExecutionContext::write_after_slice_word(
    SignalId signal,
    const Logic4Word value,
    std::uint32_t offset,
    SimulationTick delay)
{
    write_after_slice(
        signal,
        PackedLogic4::from_aval_bval(
            value.width, value.aval, value.bval),
        offset,
        delay);
}

void ProcessExecutionContext::write_inertial_word(
    SignalId signal,
    const Logic4Word value,
    const TransitionDelays& delays)
{
    write_inertial(
        signal,
        PackedLogic4::from_aval_bval(
            value.width, value.aval, value.bval),
        delays);
}

void ProcessExecutionContext::write_inertial_slice_word(
    SignalId signal,
    const Logic4Word value,
    std::uint32_t offset,
    const TransitionDelays& delays)
{
    write_inertial_slice(
        signal,
        PackedLogic4::from_aval_bval(
            value.width, value.aval, value.bval),
        offset,
        delays);
}

void ProcessExecutionContext::write_projected(
    SignalId signal,
    PackedLogic4 value,
    SimulationTick delay,
    SimulationTick rejection,
    ProjectedDelayMode mode)
{
    (void)signal;
    (void)value;
    (void)delay;
    (void)rejection;
    (void)mode;
    throw std::logic_error {
        "alternate process executor does not support projected writes"
    };
}

void ProcessExecutionContext::write_projected_word(
    SignalId signal,
    const Logic4Word value,
    SimulationTick delay,
    SimulationTick rejection,
    ProjectedDelayMode mode)
{
    write_projected(
        signal,
        PackedLogic4::from_aval_bval(
            value.width, value.aval, value.bval),
        delay,
        rejection,
        mode);
}

void ProcessExecutionContext::write_projected_slice(
    SignalId signal,
    PackedLogic4 value,
    std::size_t offset,
    SimulationTick delay,
    SimulationTick rejection,
    ProjectedDelayMode mode)
{
    (void)signal;
    (void)value;
    (void)offset;
    (void)delay;
    (void)rejection;
    (void)mode;
    throw std::logic_error {
        "alternate process executor does not support projected slice writes"
    };
}

void ProcessExecutionContext::write_projected_slice_word(
    SignalId signal,
    const Logic4Word value,
    std::uint32_t offset,
    SimulationTick delay,
    SimulationTick rejection,
    ProjectedDelayMode mode)
{
    write_projected_slice(
        signal,
        PackedLogic4::from_aval_bval(
            value.width, value.aval, value.bval),
        offset,
        delay,
        rejection,
        mode);
}

void ProcessExecutionContext::write_projected_waveform(
    SignalId signal,
    std::vector<ProjectedWaveformValue> elements,
    SimulationTick rejection,
    ProjectedDelayMode mode)
{
    (void)signal;
    (void)elements;
    (void)rejection;
    (void)mode;
    throw std::logic_error {
        "alternate process executor does not support projected waveforms"
    };
}

void ProcessExecutionContext::write_projected_waveform_slice(
    SignalId signal,
    std::vector<ProjectedWaveformValue> elements,
    std::size_t offset,
    SimulationTick rejection,
    ProjectedDelayMode mode)
{
    (void)signal;
    (void)elements;
    (void)offset;
    (void)rejection;
    (void)mode;
    throw std::logic_error {
        "alternate process executor does not support projected slice "
        "waveforms"
    };
}

void ProcessExecutionContext::notify_event(
    SignalId,
    SimulationTick,
    EventNotificationKind)
{
    throw std::logic_error {
        "alternate process executor does not support event notification"
    };
}

void ProcessExecutionContext::cancel_event(SignalId)
{
    throw std::logic_error {
        "alternate process executor does not support event cancellation"
    };
}

[[nodiscard]] bool ProcessExecutionContext::signal_event(SignalId) const
{
    return false;
}

[[nodiscard]] Logic4Word ProcessExecutionContext::signal_last_value_word(SignalId) const
{
    throw std::logic_error {
        "alternate process executor does not support signal last-value reads"
    };
}

[[nodiscard]] Logic9Word
ProcessExecutionContext::signal_last_value_logic9_word(SignalId) const
{
    throw std::logic_error {
        "alternate process executor does not support exact signal "
        "last-value reads"
    };
}

[[nodiscard]] SimulationTick ProcessExecutionContext::signal_last_event(SignalId) const
{
    return std::numeric_limits<SimulationTick>::max();
}

[[nodiscard]] bool ProcessExecutionContext::signal_active(SignalId) const
{
    return false;
}

[[nodiscard]] SimulationTick ProcessExecutionContext::signal_last_active(SignalId) const
{
    return std::numeric_limits<SimulationTick>::max();
}

[[nodiscard]] bool ProcessExecutionContext::signal_driving(SignalId) const
{
    return false;
}

[[nodiscard]] Logic4Word ProcessExecutionContext::signal_driving_value_word(SignalId) const
{
    throw std::logic_error {
        "alternate process executor does not support signal driving-value reads"
    };
}

[[nodiscard]] Logic9Word
ProcessExecutionContext::signal_driving_value_logic9_word(SignalId) const
{
    throw std::logic_error {
        "alternate process executor does not support exact signal "
        "driving-value reads"
    };
}

void ProcessExecutionContext::request_channel_update(std::uint64_t)
{
    throw std::logic_error {
        "alternate process executor does not support channel updates"
    };
}

void ProcessExecutionContext::display(std::string_view, bool)
{
}

void ProcessExecutionContext::postpone_display(std::string_view, bool)
{
}

[[nodiscard]] SimulationTick ProcessExecutionContext::current_time() const noexcept
{
    return 0;
}

[[nodiscard]] SystemVerilogTimeFormat
ProcessExecutionContext::systemverilog_time_format() const
{
    return { };
}

void ProcessExecutionContext::display_formatted(
    std::string_view,
    std::string_view,
    OutputFormat,
    const PackedLogic4&,
    bool,
    bool,
    bool,
    bool,
    std::uint32_t,
    bool,
    bool,
    SystemVerilogScalarKind)
{
}

void ProcessExecutionContext::display_time(
    std::string_view,
    std::string_view,
    bool,
    bool,
    std::uint32_t,
    bool,
    bool,
    bool)
{
}

void ProcessExecutionContext::install_monitor(const MonitorInstall&)
{
}

void ProcessExecutionContext::set_monitor_enabled(bool)
{
}

[[nodiscard]] PackedLogic4 ProcessExecutionContext::random_value(
    RandomKind,
    const std::optional<PackedLogic4>&,
    const std::optional<PackedLogic4>&)
{
    throw std::logic_error {
        "alternate process executor does not support random values"
    };
}

void ProcessExecutionContext::report(
    std::string_view,
    AssertionSeverity,
    const SourceLocation&)
{
}

void ProcessExecutionContext::vhdl_report(
    InstructionIndex instruction,
    std::string_view message,
    AssertionSeverity severity,
    const SourceLocation& source,
    bool standalone)
{
    (void)instruction;
    (void)standalone;
    report(message, severity, source);
}

[[nodiscard]] Logic9 ProcessExecutionContext::evaluate_vital_timing_check(
    InstructionIndex,
    const VitalTimingCheck&)
{
    throw std::logic_error {
        "alternate process executor does not support VITAL timing checks"
    };
}

void ProcessExecutionContext::execute_vital_delay(
    InstructionIndex,
    const VitalDelay&,
    const VitalDelayRuntimeValues&)
{
    throw std::logic_error {
        "alternate process executor does not support VITAL delays"
    };
}

[[nodiscard]] bool ProcessExecutionContext::execution_points_enabled() const noexcept
{
    return false;
}

} // namespace fsim::runtime::simir
