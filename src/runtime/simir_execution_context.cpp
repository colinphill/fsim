// SPDX-License-Identifier: Apache-2.0
#include "simir_execution_context.hpp"
#include "fsim/runtime/systemverilog_string.hpp"

namespace fsim::runtime::simir {

Interpreter::Impl::PendingUpdate::PendingUpdate(
    const SignalId signal_value,
    const std::optional<ProcessId> driver_value,
    const std::optional<std::size_t> offset_value,
    const Logic4Word word_value,
    const std::optional<std::size_t> packed_value_index)
    : signal(signal_value)
    , driver(driver_value.value_or(0U))
    , offset(static_cast<std::uint32_t>(offset_value.value_or(0U)))
    , packed_value(static_cast<std::uint32_t>(
          packed_value_index.value_or(0U)))
    , word(word_value)
    , flags(static_cast<std::uint8_t>(
          (driver_value ? static_cast<std::uint8_t>(has_driver)
                        : std::uint8_t { })
          | (offset_value ? static_cast<std::uint8_t>(has_offset)
                          : std::uint8_t { })
          | (packed_value_index
                  ? static_cast<std::uint8_t>(has_packed_value)
                  : std::uint8_t { })))
{
    if ((offset_value
            && *offset_value > std::numeric_limits<std::uint32_t>::max())
        || (packed_value_index
            && *packed_value_index
                > std::numeric_limits<std::uint32_t>::max())) {
        throw std::length_error(
            "pending update metadata exceeds its compact representation");
    }
}

Interpreter::Impl::ExecutionContext::ExecutionContext(
    Impl& owner_value, const ProcessId process_value)
    : owner(owner_value)
    , process(process_value)
{
}

[[nodiscard]] std::uint64_t Interpreter::Impl::ExecutionContext::static_trigger_mask() const noexcept{
    return owner.processes[process].static_trigger_mask;
}

[[nodiscard]] PackedLogic4
Interpreter::Impl::ExecutionContext::read_signal(const SignalId signal) const{
    return owner.get_signal(signal).initial_value;
}

[[nodiscard]] Logic4Word
Interpreter::Impl::ExecutionContext::read_signal_word(const SignalId signal) const{
    // Native lowering selects this callback only for validated nonempty
    // Logic4 signals no wider than one ABI word. Avoid repeating the
    // public checked conversion on every generated signal read.
    return owner.get_signal(signal).initial_value.unchecked_low_word();
}

[[nodiscard]] std::span<const std::uint64_t>
Interpreter::Impl::ExecutionContext::direct_signal_aval() const noexcept{
    return owner.direct_signal_aval;
}

[[nodiscard]] std::span<const std::uint64_t>
Interpreter::Impl::ExecutionContext::direct_signal_bval() const noexcept{
    return owner.direct_signal_bval;
}

[[nodiscard]] std::span<std::uint64_t>
Interpreter::Impl::ExecutionContext::direct_code_coverage_counters() noexcept{
    return owner.code_coverage_counters.direct_values();
}

[[nodiscard]] CodeCoverageCounterRuntimeStatus Interpreter::Impl::ExecutionContext::record_code_coverage_counter(
    const ::fsim::runtime::CodeCoverageCounterId counter){
    const auto update = owner.code_coverage_counters.record(counter);
    if (update == CodeCoverageCounterUpdate::Unavailable) {
        return CodeCoverageCounterRuntimeStatus::Unavailable;
    }
    if (update == CodeCoverageCounterUpdate::OutOfRange) {
        return CodeCoverageCounterRuntimeStatus::OutOfRange;
    }
    if (update == CodeCoverageCounterUpdate::Ignored) {
        return CodeCoverageCounterRuntimeStatus::Recorded;
    }
    if (update == CodeCoverageCounterUpdate::FirstOverflow
        && owner.code_coverage_overflow_hook) {
        owner.code_coverage_overflow_hook(counter);
    }
    return CodeCoverageCounterRuntimeStatus::Recorded;
}

[[nodiscard]] std::span<const std::uint64_t>
Interpreter::Impl::ExecutionContext::direct_signal_logic9_plane0() const noexcept{
    return owner.direct_signal_logic9_plane0;
}

[[nodiscard]] std::span<const std::uint64_t>
Interpreter::Impl::ExecutionContext::direct_signal_logic9_plane1() const noexcept{
    return owner.direct_signal_logic9_plane1;
}

[[nodiscard]] std::span<const std::uint64_t>
Interpreter::Impl::ExecutionContext::direct_signal_logic9_plane2() const noexcept{
    return owner.direct_signal_logic9_plane2;
}

[[nodiscard]] std::span<const std::uint64_t>
Interpreter::Impl::ExecutionContext::direct_signal_logic9_plane3() const noexcept{
    return owner.direct_signal_logic9_plane3;
}

[[nodiscard]] std::span<const std::uint64_t>
Interpreter::Impl::ExecutionContext::direct_wide_signal_aval() const noexcept{
    return owner.direct_wide_signal_aval;
}

[[nodiscard]] std::span<const std::uint64_t>
Interpreter::Impl::ExecutionContext::direct_wide_signal_bval() const noexcept{
    return owner.direct_wide_signal_bval;
}

[[nodiscard]] std::span<const std::uint64_t>
Interpreter::Impl::ExecutionContext::direct_wide_signal_logic9_plane2() const noexcept{
    return owner.direct_wide_signal_logic9_plane2;
}

[[nodiscard]] std::span<const std::uint64_t>
Interpreter::Impl::ExecutionContext::direct_wide_signal_logic9_plane3() const noexcept{
    return owner.direct_wide_signal_logic9_plane3;
}

[[nodiscard]] std::span<const std::uint32_t>
Interpreter::Impl::ExecutionContext::direct_wide_signal_offsets() const noexcept{
    return owner.direct_wide_signal_offsets;
}

[[nodiscard]] std::span<const ProcessId>
Interpreter::Impl::ExecutionContext::direct_single_driver_processes() const noexcept{
    return owner.direct_single_driver_processes;
}

[[nodiscard]] std::span<const ProcessId>
Interpreter::Impl::ExecutionContext::stable_single_writer_processes() const noexcept{
    return owner.stable_single_writer_processes;
}

[[nodiscard]] std::uint64_t
Interpreter::Impl::ExecutionContext::signal_writer_revision() const noexcept{
    return owner.signal_writer_revision;
}

void Interpreter::Impl::ExecutionContext::read_signal_planes(
    const SignalId signal,
    const std::span<std::uint64_t> aval,
    const std::span<std::uint64_t> bval,
    const std::span<std::uint64_t> logic9_plane2,
    const std::span<std::uint64_t> logic9_plane3) const{
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
Interpreter::Impl::ExecutionContext::read_string_object(const StringObjectId object) const{
    return owner.get_string_object(object).initial_value;
}

void Interpreter::Impl::ExecutionContext::write_string_object(
    const StringObjectId object,
    const std::string_view value){
    if (value.size() > maximum_string_bytes) {
        throw std::length_error {
            "SimIR string object exceeds byte limit"
        };
    }
    (void)systemverilog_string_length(value);
    owner.get_string_object(object).initial_value = value;
}

[[nodiscard]] ContainerValue Interpreter::Impl::ExecutionContext::read_container_object(
    const ContainerObjectId object) const{
    return owner.read_container_object_value(object);
}

[[nodiscard]] bool Interpreter::Impl::ExecutionContext::copy_container_object(
    const ContainerObjectId object,
    ContainerValue& destination) const{
    const auto& source = owner.read_container_object_value(object);
    if (destination.type != source.type) {
        return false;
    }
    destination = source;
    return true;
}

[[nodiscard]] const ContainerValue* Interpreter::Impl::ExecutionContext::borrow_container_object(
    const ContainerObjectId object) const{
    return &owner.read_container_object_value(object);
}

[[nodiscard]] bool Interpreter::Impl::ExecutionContext::container_object_has_type(
    const ContainerObjectId object,
    const ContainerType& type) const{
    return owner.get_container_object(object).initial_value.type == type;
}

[[nodiscard]] bool Interpreter::Impl::ExecutionContext::read_container_object_element(
    const ContainerObjectId object,
    const std::size_t ordinal,
    PackedLogic4& result) const{
    return owner.read_container_object_element(object, ordinal, result);
}

void Interpreter::Impl::ExecutionContext::write_container_object(
    const ContainerObjectId object,
    const ContainerValue& value){
    owner.write_container_object_value(object, value);
}

void Interpreter::Impl::ExecutionContext::write_container_object_element(
    const ContainerObjectId object,
    const PackedLogic4& index,
    const bool signed_index,
    const bool linear_index,
    const PackedLogic4& value,
    const ProcessId generated_process,
    const InstructionIndex instruction,
    const bool nonblocking){
    if (nonblocking) {
        owner.scheduler.schedule(
            SchedulerPhase::update,
            generated_process,
            [&owner = owner, object, index, signed_index, linear_index,
                value, generated_process, instruction](Scheduler&) {
                owner.write_container_object_element_value(
                    object, index, signed_index, linear_index, value,
                    generated_process, instruction);
            });
    } else {
        owner.write_container_object_element_value(
            object, index, signed_index, linear_index, value,
            generated_process, instruction);
    }
}

[[nodiscard]] FileHandle Interpreter::Impl::ExecutionContext::open_file(
    const std::string_view path,
    const std::string_view mode){
    return owner.open_file(process, path, mode);
}

void Interpreter::Impl::ExecutionContext::close_file(const FileHandle handle){
    owner.close_file(process, handle);
}

void Interpreter::Impl::ExecutionContext::write_file(
    const FileHandle handle,
    const std::string_view text,
    const bool newline){
    owner.write_file(process, handle, text, newline);
}

void Interpreter::Impl::ExecutionContext::write_file_formatted(
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
    const SystemVerilogScalarKind scalar_kind){
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

[[nodiscard]] std::string Interpreter::Impl::ExecutionContext::read_file_line(
    const FileHandle handle,
    std::uint32_t& count){
    return owner.read_file_line(process, handle, count);
}

[[nodiscard]] std::int32_t Interpreter::Impl::ExecutionContext::read_file_character(
    const FileHandle handle){
    return owner.read_file_character(process, handle);
}

[[nodiscard]] std::int32_t Interpreter::Impl::ExecutionContext::unread_file_character(
    const FileHandle handle,
    const std::int32_t character){
    return owner.unread_file_character(process, handle, character);
}

[[nodiscard]] bool Interpreter::Impl::ExecutionContext::file_end_of_file(
    const FileHandle handle){
    return owner.file_end_of_file(process, handle);
}

[[nodiscard]] std::string Interpreter::Impl::ExecutionContext::file_error(
    const FileHandle handle,
    bool& has_error){
    return owner.file_error(process, handle, has_error);
}

[[nodiscard]] std::int32_t Interpreter::Impl::ExecutionContext::position_file(
    const FileHandle handle,
    const FilePositionKind kind,
    const std::int32_t offset,
    const std::int32_t origin){
    return owner.position_file(process, handle, kind, offset, origin);
}

void Interpreter::Impl::ExecutionContext::flush_file(const std::optional<FileHandle> handle){
    owner.flush_file(process, handle);
}

[[nodiscard]] Logic9Word
Interpreter::Impl::ExecutionContext::read_signal_logic9_word(const SignalId signal) const{
    return owner.get_signal(signal).initial_value.logic9_low_word();
}

void Interpreter::Impl::ExecutionContext::write_blocking(
    const SignalId signal, PackedLogic4 value){
    owner.commit_driver(process, signal, std::move(value));
}

void Interpreter::Impl::ExecutionContext::write_blocking_word(
    const SignalId signal,
    const Logic4Word value){
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

void Interpreter::Impl::ExecutionContext::write_blocking_slice(
    const SignalId signal,
    PackedLogic4 value,
    const std::size_t offset){
    owner.commit_driver_slice(
        process, signal, std::move(value), offset);
}

void Interpreter::Impl::ExecutionContext::write_blocking_slice_word(
    const SignalId signal,
    const Logic4Word value,
    const std::uint32_t offset){
    owner.commit_driver_slice(
        process,
        signal,
        PackedLogic4::from_aval_bval(
            value.width, value.aval, value.bval),
        offset);
}

void Interpreter::Impl::ExecutionContext::force_signal_slice(
    const SignalId signal,
    PackedLogic4 value,
    const std::size_t offset){
    owner.force_slice(signal, std::move(value), offset);
}

void Interpreter::Impl::ExecutionContext::release_signal_slice(
    const SignalId signal,
    const std::size_t offset,
    const std::size_t width){
    owner.release_slice(signal, offset, width);
}

void Interpreter::Impl::ExecutionContext::force_driver_signal_slice(
    const SignalId signal,
    PackedLogic4 value,
    const std::size_t offset){
    owner.force_driver_slice(
        process, signal, std::move(value), offset);
}

void Interpreter::Impl::ExecutionContext::release_driver_signal_slice(
    const SignalId signal,
    const std::size_t offset,
    const std::size_t width){
    owner.release_driver_slice(process, signal, offset, width);
}

void Interpreter::Impl::ExecutionContext::write_update(
    const SignalId signal, PackedLogic4 value){
    owner.stage_update(process, signal, std::move(value));
}

void Interpreter::Impl::ExecutionContext::write_update_word(
    const SignalId signal,
    const Logic4Word value){
    owner.stage_update(
        process,
        signal,
        PackedLogic4::from_aval_bval(
            value.width, value.aval, value.bval));
}

void Interpreter::Impl::ExecutionContext::write_update_slice(
    const SignalId signal,
    PackedLogic4 value,
    const std::size_t offset){
    owner.stage_update_slice(
        process, signal, std::move(value), offset);
}

void Interpreter::Impl::ExecutionContext::write_update_slice_word(
    const SignalId signal,
    const Logic4Word value,
    const std::uint32_t offset){
    owner.stage_update_slice(
        process,
        signal,
        PackedLogic4::from_aval_bval(
            value.width, value.aval, value.bval),
        offset);
}

void Interpreter::Impl::ExecutionContext::write_update_words(
    const std::span<const ProcessUpdateWord> updates){
    owner.stage_update_words(process, updates);
}

void Interpreter::Impl::ExecutionContext::write_validated_update_words(
    const std::span<const ProcessUpdateWord> updates){
    owner.stage_validated_update_words(process, updates);
}

[[nodiscard]] const void* Interpreter::Impl::ExecutionContext::direct_update_domain() const noexcept{
    return &owner;
}

bool Interpreter::Impl::ExecutionContext::write_validated_update_slot_batches(
    const std::span<const ProcessUpdateSlotBatch> batches){
    return owner.stage_validated_update_slot_batches(batches);
}

bool Interpreter::Impl::ExecutionContext::write_validated_logic9_update_batch(
    const ProcessLogic9UpdateBatch& batch){
    return owner.stage_validated_logic9_update_batch(batch);
}

bool Interpreter::Impl::ExecutionContext::write_validated_logic9_update_batches(
    const std::span<const ProcessLogic9UpdateBatch> batches){
    return owner.stage_validated_logic9_update_batches(batches);
}

[[nodiscard]] bool Interpreter::Impl::ExecutionContext::supports_direct_word_updates() const noexcept{
    return owner.module_paths.empty();
}

void Interpreter::Impl::ExecutionContext::write_after(
    const SignalId signal,
    PackedLogic4 value,
    const SimulationTick delay){
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

void Interpreter::Impl::ExecutionContext::write_after_word(
    const SignalId signal,
    const Logic4Word value,
    const SimulationTick delay){
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

void Interpreter::Impl::ExecutionContext::write_after_slice(
    const SignalId signal,
    PackedLogic4 value,
    const std::size_t offset,
    const SimulationTick delay){
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

void Interpreter::Impl::ExecutionContext::write_after_slice_word(
    const SignalId signal,
    const Logic4Word value,
    const std::uint32_t offset,
    const SimulationTick delay){
    write_after_slice(
        signal,
        PackedLogic4::from_aval_bval(
            value.width, value.aval, value.bval),
        offset,
        delay);
}

void Interpreter::Impl::ExecutionContext::write_inertial(
    const SignalId signal,
    PackedLogic4 value,
    const TransitionDelays& delays){
    owner.schedule_inertial(
        process,
        signal,
        std::move(value),
        std::nullopt,
        delays);
}

void Interpreter::Impl::ExecutionContext::write_inertial_slice(
    const SignalId signal,
    PackedLogic4 value,
    const std::size_t offset,
    const TransitionDelays& delays){
    owner.schedule_inertial(
        process,
        signal,
        std::move(value),
        offset,
        delays);
}

void Interpreter::Impl::ExecutionContext::write_projected(
    const SignalId signal,
    PackedLogic4 value,
    const SimulationTick delay,
    const SimulationTick rejection,
    const ProjectedDelayMode mode){
    owner.schedule_projected(
        process,
        signal,
        value,
        std::nullopt,
        delay,
        rejection,
        mode);
}

void Interpreter::Impl::ExecutionContext::write_projected_slice(
    const SignalId signal,
    PackedLogic4 value,
    const std::size_t offset,
    const SimulationTick delay,
    const SimulationTick rejection,
    const ProjectedDelayMode mode){
    owner.schedule_projected(
        process,
        signal,
        value,
        offset,
        delay,
        rejection,
        mode);
}

void Interpreter::Impl::ExecutionContext::write_projected_waveform(
    const SignalId signal,
    std::vector<ProjectedWaveformValue> elements,
    const SimulationTick rejection,
    const ProjectedDelayMode mode){
    owner.schedule_projected_waveform(
        process,
        signal,
        elements,
        std::nullopt,
        rejection,
        mode);
}

void Interpreter::Impl::ExecutionContext::write_projected_waveform_slice(
    const SignalId signal,
    std::vector<ProjectedWaveformValue> elements,
    const std::size_t offset,
    const SimulationTick rejection,
    const ProjectedDelayMode mode){
    owner.schedule_projected_waveform(
        process,
        signal,
        elements,
        offset,
        rejection,
        mode);
}

void Interpreter::Impl::ExecutionContext::notify_event(
    const SignalId event,
    const SimulationTick delay,
    const EventNotificationKind kind){
    owner.notify_event(event, delay, kind, process);
}

void Interpreter::Impl::ExecutionContext::cancel_event(const SignalId event){
    owner.cancel_event(event);
}

[[nodiscard]] bool
Interpreter::Impl::ExecutionContext::signal_event(const SignalId signal) const{
    (void)owner.get_signal(signal);
    const auto& event = owner.signal_events[signal];
    return event
        && event->first == owner.scheduler.now()
        && event->second == owner.scheduler.delta();
}

[[nodiscard]] Logic4Word
Interpreter::Impl::ExecutionContext::signal_last_value_word(const SignalId signal) const{
    (void)owner.get_signal(signal);
    return owner.signal_last_values[signal].low_word();
}

[[nodiscard]] Logic9Word
Interpreter::Impl::ExecutionContext::signal_last_value_logic9_word(
    const SignalId signal) const{
    (void)owner.get_signal(signal);
    return owner.signal_last_values[signal].logic9_low_word();
}

[[nodiscard]] SimulationTick
Interpreter::Impl::ExecutionContext::signal_last_event(const SignalId signal) const{
    (void)owner.get_signal(signal);
    const auto& event = owner.signal_events[signal];
    return event
        ? owner.scheduler.now() - event->first
        : std::numeric_limits<SimulationTick>::max();
}

[[nodiscard]] bool
Interpreter::Impl::ExecutionContext::signal_active(const SignalId signal) const{
    (void)owner.get_signal(signal);
    const auto& transaction = owner.signal_transactions[signal];
    return transaction
        && transaction->first == owner.scheduler.now()
        && transaction->second == owner.scheduler.delta();
}

[[nodiscard]] SimulationTick Interpreter::Impl::ExecutionContext::signal_last_active(
    const SignalId signal) const{
    return signal_attribute_detail::last_active(owner, signal);
}

[[nodiscard]] bool Interpreter::Impl::ExecutionContext::signal_driving(
    const SignalId signal) const{
    return signal_attribute_detail::driving(owner, process, signal);
}

[[nodiscard]] Logic4Word Interpreter::Impl::ExecutionContext::signal_driving_value_word(
    const SignalId signal) const{
    return signal_attribute_detail::driving_value(
        owner, process, signal)
        .low_word();
}

[[nodiscard]] Logic9Word Interpreter::Impl::ExecutionContext::signal_driving_value_logic9_word(
    const SignalId signal) const{
    return signal_attribute_detail::driving_value(
        owner, process, signal)
        .logic9_low_word();
}

void Interpreter::Impl::ExecutionContext::request_channel_update(
    const std::uint64_t channel){
    owner.request_channel_update(process, channel);
}

void Interpreter::Impl::ExecutionContext::display(
    const std::string_view text,
    const bool newline){
    if (owner.output_hook) {
        owner.output_hook(
            process,
            text,
            newline,
            owner.scheduler.now(),
            owner.scheduler.delta());
    }
}

void Interpreter::Impl::ExecutionContext::postpone_display(
    const std::string_view text,
    const bool newline){
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

[[nodiscard]] SimulationTick Interpreter::Impl::ExecutionContext::current_time() const noexcept{
    return owner.scheduler.now();
}

[[nodiscard]] SystemVerilogTimeFormat
Interpreter::Impl::ExecutionContext::systemverilog_time_format() const{
    return owner.time_format;
}

void Interpreter::Impl::ExecutionContext::display_formatted(
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
    const SystemVerilogScalarKind scalar_kind){
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

void Interpreter::Impl::ExecutionContext::display_time(
    const std::string_view prefix,
    const std::string_view suffix,
    const bool newline,
    const bool postponed,
    const std::uint32_t minimum_width,
    const bool left_justify,
    const bool zero_pad,
    const bool use_timeformat_width){
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

void Interpreter::Impl::ExecutionContext::install_monitor(const MonitorInstall& registration){
    owner.install_monitor(process, registration);
}

void Interpreter::Impl::ExecutionContext::set_monitor_enabled(const bool enabled){
    owner.set_monitor_enabled(enabled);
}

[[nodiscard]] PackedLogic4 Interpreter::Impl::ExecutionContext::random_value(
    const RandomKind kind,
    const std::optional<PackedLogic4>& maximum,
    const std::optional<PackedLogic4>& minimum){
    return owner.random_value(process, kind, maximum, minimum);
}

void Interpreter::Impl::ExecutionContext::report(const std::string_view message,
    const AssertionSeverity severity,
    const SourceLocation& source){
    if (!owner.report_hook)
        return;
    owner.report_hook(process, message, severity, source,
        owner.scheduler.now(), owner.scheduler.delta());
}

void Interpreter::Impl::ExecutionContext::vhdl_report(
    const InstructionIndex instruction,
    const std::string_view message,
    const AssertionSeverity severity,
    const SourceLocation& source,
    const bool standalone){
    owner.execute_vhdl_report(
        owner.processes[process], instruction, message, severity,
        source, standalone);
}

[[nodiscard]] Logic9 Interpreter::Impl::ExecutionContext::evaluate_vital_timing_check(
    const InstructionIndex instruction, const VitalTimingCheck& operation){
    return owner.execute_vital_timing_check(process, instruction, operation);
}

void Interpreter::Impl::ExecutionContext::execute_vital_delay(const InstructionIndex instruction,
    const VitalDelay& operation, const VitalDelayRuntimeValues& values){
    owner.execute_vital_delay(process, instruction, operation, values);
}

[[nodiscard]] bool Interpreter::Impl::ExecutionContext::execution_points_enabled() const noexcept{
    return static_cast<bool>(owner.execution_point_hook);
}

} // namespace fsim::runtime::simir
