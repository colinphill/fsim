// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"
#include "fsim/runtime/systemverilog_string.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <map>
#include <new>
#include <ranges>
#include <string>
#include <vector>

namespace fsim::app::application_detail {

#if defined(FSIM_HAS_LLVM)

namespace {

struct JitProcessProfile {
    struct Entry {
        std::string name;
        std::uint64_t resumes { };
        runtime::simir::ProcessId generated_process { };
        std::size_t direct_read_slots { };
        std::size_t direct_update_slots { };
        std::size_t operations { };
        std::size_t static_waits { };
        std::chrono::nanoseconds elapsed { };
    };

    bool enabled = std::getenv("FSIM_PROFILE_JIT_PROCESSES") != nullptr;
    std::map<runtime::simir::ProcessId, Entry> entries;

    void record(
        const runtime::simir::Process& process,
        const runtime::simir::ProcessId generated_process,
        const std::size_t direct_read_slots,
        const std::size_t direct_update_slots,
        const std::chrono::nanoseconds elapsed)
    {
        if (!enabled) {
            return;
        }
        auto& entry = entries[process.id];
        if (entry.resumes == 0U) {
            entry.name = process.name;
            entry.generated_process = generated_process;
            entry.direct_read_slots = direct_read_slots;
            entry.direct_update_slots = direct_update_slots;
            entry.operations = process.operations.size();
            entry.static_waits = static_cast<std::size_t>(
                std::ranges::count_if(
                    process.operations, [](const auto& operation) {
                        return fsim::runtime::simir::operation_holds<
                            runtime::simir::WaitSensitivity>(operation);
                    }));
        }
        ++entry.resumes;
        entry.elapsed += elapsed;
    }

    ~JitProcessProfile()
    {
        if (!enabled) {
            return;
        }
        std::vector<std::pair<runtime::simir::ProcessId, Entry>> ranked {
            entries.begin(), entries.end()
        };
        std::ranges::sort(ranked, [](const auto& left, const auto& right) {
            return left.second.elapsed > right.second.elapsed;
        });
        const auto count
            = std::getenv("FSIM_PROFILE_JIT_PROCESSES_ALL") != nullptr
            ? ranked.size()
            : std::min<std::size_t>(ranked.size(), 32U);
        auto total_elapsed = std::chrono::nanoseconds::zero();
        std::uint64_t total_resumes = 0;
        for (const auto& [id, entry] : ranked) {
            static_cast<void>(id);
            total_elapsed += entry.elapsed;
            total_resumes += entry.resumes;
        }
        std::cerr << "fsim-profile: jit-process-summary processes="
                  << ranked.size()
                  << " resumes=" << total_resumes
                  << " elapsed_ms="
                  << std::chrono::duration<double, std::milli>(
                         total_elapsed).count()
                  << '\n';
        for (std::size_t index = 0; index < count; ++index) {
            const auto& [id, entry] = ranked[index];
            std::cerr << "fsim-profile: jit-process id=" << id
                      << " generated_id=" << entry.generated_process
                      << " resumes=" << entry.resumes
                      << " direct_read_slots="
                      << entry.direct_read_slots
                      << " direct_update_slots="
                      << entry.direct_update_slots
                      << " operations=" << entry.operations
                      << " static_waits=" << entry.static_waits
                      << " elapsed_ms="
                      << std::chrono::duration<double, std::milli>(
                             entry.elapsed).count()
                      << " name='" << entry.name << "'\n";
        }
    }
};

JitProcessProfile& jit_process_profile()
{
    static JitProcessProfile profile;
    return profile;
}

class JitProcessProfileScope {
public:
    explicit JitProcessProfileScope(
        const runtime::simir::Process& process,
        const runtime::simir::ProcessId generated_process,
        const std::size_t direct_read_slots,
        const std::size_t direct_update_slots)
        : process_(process)
        , generated_process_(generated_process)
        , direct_read_slots_(direct_read_slots)
        , direct_update_slots_(direct_update_slots)
        , enabled_(jit_process_profile().enabled)
    {
        if (enabled_) {
            begin_ = std::chrono::steady_clock::now();
        }
    }

    ~JitProcessProfileScope()
    {
        if (enabled_) {
            jit_process_profile().record(
                process_,
                generated_process_,
                direct_read_slots_,
                direct_update_slots_,
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - begin_));
        }
    }

private:
    const runtime::simir::Process& process_;
    runtime::simir::ProcessId generated_process_ { };
    std::size_t direct_read_slots_ { };
    std::size_t direct_update_slots_ { };
    bool enabled_ { };
    std::chrono::steady_clock::time_point begin_;
};

} // namespace

std::optional<PureBitAndAssignment> classify_pure_bit_and_assignment(
    const runtime::simir::Process& process,
    const compiler::JitProcessFrameLayout& layout,
    const std::span<const std::uint32_t> signal_widths,
    const std::span<const runtime::simir::ValueKind> signal_value_kinds,
    const std::span<const runtime::simir::SignalId> direct_read_signals,
    const std::span<const runtime::simir::SignalId> direct_update_signals)
{
    using namespace runtime::simir;

    // This is an exact admission contract, not an operation histogram. The
    // final jump re-enters the body after each static-sensitivity suspension.
    if (process.operations.size() != 10U
        || process.static_sensitivity.empty()
        || !process.static_trigger_regions.empty()
        || !process.container_register_types.empty()
        || !process.debug_string_locals.empty()
        || !process.debug_container_locals.empty()
        || layout.uses_logic9
        || layout.register_count != process.register_count
        || layout.register_widths.size() != process.register_count
        || layout.register_word_offsets.size() != process.register_count
        || direct_read_signals.size()
            != layout.direct_read_signals.size()
        || direct_update_signals.size()
            != layout.direct_update_signals.size()
        || (!process.register_value_kinds.empty()
            && process.register_value_kinds.size()
                != process.register_count)
        || !operation_holds<WaitSensitivity>(process.operations[8])
        || !operation_holds<Jump>(process.operations[9])) {
        return std::nullopt;
    }
    // The fused body preserves this exact instruction order. Public native
    // frame and signal planes are not required to have disjoint storage.
    if (!operation_holds<DebugPoint>(process.operations[0])
        || !operation_holds<DebugPoint>(process.operations[1])
        || !operation_holds<ReadSignal>(process.operations[2])
        || !operation_holds<Extract>(process.operations[3])
        || !operation_holds<ReadSignal>(process.operations[4])
        || !operation_holds<Extract>(process.operations[5])
        || !operation_holds<Binary>(process.operations[6])
        || !operation_holds<WriteUpdateSlice>(process.operations[7])) {
        return std::nullopt;
    }
    const auto* jump = operation_get_if<Jump>(&process.operations[9]);
    if (jump->target != 0U) {
        return std::nullopt;
    }

    std::array<const ReadSignal*, 2> reads { };
    std::array<const Extract*, 2> extracts { };
    std::array<std::size_t, 2> read_indices { };
    std::array<std::size_t, 2> extract_indices { };
    const Binary* binary { };
    const WriteUpdateSlice* update { };
    std::size_t binary_index { };
    std::size_t update_index { };
    std::size_t read_count { };
    std::size_t extract_count { };
    std::size_t debug_count { };
    for (std::size_t index = 0; index < 8U; ++index) {
        const auto& stored = process.operations[index];
        if (const auto* read = operation_get_if<ReadSignal>(&stored)) {
            if (read_count == reads.size()) {
                return std::nullopt;
            }
            read_indices[read_count] = index;
            reads[read_count++] = read;
        } else if (const auto* extract
            = operation_get_if<Extract>(&stored)) {
            if (extract_count == extracts.size()) {
                return std::nullopt;
            }
            extract_indices[extract_count] = index;
            extracts[extract_count++] = extract;
        } else if (const auto* binary_candidate
            = operation_get_if<Binary>(&stored)) {
            if (binary != nullptr) {
                return std::nullopt;
            }
            binary = binary_candidate;
            binary_index = index;
        } else if (const auto* update_candidate
            = operation_get_if<WriteUpdateSlice>(&stored)) {
            if (update != nullptr) {
                return std::nullopt;
            }
            update = update_candidate;
            update_index = index;
        } else if (operation_holds<DebugPoint>(stored)) {
            ++debug_count;
        } else {
            return std::nullopt;
        }
    }
    if (read_count != 2U || extract_count != 2U
        || debug_count != 2U || binary == nullptr || update == nullptr
        || binary->operation != BinaryOperator::bit_and
        || binary_index >= update_index
        || !std::ranges::all_of(
            extract_indices,
            [binary_index](const auto index) {
                return index < binary_index;
            })) {
        return std::nullopt;
    }

    const auto valid_register = [&](const RegisterId id,
                                    const std::uint32_t width) {
        return id < layout.register_widths.size()
            && layout.register_widths[id] == width
            && layout.register_word_offsets[id]
                < layout.register_word_count
            && (process.register_value_kinds.empty()
                || process.register_value_kinds[id] == ValueKind::logic4);
    };
    PureBitAndAssignment descriptor;
    for (std::size_t index = 0; index < reads.size(); ++index) {
        const auto& read = *reads[index];
        if (read.kind != SignalReadKind::current
            || read.clock || read.gate
            || read.signal >= signal_widths.size()
            || read.signal >= signal_value_kinds.size()
            || signal_value_kinds[read.signal] != ValueKind::logic4
            || signal_widths[read.signal] == 0U
            || signal_widths[read.signal] > 64U
            || !valid_register(
                read.destination, signal_widths[read.signal])
            || std::ranges::find(
                direct_read_signals, read.signal)
                == direct_read_signals.end()
            || std::ranges::count(
                direct_read_signals, read.signal) != 1) {
            return std::nullopt;
        }
        descriptor.inputs[index] = read.signal;
        descriptor.read_registers[index] = read.destination;
    }
    if (descriptor.read_registers[0] == descriptor.read_registers[1]) {
        return std::nullopt;
    }
    std::array<bool, 2> matched_reads { };
    for (std::size_t index = 0; index < extracts.size(); ++index) {
        const auto& extract = *extracts[index];
        const auto found = std::ranges::find(
            descriptor.read_registers, extract.source);
        if (found == descriptor.read_registers.end()
            || extract.width != 1U
            || !valid_register(extract.destination, 1U)) {
            return std::nullopt;
        }
        const auto read_index = static_cast<std::size_t>(
            found - descriptor.read_registers.begin());
        if (read_index != index || matched_reads[read_index]
            || read_indices[read_index] >= extract_indices[index]
            || extract.offset >= signal_widths[
                descriptor.inputs[read_index]]) {
            return std::nullopt;
        }
        matched_reads[read_index] = true;
        descriptor.extracted_registers[read_index]
            = extract.destination;
        descriptor.input_bit_offsets[read_index]
            = extract.offset;
    }
    if (descriptor.extracted_registers[0]
            == descriptor.extracted_registers[1]
        || !valid_register(binary->destination, 1U)
        || !((binary->lhs == descriptor.extracted_registers[0]
                && binary->rhs == descriptor.extracted_registers[1])
            || (binary->lhs == descriptor.extracted_registers[1]
                && binary->rhs == descriptor.extracted_registers[0]))
        || update->source != binary->destination
        || update->signal >= signal_widths.size()
        || update->signal >= signal_value_kinds.size()
        || signal_value_kinds[update->signal] != ValueKind::logic4
        || signal_widths[update->signal] == 0U
        || signal_widths[update->signal] > 64U
        || update->offset >= signal_widths[update->signal]
        || std::ranges::find(
            direct_update_signals, update->signal)
            == direct_update_signals.end()
        || std::ranges::count(
            direct_update_signals, update->signal) != 1
        || process.driver_regions.size() != 1U) {
        return std::nullopt;
    }
    const auto& region = process.driver_regions.front();
    if (region.signal != update->signal || region.whole
        || region.offset != update->offset || region.width != 1U) {
        return std::nullopt;
    }
    const std::array destination_registers {
        descriptor.read_registers[0], descriptor.read_registers[1],
        descriptor.extracted_registers[0],
        descriptor.extracted_registers[1], binary->destination
    };
    for (std::size_t index = 0; index < destination_registers.size();
         ++index) {
        if (std::ranges::find(
                destination_registers.begin(),
                destination_registers.begin() + index,
                destination_registers[index])
            != destination_registers.begin() + index) {
            return std::nullopt;
        }
        const auto slot = layout.register_word_offsets[
            destination_registers[index]];
        for (std::size_t prior = 0; prior < index; ++prior) {
            if (layout.register_word_offsets[
                    destination_registers[prior]] == slot) {
                return std::nullopt;
            }
        }
    }
    descriptor.result_register = binary->destination;
    descriptor.output = update->signal;
    descriptor.output_bit_offset = update->offset;
    return descriptor;
}

[[nodiscard]] static bool is_pure_wave_source_shape(
    const runtime::simir::Process& process)
{
    using namespace runtime::simir;
    if (process.static_sensitivity.empty()
        || !process.static_trigger_regions.empty()
        || !process.debug_locals.empty()
        || !process.debug_string_locals.empty()
        || !process.debug_container_locals.empty()
        || !process.container_register_types.empty()) {
        return false;
    }
    const auto& operations = process.operations;
    const auto debug_prefix = operations.size() >= 2U
        && operation_holds<DebugPoint>(operations[0])
        && operation_holds<DebugPoint>(operations[1]);
    if (!debug_prefix) {
        return false;
    }
    switch (operations.size()) {
    case 6U:
        return operation_holds<ReadSignal>(operations[2])
            && operation_holds<WriteUpdateSlice>(operations[3])
            && operation_holds<WaitSensitivity>(operations[4])
            && operation_holds<Jump>(operations[5]);
    case 7U:
        return operation_holds<ReadSignal>(operations[2])
            && operation_holds<Reduction>(operations[3])
            && operation_holds<WriteUpdateSlice>(operations[4])
            && operation_holds<WaitSensitivity>(operations[5])
            && operation_holds<Jump>(operations[6]);
    case 10U:
        return operation_holds<ReadSignal>(operations[2])
            && operation_holds<Extract>(operations[3])
            && operation_holds<ReadSignal>(operations[4])
            && operation_holds<Extract>(operations[5])
            && operation_holds<Binary>(operations[6])
            && operation_holds<WriteUpdateSlice>(operations[7])
            && operation_holds<WaitSensitivity>(operations[8])
            && operation_holds<Jump>(operations[9]);
    case 31U:
        return operation_holds<ReadSignal>(operations[2])
            && operation_holds<Extract>(operations[3])
            && operation_holds<Extract>(operations[4])
            && operation_holds<LoadConstant>(operations[5])
            && operation_holds<LoadConstant>(operations[6])
            && operation_holds<Binary>(operations[7])
            && operation_holds<Binary>(operations[8])
            && operation_holds<Branch>(operations[9])
            && operation_holds<ReadSignal>(operations[10])
            && operation_holds<Extract>(operations[11])
            && operation_holds<ReadSignal>(operations[12])
            && operation_holds<Binary>(operations[13])
            && operation_holds<CopyRegister>(operations[14])
            && operation_holds<Jump>(operations[15])
            && operation_holds<Branch>(operations[16])
            && operation_holds<ReadSignal>(operations[17])
            && operation_holds<Extract>(operations[18])
            && operation_holds<CopyRegister>(operations[19])
            && operation_holds<Jump>(operations[20])
            && operation_holds<ReadSignal>(operations[21])
            && operation_holds<Extract>(operations[22])
            && operation_holds<ReadSignal>(operations[23])
            && operation_holds<Binary>(operations[24])
            && operation_holds<ReadSignal>(operations[25])
            && operation_holds<Extract>(operations[26])
            && operation_holds<ConditionalSelect>(operations[27])
            && operation_holds<WriteUpdateSlice>(operations[28])
            && operation_holds<WaitSensitivity>(operations[29])
            && operation_holds<Jump>(operations[30]);
    default:
        return false;
    }
}

[[nodiscard]] static std::optional<
    compiler::JitProcessCohortLogic4BitAndMember>
make_pure_bit_and_native_member(
    const PureBitAndAssignment& assignment,
    const runtime::simir::Process& process,
    const compiler::JitProcessFrameLayout& layout,
    const std::span<const runtime::simir::SignalId> direct_reads,
    const std::span<const runtime::simir::SignalId> direct_updates)
{
    const auto left = std::ranges::find(
        direct_reads, assignment.inputs[0]);
    const auto right = std::ranges::find(
        direct_reads, assignment.inputs[1]);
    const auto update = std::ranges::find(
        direct_updates, assignment.output);
    if (left == direct_reads.end() || right == direct_reads.end()
        || update == direct_updates.end()
        || direct_reads.size()
            > std::numeric_limits<std::uint32_t>::max()
        || direct_updates.size()
            > std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }
    const auto slot = [&](const runtime::simir::RegisterId id) {
        return compiler::JitProcessCohortLogic4BitAndRegisterSlot {
            id, layout.register_word_offsets[id],
            layout.register_widths[id],
            std::ranges::any_of(process.debug_locals,
                [id](const auto& local) {
                    return local.register_id == id;
                })
        };
    };
    compiler::JitProcessCohortLogic4BitAndMember member;
    member.read_lhs = slot(assignment.read_registers[0]);
    member.read_rhs = slot(assignment.read_registers[1]);
    member.extract_lhs = slot(assignment.extracted_registers[0]);
    member.extract_rhs = slot(assignment.extracted_registers[1]);
    member.result = slot(assignment.result_register);
    member.direct_read_lhs_slot = static_cast<std::uint32_t>(
        left - direct_reads.begin());
    member.direct_read_rhs_slot = static_cast<std::uint32_t>(
        right - direct_reads.begin());
    member.extract_lhs_offset = assignment.input_bit_offsets[0];
    member.extract_rhs_offset = assignment.input_bit_offsets[1];
    member.direct_update_slot = static_cast<std::uint32_t>(
        update - direct_updates.begin());
    member.update_offset = assignment.output_bit_offset;
    member.tracks_register_initialization
        = layout.tracks_register_initialization;
    return member;
}

std::uint64_t LlvmProcessExecutor::next_instance_generation()
{
    static std::atomic_uint64_t next { 1U };
    const auto generation = next.fetch_add(1U, std::memory_order_relaxed);
    if (generation == 0U
        || generation == std::numeric_limits<std::uint64_t>::max()) {
        throw compiler::LlvmJitError(
            "compiled process executor generation exhausted");
    }
    return generation;
}

LlvmProcessExecutor::~LlvmProcessExecutor()
{
    try {
        invalidate_pure_wave_member_binding();
    } catch (...) {
        // Destructors must not replace an in-flight simulation failure.
    }
    if (compact_pure_cohort_binding_) {
        try {
            (void)jit_.release_cohort_binding(
                compact_pure_cohort_binding_);
        } catch (...) {
            // Destructors must not replace an in-flight simulation failure.
        }
    }
    if (pure_cohort_binding_) {
        try {
            (void)jit_.release_cohort_binding(pure_cohort_binding_);
        } catch (...) {
            // Destructors must not replace an in-flight simulation failure.
        }
    }
    if (cohort_binding_) {
        try {
            (void)jit_.release_cohort_binding(cohort_binding_);
        } catch (...) {
            // Destructors must not replace an in-flight simulation failure.
        }
    }
}

void LlvmProcessExecutor::invalidate_pure_wave_member_binding()
{
    // A live binding certifies the private warm frame and update buffers.
    // Every non-pure mutation entrance must invalidate it before changing
    // that state; the external storage and writer certificate stays live.
    // Publishing a valid record requires a completed bind attempt. This
    // invalidator clears valid before it clears the checked flag.
    if (!pure_wave_member_binding_
        && !pure_wave_member_binding_checked_) {
        return;
    }
    prepared_member_->valid = false;
    prepared_member_->compiler_view = nullptr;
    prepared_member_->prepared_owned_update_slot = nullptr;
    owned_update_slot_->reset();
    pure_wave_owned_update_checked_ = false;
    if (prepared_member_->generation
        == std::numeric_limits<std::uint64_t>::max()) {
        pure_wave_prepared_disabled_ = true;
    } else {
        ++prepared_member_->generation;
    }
    if (pure_wave_member_binding_) {
        (void)jit_.release_pure_wave_member_binding(
            pure_wave_member_binding_);
    }
    pure_wave_member_binding_ = { };
    pure_wave_member_lease_ = { };
    pure_wave_member_binding_checked_ = false;
    pure_wave_member_certificate_.reset();
}

[[nodiscard]] runtime::simir::ProcessResumeResult LlvmProcessExecutor::resume(
    runtime::simir::ProcessExecutionContext& context,
    const runtime::simir::InstructionIndex start_instruction)
{
    invalidate_pure_wave_member_binding();
    std::optional<JitProcessProfileScope> process_profile;
    if (jit_process_profile().enabled) {
        process_profile.emplace(
            process_, generated_process_, direct_read_signals_.size(),
            direct_update_slots_.size());
    }
    const bool consuming_cohort
        = cohort_resume_mode_ == CohortResumeMode::consume;
    const auto* const prior_prepared_domain = prepared_cohort_domain_;
    const auto* const current_domain = consuming_cohort
        ? prior_prepared_domain : context.direct_update_domain();
    if (!consuming_cohort) {
        prepared_cohort_domain_ = nullptr;
    }
    if (!consuming_cohort
        && frame_.program_counter != start_instruction) {
        const bool kernel_owned_callable_boundary = frame_.program_counter < operation_count_
            && [&] {
                   const auto& operation = process_.operations[frame_.program_counter];
                   const auto* call = fsim::runtime::simir::operation_get_if<
                       runtime::simir::Call>(&operation);
                   const auto* return_operation = fsim::runtime::simir::operation_get_if<
                       runtime::simir::Return>(&operation);
                   return (call != nullptr && call->stack.capacity == 0)
                       || (return_operation != nullptr
                           && return_operation->stack.capacity == 0)
                       || fsim::runtime::simir::operation_holds<
                           runtime::simir::CallableFramePush>(operation)
                       || fsim::runtime::simir::operation_holds<
                           runtime::simir::CallableFramePop>(operation);
               }();
        if (!kernel_owned_callable_boundary) {
            throw compiler::LlvmJitError(
                "compiled process frame PC disagrees with the simulation kernel");
        }
        frame_.program_counter = start_instruction;
    }

    auto& callback_state = callback_state_;
    auto& runtime = runtime_;
    if (!consuming_cohort) {
        if (callback_state.executor == nullptr) {
            callback_state.executor = this;
            callback_state.process = &process_;
            callback_state.generated_process = generated_process_;
            callback_state.signal_widths = signal_widths_;
            callback_state.signal_value_kinds = signal_value_kinds_;
            callback_state.signal_remap = signal_remap_
                    && dense_signal_remap_.empty()
                ? std::span<const SignalRemap::value_type> { *signal_remap_ }
                : std::span<const SignalRemap::value_type> { };
            callback_state.dense_signal_remap = dense_signal_remap_;
            callback_state.dense_signal_remap_base = dense_signal_remap_base_;
        }
        callback_state.context = &context;
        callback_state.direct_signal_logic9_planes = {
            context.direct_signal_logic9_plane0(),
            context.direct_signal_logic9_plane1(),
            context.direct_signal_logic9_plane2(),
            context.direct_signal_logic9_plane3()
        };
        callback_state.supports_direct_word_updates
            = context.supports_direct_word_updates();
        callback_state.failure = { };
        invalidate_signal_read_cache(callback_state);
        if (runtime.abi_version == 0) {
            runtime.abi_version = FSIM_JIT_RUNTIME_ABI_VERSION_V1;
            runtime.struct_size = sizeof(runtime);
            runtime.read_signal = read_signal;
            runtime.write_signal = write_signal;
            runtime.assert_failed = assert_failed;
            runtime.write_update = write_update;
            runtime.write_after = write_after;
            runtime.write_signal_slice = write_signal_slice;
            runtime.write_update_slice = write_update_slice;
            runtime.write_after_slice = write_after_slice;
            runtime.signal_event = signal_event;
            runtime.signal_last_value = signal_last_value;
            runtime.signal_last_event = signal_last_event;
            runtime.signal_active = signal_active;
            runtime.signal_last_active = signal_last_active;
            runtime.signal_driving = signal_driving;
            runtime.signal_driving_value = signal_driving_value;
            runtime.signal_driving_value_logic9 = signal_driving_value_logic9;
            runtime.read_simulation_time = read_simulation_time;
            runtime.vital_timing_check = vital_timing_check;
            runtime.vital_delay = vital_delay;
            runtime.write_output = write_output;
            runtime.schedule_output = schedule_output;
            runtime.write_report = write_report;
            runtime.write_formatted = write_formatted;
            runtime.write_time = write_time;
            runtime.install_monitor = install_monitor;
            runtime.control_monitor = control_monitor;
            runtime.random_value = random_value;
            runtime.write_inertial = write_inertial;
            runtime.write_inertial_slice = write_inertial_slice;
            runtime.write_projected = write_projected;
            runtime.write_projected_slice = write_projected_slice;
            runtime.write_projected_waveform = write_projected_waveform;
            runtime.write_projected_waveform_slice = write_projected_waveform_slice;
            runtime.read_signal_logic9 = signal_remap_
                ? read_signal_logic9
                : read_signal_logic9_identity;
            runtime.write_signal_logic9 = write_signal_logic9;
            runtime.write_update_logic9 = write_update_logic9;
            runtime.write_after_logic9 = write_after_logic9;
            runtime.write_signal_slice_logic9 = write_signal_slice_logic9;
            runtime.write_update_slice_logic9 = write_update_slice_logic9;
            runtime.write_after_slice_logic9 = write_after_slice_logic9;
            runtime.signal_last_value_logic9 = signal_last_value_logic9;
            runtime.write_inertial_logic9 = write_inertial_logic9;
            runtime.write_inertial_slice_logic9 = write_inertial_slice_logic9;
            runtime.write_projected_logic9 = write_projected_logic9;
            runtime.write_projected_slice_logic9 = write_projected_slice_logic9;
            runtime.write_projected_waveform_logic9 = write_projected_waveform_logic9;
            runtime.write_projected_waveform_slice_logic9 = write_projected_waveform_slice_logic9;
            runtime.write_formatted_logic9 = write_formatted_logic9;
            runtime.force_signal_slice = force_signal_slice;
            runtime.force_signal_slice_logic9 = force_signal_slice_logic9;
            runtime.release_signal_slice = release_signal_slice;
            runtime.force_driver_signal_slice = force_driver_signal_slice;
            runtime.force_driver_signal_slice_logic9 = force_driver_signal_slice_logic9;
            runtime.release_driver_signal_slice = release_driver_signal_slice;
            runtime.load_string = load_string;
            runtime.copy_string = copy_string;
            runtime.read_string_object = read_string_object;
            runtime.write_string_object = write_string_object;
            runtime.concatenate_strings = concatenate_strings;
            runtime.compare_strings = compare_strings;
            runtime.string_length = string_length;
            runtime.string_index = string_index;
            runtime.string_replace_byte = string_replace_byte;
            runtime.write_string_output = write_string_output;
            runtime.file_open = file_open;
            runtime.file_close = file_close;
            runtime.file_write = file_write;
            runtime.file_read_line = file_read_line;
            runtime.file_end_of_file = file_end_of_file;
            runtime.file_error = file_error;
            runtime.container_operation = container_operation;
            runtime.execute_signal_operation = execute_signal_operation;
            runtime.container_read_word = container_read_word;
            runtime.container_write_word = container_write_word;
            runtime.container_read_packed = container_read_packed;
            runtime.container_write_packed = container_write_packed;
            runtime.read_signal_packed = read_signal_packed;
            runtime.write_signal_packed = write_signal_packed;
            runtime.read_signal_dynamic_part = read_signal_dynamic_part;
            runtime.record_code_coverage_counter
                = record_code_coverage_counter;
            runtime.sample_coverage = sample_coverage;
            runtime.execute_class_property_operation
                = execute_class_property_operation;
            runtime.query_event_triggered = query_event_triggered;
            const auto direct_aval = context.direct_signal_aval();
            const auto direct_bval = context.direct_signal_bval();
            const bool supports_direct_planes
                = (!direct_read_signals_.empty() || !direct_update_signals_.empty())
                && !direct_aval.empty()
                && direct_aval.size() == direct_bval.size()
                && direct_aval.size()
                    <= std::numeric_limits<std::uint32_t>::max();
            const bool supports_direct_reads = supports_direct_planes
                && !direct_read_signals_.empty()
                && direct_read_signals_.size() == layout_.direct_read_signals.size()
                && std::ranges::all_of(
                    direct_read_signals_,
                    [&](const auto signal) { return signal < direct_aval.size(); });
            runtime.direct_signal_aval = supports_direct_planes
                ? direct_aval.data()
                : nullptr;
            runtime.direct_signal_bval = supports_direct_planes
                ? direct_bval.data()
                : nullptr;
            const bool supports_direct_logic9_planes
                = callback_state.direct_signal_logic9_planes[0].size()
                    == direct_aval.size()
                && callback_state.direct_signal_logic9_planes[1].size()
                    == direct_aval.size()
                && callback_state.direct_signal_logic9_planes[2].size()
                    == direct_aval.size()
                && callback_state.direct_signal_logic9_planes[3].size()
                    == direct_aval.size();
            runtime.direct_signal_logic9_plane0
                = supports_direct_logic9_planes
                ? callback_state.direct_signal_logic9_planes[0].data()
                : nullptr;
            runtime.direct_signal_logic9_plane1
                = supports_direct_logic9_planes
                ? callback_state.direct_signal_logic9_planes[1].data()
                : nullptr;
            runtime.direct_signal_logic9_plane2
                = supports_direct_logic9_planes
                ? callback_state.direct_signal_logic9_planes[2].data()
                : nullptr;
            runtime.direct_signal_logic9_plane3
                = supports_direct_logic9_planes
                ? callback_state.direct_signal_logic9_planes[3].data()
                : nullptr;
            runtime.direct_read_signals = supports_direct_reads
                ? direct_read_signals_.data()
                : nullptr;
            runtime.direct_read_signal_count = supports_direct_reads
                ? static_cast<std::uint32_t>(direct_read_signals_.size())
                : 0U;
            runtime.direct_signal_count = supports_direct_planes
                ? static_cast<std::uint32_t>(direct_aval.size())
                : 0U;
            runtime.direct_signal_reserved = 0U;
            const auto direct_wide_aval = context.direct_wide_signal_aval();
            const auto direct_wide_bval = context.direct_wide_signal_bval();
            const auto direct_wide_logic9_plane2
                = context.direct_wide_signal_logic9_plane2();
            const auto direct_wide_logic9_plane3
                = context.direct_wide_signal_logic9_plane3();
            const auto direct_wide_offsets = context.direct_wide_signal_offsets();
            const bool supports_direct_wide_planes
                = !direct_wide_aval.empty()
                && direct_wide_aval.size() == direct_wide_bval.size()
                && direct_wide_aval.size()
                    <= std::numeric_limits<std::uint32_t>::max()
                && direct_wide_offsets.size()
                    <= std::numeric_limits<std::uint32_t>::max();
            const bool supports_direct_wide_logic9_planes
                = supports_direct_wide_planes
                && direct_wide_logic9_plane2.size()
                    == direct_wide_aval.size()
                && direct_wide_logic9_plane3.size()
                    == direct_wide_aval.size();
            runtime.direct_wide_signal_aval = supports_direct_wide_planes
                ? direct_wide_aval.data()
                : nullptr;
            runtime.direct_wide_signal_bval = supports_direct_wide_planes
                ? direct_wide_bval.data()
                : nullptr;
            runtime.direct_wide_signal_offsets = supports_direct_wide_planes
                ? direct_wide_offsets.data()
                : nullptr;
            runtime.direct_wide_signal_offset_count = supports_direct_wide_planes
                ? static_cast<std::uint32_t>(direct_wide_offsets.size())
                : 0U;
            runtime.direct_wide_word_count = supports_direct_wide_planes
                ? static_cast<std::uint32_t>(direct_wide_aval.size())
                : 0U;
            runtime.direct_wide_signal_logic9_plane2
                = supports_direct_wide_logic9_planes
                ? direct_wide_logic9_plane2.data()
                : nullptr;
            runtime.direct_wide_signal_logic9_plane3
                = supports_direct_wide_logic9_planes
                ? direct_wide_logic9_plane3.data()
                : nullptr;
        }
        runtime.context = &callback_state;
        runtime.code_coverage_hit_counters
            = code_coverage_hit_counters_.empty()
            ? nullptr
            : code_coverage_hit_counters_.data();
        runtime.code_coverage_hit_count = static_cast<std::uint32_t>(
            code_coverage_hit_counters_.size());
        const auto direct_code_coverage_counters
            = context.direct_code_coverage_counters();
        runtime.code_coverage_counter_values
            = direct_code_coverage_counters.empty()
            ? nullptr
            : direct_code_coverage_counters.data();
        runtime.code_coverage_counter_count
            = direct_code_coverage_counters.size()
                    <= std::numeric_limits<std::uint32_t>::max()
            ? static_cast<std::uint32_t>(
                  direct_code_coverage_counters.size())
            : 0U;
        runtime.flags = context.execution_points_enabled()
            ? FSIM_JIT_RUNTIME_FLAG_DEBUG_POINTS
            : 0;
        runtime.direct_update_slots
            = callback_state.supports_direct_word_updates
                && !direct_update_slots_.empty()
            ? direct_update_slots_.data()
            : nullptr;
        runtime.direct_update_slot_count = static_cast<std::uint32_t>(
            direct_update_slots_.size());
        runtime.direct_update_reserved = 0U;
        runtime.direct_update_active_words
            = callback_state.supports_direct_word_updates
                && !direct_update_active_words_.empty()
            ? direct_update_active_words_.data()
            : nullptr;
        runtime.direct_update_active_word_count = static_cast<std::uint32_t>(
            direct_update_active_words_.size());
        runtime.direct_update_active_reserved = 0U;
        runtime.static_trigger_mask = context.static_trigger_mask();
        const auto writer_revision = context.signal_writer_revision();
        const bool owner_changed = current_domain != prior_prepared_domain;
        if (owner_changed
            || writer_revision != direct_update_writer_revision_) {
            const auto stable_owners
                = context.stable_single_writer_processes();
            const auto direct_aval = context.direct_signal_aval();
            const auto direct_bval = context.direct_signal_bval();
            const bool enabled = stable_direct_update_suppression_allowed_;
            for (std::size_t index = 0;
                index < direct_update_slots_.size(); ++index) {
                auto& slot = direct_update_slots_[index];
                if (owner_changed) {
                    slot.reserved &= ~1U;
                }
                const auto signal = direct_update_signals_[index];
                if (signal < signal_value_kinds_.size()
                    && signal_value_kinds_[signal]
                        == runtime::simir::ValueKind::logic9) {
                    slot.reserved &= ~1U;
                    continue;
                }
                const bool stable = enabled && slot.width <= 64U
                    && signal < stable_owners.size()
                    && signal < direct_aval.size()
                    && signal < direct_bval.size();
                const bool owned = stable
                    && stable_owners[signal] == process_.id;
                if (!owned) {
                    slot.reserved &= ~1U;
                    continue;
                }
                if ((slot.reserved & 1U) == 0U) {
                    slot.aval = direct_aval[signal];
                    slot.bval = direct_bval[signal];
                }
                slot.reserved |= 1U;
            }
            direct_update_writer_revision_ = writer_revision;
        }
        prepared_cohort_domain_ = current_domain;
    }

    if (cohort_resume_mode_ == CohortResumeMode::prepare) {
        cohort_resume_mode_ = CohortResumeMode::normal;
        return { };
    }

    const auto flush_updates = [&] {
        flush_buffered_updates(context, !consuming_cohort);
    };

    auto result = consuming_cohort
        ? cohort_resume_result_
        : fsim_jit_resume_result_v1 { };
    if (!consuming_cohort) {
        result.abi_version = FSIM_JIT_RESUME_RESULT_ABI_VERSION_V1;
        result.struct_size = sizeof(result);
    }
    compiler::JitResumeStatus status;
    try {
        status = [&]() -> compiler::JitResumeStatus {
        try {
            if (consuming_cohort) {
                cohort_resume_mode_ = CohortResumeMode::normal;
                const auto failure = std::exchange(
                    cohort_resume_failure_, { });
                if (failure) {
                    std::rethrow_exception(failure);
                }
                return static_cast<compiler::JitResumeStatus>(
                    cohort_resume_status_);
            }
            return jit_.resume_prevalidated(binding_, runtime, frame_, result);
        } catch (const compiler::LlvmJitGeneratedRuntimeError& error) {
            if (callback_state.failure) {
                std::rethrow_exception(callback_state.failure);
            }
            switch (error.reason()) {
            case compiler::JitGeneratedRuntimeErrorReason::
                unknown_branch_condition:
                throw runtime::simir::InterpreterError(
                    process_.id,
                    error.instruction(),
                    "branch condition is unknown or high impedance");
            case compiler::JitGeneratedRuntimeErrorReason::
                integer_operand_unknown:
                throw runtime::simir::InterpreterError(
                    process_.id,
                    error.instruction(),
                    "VHDL integer operand contains an unknown or "
                    "high-impedance value");
            case compiler::JitGeneratedRuntimeErrorReason::
                integer_overflow:
                throw runtime::simir::InterpreterError(
                    process_.id,
                    error.instruction(),
                    "VHDL integer arithmetic overflow");
            case compiler::JitGeneratedRuntimeErrorReason::
                integer_division_by_zero:
                throw runtime::simir::InterpreterError(
                    process_.id,
                    error.instruction(),
                    "VHDL integer division by zero");
            case compiler::JitGeneratedRuntimeErrorReason::
                integer_negative_exponent:
                throw runtime::simir::InterpreterError(
                    process_.id,
                    error.instruction(),
                    "VHDL integer exponent must be nonnegative");
            case compiler::JitGeneratedRuntimeErrorReason::
                integer_subtype_range:
                throw runtime::simir::InterpreterError(
                    process_.id,
                    error.instruction(),
                    "VHDL integer subtype range check failed");
            case compiler::JitGeneratedRuntimeErrorReason::
                dynamic_index_unknown:
                throw runtime::simir::InterpreterError(
                    process_.id,
                    error.instruction(),
                    "dynamic packed index contains an unknown or "
                    "high-impedance value");
            case compiler::JitGeneratedRuntimeErrorReason::
                dynamic_index_range:
                throw runtime::simir::InterpreterError(
                    process_.id,
                    error.instruction(),
                    "dynamic packed index is outside the declared range");
            case compiler::JitGeneratedRuntimeErrorReason::
                call_stack_unknown:
                throw runtime::simir::InterpreterError(
                    process_.id,
                    error.instruction(),
                    "call-stack pointer or return target is unknown");
            case compiler::JitGeneratedRuntimeErrorReason::
                call_stack_overflow:
                throw runtime::simir::InterpreterError(
                    process_.id,
                    error.instruction(),
                    "call-stack capacity is exhausted");
            case compiler::JitGeneratedRuntimeErrorReason::
                call_stack_underflow:
                throw runtime::simir::InterpreterError(
                    process_.id,
                    error.instruction(),
                    "call-stack underflow");
            case compiler::JitGeneratedRuntimeErrorReason::
                call_stack_target:
                throw runtime::simir::InterpreterError(
                    process_.id,
                    error.instruction(),
                    "call-stack return target is invalid");
            case compiler::JitGeneratedRuntimeErrorReason::
                string_callback_failure:
                throw runtime::simir::InterpreterError(
                    process_.id,
                    error.instruction(),
                    "mutable string runtime callback failed");
            case compiler::JitGeneratedRuntimeErrorReason::
                file_callback_failure:
                throw runtime::simir::InterpreterError(
                    process_.id,
                    error.instruction(),
                    "text file runtime callback failed");
            case compiler::JitGeneratedRuntimeErrorReason::
                container_callback_failure:
                throw runtime::simir::InterpreterError(
                    process_.id,
                    error.instruction(),
                    "bounded container runtime callback failed");
            case compiler::JitGeneratedRuntimeErrorReason::
                signal_callback_failure:
                throw runtime::simir::InterpreterError(
                    process_.id,
                    error.instruction(),
                    "exact-width signal runtime callback failed");
            case compiler::JitGeneratedRuntimeErrorReason::
                coverage_callback_failure:
                throw runtime::simir::InterpreterError(
                    process_.id,
                    error.instruction(),
                    "code coverage counter runtime callback failed");
            case compiler::JitGeneratedRuntimeErrorReason::
                native_service_callback_failure:
                throw runtime::simir::InterpreterError(
                    process_.id,
                    error.instruction(),
                    "native SimIR service callback failed");
            }
            throw;
        } catch (...) {
            // A callback failure is the first language/runtime failure observed by
            // generated code and must not be masked by a later adapter status.
            if (callback_state.failure) {
                std::rethrow_exception(callback_state.failure);
            }
            throw;
        }
        }();
    } catch (...) {
        const auto failure = std::current_exception();
        flush_updates();
        if (has_container_registers_
            && !active_container_object_aliases_.empty()) {
            discard_container_object_aliases();
        }
        std::rethrow_exception(failure);
    }
    flush_updates();
    if (callback_state.failure) {
        std::rethrow_exception(callback_state.failure);
    }
    // ReadContainerObject installs a zero-copy alias. Any operation that needs
    // a complete value materializes it before executing; aliases still active
    // at a suspension boundary were consumed only by direct element reads and
    // are dead temporaries, so do not snapshot the whole object here.
    if (has_container_registers_
        && !active_container_object_aliases_.empty()) {
        discard_container_object_aliases();
    }
    if (result.instruction >= operation_count_) {
        throw compiler::LlvmJitError(
            "compiled process returned an invalid boundary instruction");
    }
    if (status == compiler::JitResumeStatus::wait_sensitivity) {
        if (frame_.program_counter != result.instruction + 1U) {
            throw compiler::LlvmJitError(
                "compiled process returned a non-sequential WaitSensitivity PC");
        }
        auto resumed = runtime::simir::ProcessResumeResult {
            result.instruction, frame_.program_counter
        };
        resumed.external.kind
            = validated_static_sensitivity_
            ? runtime::simir::ExternalSuspendKind::validated_wait_sensitivity
            : runtime::simir::ExternalSuspendKind::wait_sensitivity;
        return resumed;
    }

    switch (status) {
    case compiler::JitResumeStatus::completed:
        require_boundary<runtime::simir::Halt>(
            result.instruction, "completion");
        break;
    case compiler::JitResumeStatus::assertion_failed: {
        const auto* assertion = fsim::runtime::simir::operation_get_if<runtime::simir::Assert>(
            &process_.operations[result.instruction]);
        if (assertion != nullptr) {
            throw runtime::simir::AssertionError(
                process_.id,
                result.instruction,
                assertion->message.empty()
                    ? "assertion failed"
                    : assertion->message,
                assertion->severity,
                assertion->source);
        }
        const auto* report = fsim::runtime::simir::operation_get_if<runtime::simir::Report>(
            &process_.operations[result.instruction]);
        if (report != nullptr
            && report->severity
                == runtime::simir::AssertionSeverity::failure) {
            throw runtime::simir::AssertionError(
                process_.id,
                result.instruction,
                report->message.empty()
                    ? "report failure"
                    : report->message,
                report->severity,
                report->source,
                true);
        }
        throw compiler::LlvmJitError(
            "compiled process reported an assertion at an incompatible "
            "instruction");
    }
    case compiler::JitResumeStatus::wait_for: {
        const auto* wait = fsim::runtime::simir::operation_get_if<runtime::simir::WaitFor>(
            &process_.operations[result.instruction]);
        if (wait == nullptr) {
            throw compiler::LlvmJitError(
                "compiled process reported WaitFor at a non-wait instruction");
        }
        const auto expected_delay = wait->source ? 0 : wait->delay;
        if (result.delay != expected_delay) {
            throw compiler::LlvmJitError(
                "compiled process returned a WaitFor delay that disagrees "
                "with SimIR");
        }
        break;
    }
    case compiler::JitResumeStatus::wait_on: {
        const auto* wait = fsim::runtime::simir::operation_get_if<
            runtime::simir::WaitOn>(
            &process_.operations[result.instruction]);
        if (wait == nullptr) {
            throw compiler::LlvmJitError(
                "compiled process reported WaitOn at a non-wait instruction");
        }
        if (result.delay != wait->timeout.value_or(0)) {
            throw compiler::LlvmJitError(
                "compiled process returned a WaitOn timeout that disagrees "
                "with SimIR");
        }
        break;
    }
    case compiler::JitResumeStatus::wait_sensitivity:
        throw compiler::LlvmJitError(
            "compiled WaitSensitivity bypassed its fast return path");
    case compiler::JitResumeStatus::wait_forever:
        require_boundary<runtime::simir::WaitForever>(
            result.instruction, "WaitForever");
        break;
    case compiler::JitResumeStatus::yielded:
        require_boundary<runtime::simir::Yield>(
            result.instruction, "yield");
        break;
    case compiler::JitResumeStatus::fork:
        require_boundary<runtime::simir::Fork>(
            result.instruction, "fork");
        break;
    case compiler::JitResumeStatus::fork_end:
        require_boundary<runtime::simir::ForkEnd>(
            result.instruction, "fork end");
        break;
    case compiler::JitResumeStatus::wait_fork:
        require_boundary<runtime::simir::WaitFork>(
            result.instruction, "wait fork");
        break;
    case compiler::JitResumeStatus::disable_fork:
        require_boundary<runtime::simir::DisableFork>(
            result.instruction, "disable fork");
        break;
    case compiler::JitResumeStatus::debug_point:
        require_boundary<runtime::simir::DebugPoint>(
            result.instruction, "debug point");
        break;
    case compiler::JitResumeStatus::paused:
        require_boundary<runtime::simir::Pause>(
            result.instruction, "pause");
        break;
    case compiler::JitResumeStatus::stopped:
        require_boundary<runtime::simir::Stop>(
            result.instruction, "stop");
        break;
    case compiler::JitResumeStatus::simir_boundary: {
        const auto& operation = process_.operations[result.instruction];
        const auto* call = fsim::runtime::simir::operation_get_if<
            runtime::simir::Call>(&operation);
        const auto* return_operation = fsim::runtime::simir::operation_get_if<
            runtime::simir::Return>(&operation);
        const auto* read_signal = fsim::runtime::simir::operation_get_if<
            runtime::simir::ReadSignal>(&operation);
        const auto* last_value = fsim::runtime::simir::operation_get_if<
            runtime::simir::SignalLastValue>(&operation);
        const auto* driving_value = fsim::runtime::simir::operation_get_if<
            runtime::simir::SignalDrivingValue>(&operation);
        const auto* report = fsim::runtime::simir::operation_get_if<
            runtime::simir::Report>(&operation);
        const auto wide_attribute_boundary = [&](const auto* attribute) {
            return attribute != nullptr
                && attribute->signal < signal_widths_.size()
                && signal_widths_[attribute->signal] > 64U;
        };
        const auto host_boundary = (read_signal != nullptr
                                       && read_signal->kind
                                           != runtime::simir::SignalReadKind::current)
            || wide_attribute_boundary(last_value)
            || wide_attribute_boundary(driving_value)
            || (report != nullptr
                && report->severity
                    == runtime::simir::AssertionSeverity::failure)
            || fsim::runtime::simir::operation_holds<
                                       runtime::simir::ClassAllocate>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::ClassPropertyRead>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::ClassPropertyWrite>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::ClassMethodCall>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::ClassStaticPropertyRead>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::ClassStaticPropertyWrite>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::ClassStaticMethodCall>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::ProcessSelf>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::ProcessStatusQuery>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::ProcessCompleted>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::ProcessAwait>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::ProcessKill>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::ProcessSuspend>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::ProcessResume>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::ProcessGetRandState>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::ProcessSetRandState>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::ProcessSrandom>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::ScopeRandomize>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::FormatDisplay>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::WaitOrder>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::EventTriggered>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::EventAlias>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::WaitRegion>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::CoverageSample>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::CoverageQuery>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::VhdlPslApi>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::VhdlAssertApi>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::CoverageControl>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::CoverageAccess>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::RandomDistribution>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::PlusArgSelect>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::SystemCommand>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::VcdControl>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::CoverageDatabaseControl>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::VhdlEnvironmentTime>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::VhdlEnvironmentTimeToString>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::VhdlEnvironmentDirectory>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::VhdlEnvironmentGetenv>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::VhdlEnvironmentCallPath>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::VhdlEnvironmentGetCallPath>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::VhdlReflectionApi>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::StochasticQueueOperation>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::PlaEvaluate>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::WaitPla>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::TimeFormatControl>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::DisableBlock>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::MailboxCreate>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::MailboxPut>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::MailboxGet>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::MailboxNum>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::SemaphoreCreate>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::SemaphoreGet>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::SemaphorePut>(operation)
            || (call != nullptr && call->stack.capacity == 0)
            || (return_operation != nullptr
                && return_operation->stack.capacity == 0)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::CallableFramePush>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::CallableFramePop>(operation);
        if (!host_boundary) {
            throw compiler::LlvmJitError(
                "compiled process reported an unsupported SimIR boundary");
        }
        break;
    }
    }
    const auto& boundary_operation = process_.operations[result.instruction];
    const auto* boundary_call = fsim::runtime::simir::operation_get_if<
        runtime::simir::Call>(&boundary_operation);
    const auto* boundary_return = fsim::runtime::simir::operation_get_if<
        runtime::simir::Return>(&boundary_operation);
    const bool callable_boundary = status
            == compiler::JitResumeStatus::simir_boundary
        && ((boundary_call != nullptr
                && boundary_call->stack.capacity == 0)
            || (boundary_return != nullptr
                && boundary_return->stack.capacity == 0)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::CallableFramePush>(boundary_operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::CallableFramePop>(boundary_operation));
    const auto expected_program_counter = callable_boundary
        ? result.instruction
        : result.instruction + 1U;
    if (frame_.program_counter != expected_program_counter) {
        throw compiler::LlvmJitError(
            "compiled process returned a non-sequential boundary PC");
    }
    return runtime::simir::ProcessResumeResult {
        result.instruction, frame_.program_counter
    };
}

[[nodiscard]] std::size_t LlvmProcessExecutor::resume_cohort(
    const std::span<runtime::simir::ProcessCohortResumeEntry> entries)
{
    return resume_cohort_impl(entries, nullptr);
}

[[nodiscard]] std::size_t
LlvmProcessExecutor::resume_cohort_with_native_context(
    const std::span<runtime::simir::ProcessCohortResumeEntry> entries,
    const runtime::simir::ProcessCohortNativeContext& native_context)
{
    return resume_cohort_impl(entries, &native_context);
}

[[nodiscard]] const runtime::simir::PureWavePreparedMember*
LlvmProcessExecutor::prepare_pure_wave_member(
    const runtime::simir::PureWaveResumeEntry& entry,
    runtime::simir::ProcessExecutionContext& shared_context,
    const runtime::simir::ProcessCohortNativeContext& native_context,
    const std::uint64_t owner_epoch)
{
    if (pure_wave_prepared_disabled_
        || entry.process != process_.id || entry.executor != this
        || entry.queued == nullptr || entry.waiting_on_static == nullptr
        || entry.status == nullptr || native_context.owner == nullptr
        || !native_context.supports_direct_word_updates
        || native_context.execution_points_enabled
        || jit_process_profile().enabled
        || shared_context.direct_update_domain()
            != native_context.owner) {
        return nullptr;
    }

    if (!pure_wave_member_eligible_) {
        bool source_shape = is_pure_wave_source_shape(process_);
        if (source_shape && operation_count_ == 10U) {
            const auto assignment = classify_pure_bit_and_assignment(
                process_, layout_, signal_widths_, signal_value_kinds_,
                direct_read_signals_, direct_update_signals_);
            source_shape = assignment.has_value();
            if (assignment) {
                pure_wave_and_inputs_ = assignment->inputs;
            }
        }
        pure_wave_member_eligible_ = source_shape;
    }
    if (!*pure_wave_member_eligible_ || operation_count_ < 2U
        || entry.start_instruction != operation_count_ - 1U) {
        return nullptr;
    }

    const PureWaveMemberCertificate certificate {
        {
            entry.queued, entry.waiting_on_static, entry.status,
            native_context.owner,
            native_context.signal_aval.data(),
            native_context.signal_bval.data(),
            native_context.wide_signal_aval.data(),
            native_context.wide_signal_bval.data(),
            native_context.wide_signal_offsets.data(),
            runtime_.direct_read_signals,
            runtime_.direct_update_slots,
            runtime_.direct_update_active_words,
            direct_update_signals_.data()
        },
        {
            native_context.signal_aval.size(),
            native_context.signal_bval.size(),
            native_context.wide_signal_aval.size(),
            native_context.wide_signal_bval.size(),
            native_context.wide_signal_offsets.size(),
            runtime_.direct_read_signal_count,
            runtime_.direct_update_slot_count,
            runtime_.direct_update_active_word_count,
            direct_update_signals_.size()
        },
        native_context.signal_writer_revision
    };
    if (pure_wave_member_binding_
        && (!pure_wave_member_certificate_
            || *pure_wave_member_certificate_ != certificate
            || !pure_wave_member_lease_
            || prepared_member_->owner_epoch != owner_epoch)) {
        invalidate_pure_wave_member_binding();
    }
    if (pure_wave_prepared_disabled_) {
        return nullptr;
    }

    if (!pure_wave_member_binding_) {
        const bool warm
            = frame_.program_counter == operation_count_ - 1U
            && frame_.state == FSIM_JIT_FRAME_STATE_READY
            && frame_.last_instruction == operation_count_ - 2U
            && frame_.native_call_depth == 0U
            && cohort_resume_mode_ == CohortResumeMode::normal
            && prepared_cohort_domain_ == native_context.owner
            && runtime_.abi_version == FSIM_JIT_RUNTIME_ABI_VERSION_V1
            && runtime_.flags == 0U
            && !callback_state_.failure
            && pending_update_words_.empty()
            && !has_buffered_update_words()
            && !has_buffered_logic9_updates()
            && direct_update_writer_revision_
                == native_context.signal_writer_revision
            && runtime_.direct_signal_aval
                == native_context.signal_aval.data()
            && runtime_.direct_signal_bval
                == native_context.signal_bval.data()
            && runtime_.direct_signal_count
                == native_context.signal_aval.size()
            && native_context.signal_aval.size()
                == native_context.signal_bval.size()
            && runtime_.direct_wide_signal_aval
                == native_context.wide_signal_aval.data()
            && runtime_.direct_wide_signal_bval
                == native_context.wide_signal_bval.data()
            && runtime_.direct_wide_signal_offsets
                == native_context.wide_signal_offsets.data()
            && runtime_.direct_wide_signal_offset_count
                == native_context.wide_signal_offsets.size()
            && runtime_.direct_wide_word_count
                == native_context.wide_signal_aval.size()
            && native_context.wide_signal_aval.size()
                == native_context.wide_signal_bval.size()
            && runtime_.direct_read_signals
                == direct_read_signals_.data()
            && runtime_.direct_update_slots
                == direct_update_slots_.data()
            && runtime_.direct_update_active_words
                == direct_update_active_words_.data();
        if (!warm || pure_wave_member_binding_checked_) {
            return nullptr;
        }
        const compiler::JitProcessCohortResumeEntry native {
            binding_, runtime_, frame_, cohort_resume_result_,
            reinterpret_cast<std::uint8_t*>(entry.queued),
            reinterpret_cast<std::uint8_t*>(entry.waiting_on_static),
            reinterpret_cast<std::uint8_t*>(entry.status)
        };
        const compiler::JitPureWaveMember member {
            native, &process_, direct_update_signals_
        };
        pure_wave_member_binding_checked_ = true;
        pure_wave_member_binding_
            = jit_.bind_pure_wave_member_prevalidated(member)
                  .value_or(compiler::JitPureWaveMemberBinding { });
        if (pure_wave_member_binding_) {
            pure_wave_member_lease_
                = jit_.acquire_pure_wave_member_lease(
                      pure_wave_member_binding_)
                      .value_or(compiler::JitPureWaveMemberLease { });
        }
        pure_wave_member_certificate_ = certificate;
        if (!pure_wave_member_lease_) {
            if (pure_wave_member_binding_) {
                (void)jit_.release_pure_wave_member_binding(
                    pure_wave_member_binding_);
                pure_wave_member_binding_ = { };
            }
            return nullptr;
        }
    }

    if (prepared_member_->generation == 0U) {
        prepared_member_->generation = 1U;
    }
    auto& prepared = *prepared_member_;
    prepared.process = process_.id;
    prepared.executor = this;
    prepared.owner = native_context.owner;
    prepared.domain = pure_wave_member_lease_.prepared_domain();
    prepared.compiler_view = pure_wave_member_lease_.prepared_view();
    prepared.update_batch = {
        process_.id, direct_update_slot_views_,
        direct_update_active_words_
    };
    if (!pure_wave_owned_update_checked_) {
        *owned_update_slot_
            = shared_context.prepare_owned_update_slot(
                  prepared.update_batch);
        pure_wave_owned_update_checked_ = true;
    }
    prepared.prepared_owned_update_slot
        = *owned_update_slot_
        ? &**owned_update_slot_ : nullptr;
    prepared.and_lhs = pure_wave_and_inputs_
        ? (*pure_wave_and_inputs_)[0] : 0U;
    prepared.and_rhs = pure_wave_and_inputs_
        ? (*pure_wave_and_inputs_)[1] : 0U;
    prepared.resume_instruction = operation_count_ - 1U;
    prepared.owner_epoch = owner_epoch;
    prepared.compiler_generation
        = pure_wave_member_lease_.prepared_generation();
    switch (operation_count_) {
    case 6U:
        prepared.shape = runtime::simir::PureWavePreparedShape::wide_copy6;
        break;
    case 7U:
        prepared.shape = runtime::simir::PureWavePreparedShape::reduction7;
        break;
    case 10U:
        prepared.shape = runtime::simir::PureWavePreparedShape::logic4_bit_and;
        break;
    case 31U:
        prepared.shape = runtime::simir::PureWavePreparedShape::reducer31;
        break;
    default:
        return nullptr;
    }
    prepared.valid = prepared.compiler_view != nullptr;
    return prepared.valid ? &prepared : nullptr;
}

[[nodiscard]] std::optional<runtime::simir::PureWaveCompletion>
LlvmProcessExecutor::try_resume_prepared_pure_wave(
    const std::span<const runtime::simir::PureWavePreparedMember* const>
        members,
    const std::span<const std::size_t> task_ends,
    runtime::simir::ProcessExecutionContext& shared_context,
    const runtime::simir::ProcessCohortNativeContext& native_context)
{
    if (members.empty() || task_ends.empty()
        || task_ends.back() != members.size()
        || native_context.owner == nullptr
        || !native_context.supports_direct_word_updates
        || native_context.execution_points_enabled
        || jit_process_profile().enabled
        || shared_context.direct_update_domain()
            != native_context.owner) {
        return std::nullopt;
    }

    const auto invalidate_members = [&] {
        for (const auto* member : members) {
            static_cast<LlvmProcessExecutor*>(member->executor)
                ->invalidate_pure_wave_member_binding();
        }
    };
    std::optional<std::size_t> completed;
    try {
        completed = jit_.try_resume_pure_wave_prepared_members_prevalidated(
            members, task_ends);
    } catch (...) {
        invalidate_members();
        throw;
    }
    if (!completed) {
        invalidate_members();
        return std::nullopt;
    }
    if (*completed == 0U || *completed > task_ends.size()) {
        invalidate_members();
        throw compiler::LlvmJitError(
            "pure wave executor returned an invalid task prefix");
    }
    const auto member_end = task_ends[*completed - 1U];
    try {
        const bool staged
            = shared_context.write_validated_prepared_update_slot_batches(
                members.first(member_end));
        if (!staged) {
            invalidate_members();
        }
        return runtime::simir::PureWaveCompletion {
            *completed, staged
        };
    } catch (...) {
        invalidate_members();
        throw;
    }
}

void LlvmProcessExecutor::flush_pure_wave_updates(
    runtime::simir::ProcessExecutionContext& context)
{
    flush_buffered_updates(context, false);
}

[[nodiscard]] std::size_t LlvmProcessExecutor::resume_cohort_impl(
    const std::span<runtime::simir::ProcessCohortResumeEntry> entries,
    const runtime::simir::ProcessCohortNativeContext* native_context)
{
    if (entries.size() < 2U) {
        return 0U;
    }
    for (const auto& entry : entries) {
        if (entry.executor == nullptr
            || entry.executor->cohort_domain() != &jit_
            || entry.context == nullptr) {
            return 0U;
        }
    }
    for (const auto& entry : entries) {
        static_cast<LlvmProcessExecutor*>(entry.executor)
            ->invalidate_pure_wave_member_binding();
    }
    static const bool bound_cohort_enabled
        = std::getenv("FSIM_DISABLE_BOUND_COHORT") == nullptr;

    bool cache_matches = cohort_members_.size() == entries.size()
        && std::ranges::equal(
            entries,
            cohort_members_,
            { },
            [](const auto& entry) {
                return static_cast<LlvmProcessExecutor*>(entry.executor);
            },
            std::identity { })
        && std::ranges::equal(
            entries,
            cohort_start_instructions_,
            { },
            &runtime::simir::ProcessCohortResumeEntry::start_instruction,
            std::identity { });
    if (cache_matches) {
        for (std::size_t index = 0; index < entries.size(); ++index) {
            const auto& source = entries[index];
            const auto& executor = *static_cast<LlvmProcessExecutor*>(
                source.executor);
            const auto& cached = cohort_native_entries_[index];
            if (executor.instance_generation_
                    != cohort_member_generations_[index]
                || cached.queued
                    != reinterpret_cast<std::uint8_t*>(source.queued)
                || cached.waiting_on_static
                    != reinterpret_cast<std::uint8_t*>(
                        source.waiting_on_static)
                || cached.process_status
                    != reinterpret_cast<std::uint8_t*>(source.status)) {
                cache_matches = false;
                break;
            }
        }
    }
    if (!cache_matches) {
        if (cohort_generation_
            == std::numeric_limits<std::uint64_t>::max()) {
            throw compiler::LlvmJitError(
                "compiled cohort generation exhausted");
        }
        ++cohort_generation_;
        if (compact_pure_cohort_binding_) {
            (void)jit_.release_cohort_binding(
                compact_pure_cohort_binding_);
        }
        if (pure_cohort_binding_) {
            (void)jit_.release_cohort_binding(pure_cohort_binding_);
        }
        if (cohort_binding_) {
            (void)jit_.release_cohort_binding(cohort_binding_);
        }
        cohort_members_.clear();
        cohort_member_generations_.clear();
        cohort_start_instructions_.clear();
        cohort_native_entries_.clear();
        cohort_update_batches_.clear();
        cohort_logic9_update_batches_.clear();
        cohort_binding_ = { };
        pure_cohort_binding_ = { };
        compact_pure_cohort_binding_ = { };
        pure_cohort_native_context_.reset();
        pure_cohort_checked_ = false;
        cohort_binding_generation_ = 0U;
        cohort_members_.reserve(entries.size());
        cohort_member_generations_.reserve(entries.size());
        cohort_start_instructions_.reserve(entries.size());
        cohort_native_entries_.reserve(entries.size());
        cohort_update_batches_.reserve(entries.size());
        cohort_logic9_update_batches_.reserve(entries.size());
    }
    if (cohort_binding_
        && cohort_binding_generation_ != cohort_generation_) {
        throw compiler::LlvmJitError(
            "compiled cohort binding generation is stale");
    }
    const auto native_context_matches = [&] {
        if (native_context == nullptr || !pure_cohort_native_context_) {
            return false;
        }
        const auto& cached = *pure_cohort_native_context_;
        return native_context->owner != nullptr
            && native_context->owner == cached.owner
            && native_context->signal_aval.data()
                == cached.signal_aval.data()
            && native_context->signal_aval.size()
                == cached.signal_aval.size()
            && native_context->signal_bval.data()
                == cached.signal_bval.data()
            && native_context->signal_bval.size()
                == cached.signal_bval.size()
            && native_context->signal_writer_revision
                == cached.signal_writer_revision
            && native_context->supports_direct_word_updates
            && !native_context->execution_points_enabled;
    };
    if (native_context != nullptr
        && (pure_cohort_binding_ || compact_pure_cohort_binding_)
        && !native_context_matches()) {
        if (compact_pure_cohort_binding_) {
            (void)jit_.release_cohort_binding(
                compact_pure_cohort_binding_);
        }
        if (pure_cohort_binding_) {
            (void)jit_.release_cohort_binding(pure_cohort_binding_);
        }
        pure_cohort_binding_ = { };
        compact_pure_cohort_binding_ = { };
        pure_cohort_native_context_.reset();
        pure_cohort_checked_ = false;
    }
    const bool certified_context = native_context_matches();
    const bool pure_ready = bound_cohort_enabled && cache_matches
        && (pure_cohort_binding_
            || (certified_context && compact_pure_cohort_binding_))
        && (native_context == nullptr || certified_context)
        && !jit_process_profile().enabled
        && std::ranges::all_of(
            entries, [&](const auto& entry) {
                const auto& executor
                    = *static_cast<LlvmProcessExecutor*>(entry.executor);
                const auto& runtime = executor.runtime_;
                const bool mutable_ready = entry.start_instruction == 9U
                    && executor.frame_.program_counter == 9U
                    && executor.frame_.state
                        == FSIM_JIT_FRAME_STATE_READY
                    && executor.frame_.last_instruction == 8U
                    && executor.frame_.native_call_depth == 0U
                    && executor.cohort_resume_mode_
                        == CohortResumeMode::normal
                    && (!certified_context
                        || executor.prepared_cohort_domain_
                            == native_context->owner)
                    && runtime.flags == 0U
                    && !executor.callback_state_.failure
                    && executor.pending_update_words_.empty()
                    && !executor.has_buffered_update_words()
                    && !executor.has_buffered_logic9_updates();
                if (!mutable_ready) {
                    return false;
                }
                if (certified_context) {
                    return true;
                }
                const auto direct_aval
                    = entry.context->direct_signal_aval();
                const auto direct_bval
                    = entry.context->direct_signal_bval();
                return runtime.abi_version
                        == FSIM_JIT_RUNTIME_ABI_VERSION_V1
                    && !entry.context->execution_points_enabled()
                    && entry.context->supports_direct_word_updates()
                    && direct_aval.data() == runtime.direct_signal_aval
                    && direct_bval.data() == runtime.direct_signal_bval
                    && direct_aval.size() == direct_bval.size()
                    && direct_aval.size() == runtime.direct_signal_count
                    && runtime.direct_read_signals
                        == executor.direct_read_signals_.data()
                    && runtime.direct_read_signal_count
                        == executor.direct_read_signals_.size()
                    && runtime.direct_update_slots
                        == executor.direct_update_slots_.data()
                    && runtime.direct_update_slot_count
                        == executor.direct_update_slots_.size()
                    && runtime.direct_update_active_words
                        == executor.direct_update_active_words_.data()
                    && entry.context->signal_writer_revision()
                        == executor.direct_update_writer_revision_;
            });
    bool compact_executed { };
    if (pure_ready && certified_context
        && compact_pure_cohort_binding_) {
        compact_executed
            = jit_.try_resume_compact_logic4_bit_and_cohort_prevalidated(
                compact_pure_cohort_binding_);
    }
    std::optional<std::size_t> pure_executed;
    if (compact_executed) {
        pure_executed = entries.size();
    } else if (pure_ready && pure_cohort_binding_) {
        auto native_entries = std::span {
            cohort_native_entries_.data(), entries.size() };
        pure_executed = certified_context
            ? jit_.try_resume_logic4_bit_and_cohort_trusted_prevalidated(
                  pure_cohort_binding_, native_entries)
            : jit_.try_resume_logic4_bit_and_cohort_prevalidated(
                  pure_cohort_binding_, native_entries);
    }
    if (!pure_executed) {
        for (const auto& entry : entries) {
            auto& executor = *static_cast<LlvmProcessExecutor*>(
                entry.executor);
            executor.cohort_resume_mode_ = CohortResumeMode::prepare;
            try {
                static_cast<void>(executor.resume(
                    *entry.context, entry.start_instruction));
            } catch (...) {
                for (const auto& reset_entry : entries) {
                    auto& reset = *static_cast<LlvmProcessExecutor*>(
                        reset_entry.executor);
                    reset.cohort_resume_mode_ = CohortResumeMode::normal;
                }
                return 0U;
            }
            executor.cohort_resume_result_ = { };
            executor.cohort_resume_result_.abi_version
                = FSIM_JIT_RESUME_RESULT_ABI_VERSION_V1;
            executor.cohort_resume_result_.struct_size
                = sizeof(executor.cohort_resume_result_);
            if (!cache_matches) {
                cohort_members_.push_back(&executor);
                cohort_member_generations_.push_back(
                    executor.instance_generation_);
                cohort_start_instructions_.push_back(
                    entry.start_instruction);
                cohort_native_entries_.push_back(
                    compiler::JitProcessCohortResumeEntry {
                        executor.binding_, executor.runtime_,
                        executor.frame_, executor.cohort_resume_result_,
                        reinterpret_cast<std::uint8_t*>(entry.queued),
                        reinterpret_cast<std::uint8_t*>(
                            entry.waiting_on_static),
                        reinterpret_cast<std::uint8_t*>(entry.status)
                    });
            }
        }
        for (auto& native : cohort_native_entries_) {
            native.failure = { };
            native.status
                = std::numeric_limits<std::uint32_t>::max();
        }
    }
    auto native_entries
        = std::span { cohort_native_entries_.data(), entries.size() };
    const auto executed = pure_executed
        ? *pure_executed
        : bound_cohort_enabled && cohort_binding_
            ? jit_.resume_cohort_prevalidated(
                  cohort_binding_, native_entries)
            : jit_.resume_cohort_prevalidated(native_entries);
    if (!pure_executed && bound_cohort_enabled && !cohort_binding_) {
        cohort_binding_ = jit_.bind_cohort_prevalidated(native_entries);
        cohort_binding_generation_ = cohort_generation_;
    }
    bool cohort_updates_flushed { };
    static const bool cohort_update_batches_enabled
        = std::getenv("FSIM_DISABLE_COHORT_UPDATE_BATCH") == nullptr;
    if (cohort_update_batches_enabled
        && executed == entries.size() && !jit_process_profile().enabled) {
        bool compatible = compact_executed;
        if (!compact_executed) {
            const auto* const update_domain
                = entries.front().context->direct_update_domain();
            compatible = update_domain != nullptr
                && std::ranges::all_of(
                    entries,
                    [&](const auto& entry) {
                        return entry.context->direct_update_domain()
                            == update_domain;
                    })
                && std::ranges::all_of(
                    native_entries,
                    [](const auto& entry) {
                        return entry.status
                                == FSIM_JIT_RESUME_STATUS_WAIT_SENSITIVITY
                            && !entry.failure;
                    })
                && std::ranges::all_of(
                    entries,
                    [](const auto& entry) {
                        const auto& executor
                            = *static_cast<LlvmProcessExecutor*>(
                                entry.executor);
                        return !executor.callback_state_.failure;
                    });
        }
        if (compatible) {
            try {
                cohort_update_batches_.clear();
                cohort_logic9_update_batches_.clear();
                for (std::size_t index = 0; index < executed; ++index) {
                    auto& executor = *static_cast<LlvmProcessExecutor*>(
                        entries[index].executor);
                    if (!compact_executed
                        && executor.has_buffered_logic9_updates()) {
                        cohort_logic9_update_batches_.push_back({
                            executor.process_.id,
                            executor.buffered_logic9_update_views_
                        });
                    }
                    if (executor.has_buffered_update_words()) {
                        cohort_update_batches_.push_back({
                            executor.process_.id,
                            executor.direct_update_slot_views_,
                            executor.direct_update_active_words_
                        });
                    }
                }
                bool logic9_flushed = compact_executed
                    || cohort_logic9_update_batches_.empty()
                    || entries.front().context
                           ->write_validated_logic9_update_batches(
                               std::span {
                                   cohort_logic9_update_batches_.data(),
                                   cohort_logic9_update_batches_.size() });
                if (!logic9_flushed) {
                    for (std::size_t index = 0; index < executed; ++index) {
                        auto& executor = *static_cast<LlvmProcessExecutor*>(
                            entries[index].executor);
                        if (executor.has_buffered_logic9_updates()) {
                            executor.flush_buffered_logic9_updates(
                                *entries[index].context);
                        }
                    }
                    logic9_flushed = true;
                }
                const bool update_words_flushed
                    = cohort_update_batches_.empty()
                    || entries.front().context
                           ->write_validated_update_slot_batches(
                               std::span {
                                   cohort_update_batches_.data(),
                                   cohort_update_batches_.size() });
                cohort_updates_flushed
                    = logic9_flushed && update_words_flushed;
            } catch (...) {
                entries.front().failure = std::current_exception();
                return 1U;
            }
        }
    }
    if (compact_executed) {
        for (std::size_t index = 0; index < executed; ++index) {
            auto& entry = entries[index];
            auto& executor = *static_cast<LlvmProcessExecutor*>(
                entry.executor);
            try {
                if (!cohort_updates_flushed) {
                    executor.flush_buffered_updates(*entry.context, false);
                }
                entry.result = runtime::simir::ProcessResumeResult { 8U, 9U };
                entry.result.external.kind
                    = runtime::simir::ExternalSuspendKind::
                        validated_wait_sensitivity;
            } catch (...) {
                entry.failure = std::current_exception();
                return index + 1U;
            }
        }
        return executed;
    }
    for (std::size_t index = 0; index < executed; ++index) {
        auto& entry = entries[index];
        auto& executor = *static_cast<LlvmProcessExecutor*>(entry.executor);
        const auto status = static_cast<compiler::JitResumeStatus>(
            native_entries[index].status);
        const bool callback_failed
            = static_cast<bool>(executor.callback_state_.failure);
        if (status == compiler::JitResumeStatus::wait_sensitivity
            && !callback_failed && !native_entries[index].failure
            && !jit_process_profile().enabled) {
            try {
                if (!cohort_updates_flushed) {
                    executor.flush_buffered_updates(*entry.context, false);
                }
                if (executor.has_container_registers_
                    && !executor.active_container_object_aliases_.empty()) {
                    executor.discard_container_object_aliases();
                }
                const auto& result = executor.cohort_resume_result_;
                if (result.instruction >= executor.operation_count_) {
                    throw compiler::LlvmJitError(
                        "compiled cohort process returned an invalid "
                        "boundary instruction");
                }
                if (executor.frame_.program_counter
                    != result.instruction + 1U) {
                    throw compiler::LlvmJitError(
                        "compiled cohort process returned a non-sequential "
                        "WaitSensitivity PC");
                }
                entry.result = runtime::simir::ProcessResumeResult {
                    result.instruction, executor.frame_.program_counter
                };
                entry.result.external.kind
                    = executor.validated_static_sensitivity_
                    ? runtime::simir::ExternalSuspendKind::validated_wait_sensitivity
                    : runtime::simir::ExternalSuspendKind::wait_sensitivity;
                continue;
            } catch (...) {
                entry.failure = std::current_exception();
                return index + 1U;
            }
        }
        executor.cohort_resume_status_ = native_entries[index].status;
        executor.cohort_resume_failure_
            = callback_failed
            ? executor.callback_state_.failure
            : native_entries[index].failure;
        executor.cohort_resume_mode_ = CohortResumeMode::consume;
        try {
            entry.result = executor.resume(
                *entry.context, entry.start_instruction);
        } catch (...) {
            entry.failure = std::current_exception();
            return index + 1U;
        }
    }
    if (!pure_executed && !pure_cohort_checked_
        && bound_cohort_enabled && cohort_binding_
        && executed == entries.size()
        && !jit_process_profile().enabled
        && std::ranges::all_of(
            entries, [](const auto& entry) {
                return entry.start_instruction == 9U;
            })
        && std::ranges::all_of(
            native_entries, [](const auto& native) {
                return native.status
                        == FSIM_JIT_RESUME_STATUS_WAIT_SENSITIVITY
                    && !native.failure;
            })) {
        pure_cohort_checked_ = true;
        try {
            std::vector<compiler::JitProcessCohortLogic4BitAndMember>
                members;
            members.reserve(entries.size());
            for (const auto& entry : entries) {
                const auto& executor
                    = *static_cast<LlvmProcessExecutor*>(entry.executor);
                const auto assignment
                    = classify_pure_bit_and_assignment(
                        executor.process_, executor.layout_,
                        executor.signal_widths_,
                        executor.signal_value_kinds_,
                        executor.direct_read_signals_,
                        executor.direct_update_signals_);
                if (!assignment
                    || executor.frame_.program_counter != 9U
                    || executor.frame_.last_instruction != 8U
                    || executor.frame_.state
                        != FSIM_JIT_FRAME_STATE_READY
                    || executor.frame_.native_call_depth != 0U
                    || executor.callback_state_.failure) {
                    break;
                }
                const auto member = make_pure_bit_and_native_member(
                    *assignment, executor.process_, executor.layout_,
                    executor.direct_read_signals_,
                    executor.direct_update_signals_);
                if (!member) {
                    break;
                }
                members.push_back(*member);
            }
            if (members.size() == entries.size()) {
                const bool native_context_valid = native_context != nullptr
                    && native_context->owner != nullptr
                    && native_context->supports_direct_word_updates
                    && !native_context->execution_points_enabled
                    && !native_context->signal_aval.empty()
                    && native_context->signal_aval.size()
                        == native_context->signal_bval.size()
                    && std::ranges::all_of(
                        entries, [&](const auto& entry) {
                            const auto& executor
                                = *static_cast<LlvmProcessExecutor*>(
                                    entry.executor);
                            const auto& runtime = executor.runtime_;
                            const auto aval
                                = entry.context->direct_signal_aval();
                            const auto bval
                                = entry.context->direct_signal_bval();
                            return entry.context->direct_update_domain()
                                    == native_context->owner
                                && aval.data()
                                    == native_context->signal_aval.data()
                                && aval.size()
                                    == native_context->signal_aval.size()
                                && bval.data()
                                    == native_context->signal_bval.data()
                                && bval.size()
                                    == native_context->signal_bval.size()
                                && entry.context->signal_writer_revision()
                                    == native_context->signal_writer_revision
                                && entry.context
                                       ->supports_direct_word_updates()
                                && !entry.context
                                        ->execution_points_enabled()
                                && executor.direct_update_writer_revision_
                                    == native_context->signal_writer_revision
                                && executor.prepared_cohort_domain_
                                    == native_context->owner
                                && runtime.direct_signal_aval == aval.data()
                                && runtime.direct_signal_bval == bval.data()
                                && runtime.direct_signal_count == aval.size()
                                && runtime.direct_read_signals
                                    == executor.direct_read_signals_.data()
                                && runtime.direct_read_signal_count
                                    == executor.direct_read_signals_.size()
                                && runtime.direct_update_slots
                                    == executor.direct_update_slots_.data()
                                && runtime.direct_update_slot_count
                                    == executor.direct_update_slots_.size()
                                && runtime.direct_update_active_words
                                    == executor.direct_update_active_words_.data();
                        });
                if (native_context == nullptr || native_context_valid) {
                    const bool fully_transient
                        = native_context_valid
                        && std::ranges::all_of(
                            members, [](const auto& member) {
                                return !member.read_lhs.frame_resident
                                    && !member.read_rhs.frame_resident
                                    && !member.extract_lhs.frame_resident
                                    && !member.extract_rhs.frame_resident
                                    && !member.result.frame_resident;
                            })
                        && std::ranges::all_of(
                            entries, [](const auto& entry) {
                                const auto& executor
                                    = *static_cast<LlvmProcessExecutor*>(
                                        entry.executor);
                                return executor.validated_static_sensitivity_;
                            });
                    if (fully_transient) {
                        const auto bound
                            = jit_.bind_compact_logic4_bit_and_cohort_prevalidated(
                                native_entries, members);
                        compact_pure_cohort_binding_ = bound.value_or(
                            compiler::JitProcessCohortBinding { });
                    }
                    if (!compact_pure_cohort_binding_) {
                        pure_cohort_binding_
                            = jit_.bind_logic4_bit_and_cohort_prevalidated(
                                  native_entries, members)
                                  .value_or(
                                      compiler::JitProcessCohortBinding { });
                    }
                    if ((compact_pure_cohort_binding_
                            || pure_cohort_binding_)
                        && native_context_valid) {
                        pure_cohort_native_context_ = *native_context;
                    }
                }
            }
        } catch (const std::bad_alloc&) {
            // Native fusion is optional after the generic activation commits.
        } catch (const compiler::LlvmJitError&) {
            // Native fusion is optional after the generic activation commits.
        }
    }
    return executed;
}

[[nodiscard]] std::size_t LlvmProcessExecutor::resume_region(
    const std::span<runtime::simir::ProcessCohortResumeEntry> entries,
    const std::span<const std::size_t> active_indices)
{
    if (entries.size() < 2U || active_indices.empty()
        || !std::ranges::is_sorted(active_indices)
        || std::ranges::adjacent_find(active_indices)
            != active_indices.end()
        || active_indices.back() >= entries.size()) {
        return 0U;
    }
    for (const auto& entry : entries) {
        if (entry.executor == nullptr || entry.context == nullptr
            || entry.active == nullptr
            || entry.executor->cohort_domain() != &jit_) {
            return 0U;
        }
    }
    for (const auto& entry : entries) {
        static_cast<LlvmProcessExecutor*>(entry.executor)
            ->invalidate_pure_wave_member_binding();
    }
    bool cache_matches = region_members_.size() == entries.size();
    if (cache_matches) {
        for (std::size_t index = 0; index < entries.size(); ++index) {
            const auto& entry = entries[index];
            const auto& executor = *static_cast<LlvmProcessExecutor*>(
                entry.executor);
            const auto& native = region_native_entries_[index];
            if (entry.executor != region_members_[index]
                || executor.instance_generation_
                    != region_member_generations_[index]
                || entry.context != region_contexts_[index]
                || entry.start_instruction
                    != region_start_instructions_[index]
                || native.queued
                    != reinterpret_cast<std::uint8_t*>(entry.queued)
                || native.waiting_on_static
                    != reinterpret_cast<std::uint8_t*>(
                        entry.waiting_on_static)
                || native.process_status
                    != reinterpret_cast<std::uint8_t*>(entry.status)
                || native.active != entry.active) {
                cache_matches = false;
                break;
            }
        }
    }
    if (!cache_matches) {
        if (region_generation_
            == std::numeric_limits<std::uint64_t>::max()) {
            throw compiler::LlvmJitError(
                "compiled region generation exhausted");
        }
        ++region_generation_;
        region_members_.clear();
        region_member_generations_.clear();
        region_contexts_.clear();
        region_start_instructions_.clear();
        region_native_entries_.clear();
        region_update_batches_.clear();
        region_members_.reserve(entries.size());
        region_member_generations_.reserve(entries.size());
        region_contexts_.reserve(entries.size());
        region_start_instructions_.reserve(entries.size());
        region_native_entries_.reserve(entries.size());
        region_update_batches_.reserve(entries.size());
        try {
            for (const auto& entry : entries) {
                auto& executor = *static_cast<LlvmProcessExecutor*>(
                    entry.executor);
                executor.cohort_resume_mode_ = CohortResumeMode::prepare;
                static_cast<void>(executor.resume(
                    *entry.context, entry.start_instruction));
                executor.cohort_resume_result_ = { };
                executor.cohort_resume_result_.abi_version
                    = FSIM_JIT_RESUME_RESULT_ABI_VERSION_V1;
                executor.cohort_resume_result_.struct_size
                    = sizeof(executor.cohort_resume_result_);
                region_members_.push_back(&executor);
                region_member_generations_.push_back(
                    executor.instance_generation_);
                region_contexts_.push_back(entry.context);
                region_start_instructions_.push_back(
                    entry.start_instruction);
                region_native_entries_.push_back(
                    compiler::JitProcessCohortResumeEntry {
                        executor.binding_, executor.runtime_, executor.frame_,
                        executor.cohort_resume_result_,
                        reinterpret_cast<std::uint8_t*>(entry.queued),
                        reinterpret_cast<std::uint8_t*>(
                            entry.waiting_on_static),
                        reinterpret_cast<std::uint8_t*>(entry.status),
                        entry.active });
                region_update_batches_.push_back({
                    executor.process_.id,
                    executor.direct_update_slot_views_,
                    executor.direct_update_active_words_
                });
            }
        } catch (...) {
            for (const auto& entry : entries) {
                static_cast<LlvmProcessExecutor*>(entry.executor)
                    ->cohort_resume_mode_ = CohortResumeMode::normal;
            }
            region_members_.clear();
            region_member_generations_.clear();
            region_contexts_.clear();
            region_start_instructions_.clear();
            region_native_entries_.clear();
            region_update_batches_.clear();
            return 0U;
        }
    }

    region_active_update_batches_.clear();
    region_active_update_batches_.reserve(active_indices.size());
    for (const auto index : active_indices) {
        if (*entries[index].active == 0U
            || entries[index].executor != region_members_[index]
            || entries[index].context != region_contexts_[index]
            || entries[index].start_instruction
                != region_start_instructions_[index]) {
            return 0U;
        }
        auto& executor = *region_members_[index];
        executor.callback_state_.context = entries[index].context;
        executor.callback_state_.supports_direct_word_updates
            = entries[index].context->supports_direct_word_updates();
        executor.callback_state_.failure = { };
        invalidate_signal_read_cache(executor.callback_state_);
        executor.runtime_.context = &executor.callback_state_;
        executor.runtime_.flags
            = entries[index].context->execution_points_enabled()
            ? FSIM_JIT_RUNTIME_FLAG_DEBUG_POINTS
            : 0U;
        executor.cohort_resume_result_ = { };
        executor.cohort_resume_result_.abi_version
            = FSIM_JIT_RESUME_RESULT_ABI_VERSION_V1;
        executor.cohort_resume_result_.struct_size
            = sizeof(executor.cohort_resume_result_);
        region_native_entries_[index].failure = { };
    }

    const auto scanned = jit_.resume_region_prevalidated(
        region_native_entries_, active_indices);
    const auto active_end = std::ranges::lower_bound(
        active_indices, scanned);
    const auto executed_active = static_cast<std::size_t>(
        std::distance(active_indices.begin(), active_end));
    const bool all_wait = std::ranges::all_of(
        active_indices.first(executed_active),
        [&](const auto index) {
            const auto& executor = *region_members_[index];
            return region_native_entries_[index].status
                    == FSIM_JIT_RESUME_STATUS_WAIT_SENSITIVITY
                && !region_native_entries_[index].failure
                && !executor.callback_state_.failure;
        });
    bool updates_flushed { };
    if (all_wait && !jit_process_profile().enabled) {
        try {
            region_active_update_batches_.clear();
            for (const auto index
                : active_indices.first(executed_active)) {
                auto& executor = *region_members_[index];
                if (executor.has_buffered_logic9_updates()) {
                    executor.flush_buffered_logic9_updates(
                        *entries[index].context);
                }
                if (executor.has_buffered_update_words()) {
                    region_active_update_batches_.push_back(
                        region_update_batches_[index]);
                }
            }
            updates_flushed = region_active_update_batches_.empty()
                || entries[active_indices.front()].context
                       ->write_validated_update_slot_batches(
                           std::span {
                               region_active_update_batches_.data(),
                               region_active_update_batches_.size() });
        } catch (...) {
            entries[active_indices.front()].failure
                = std::current_exception();
            return scanned;
        }
    }

    for (const auto index : active_indices.first(executed_active)) {
        auto& entry = entries[index];
        auto& executor = *region_members_[index];
        const auto status = static_cast<compiler::JitResumeStatus>(
            region_native_entries_[index].status);
        const bool callback_failed
            = static_cast<bool>(executor.callback_state_.failure);
        if (all_wait && !jit_process_profile().enabled) {
            try {
                if (!updates_flushed) {
                    executor.flush_buffered_updates(*entry.context, false);
                }
                if (executor.has_container_registers_
                    && !executor.active_container_object_aliases_.empty()) {
                    executor.discard_container_object_aliases();
                }
                const auto& result = executor.cohort_resume_result_;
                if (result.instruction >= executor.operation_count_
                    || executor.frame_.program_counter
                        != result.instruction + 1U) {
                    throw compiler::LlvmJitError(
                        "compiled region process returned an invalid "
                        "WaitSensitivity boundary");
                }
                entry.result = runtime::simir::ProcessResumeResult {
                    result.instruction, executor.frame_.program_counter
                };
                entry.result.external.kind
                    = executor.validated_static_sensitivity_
                    ? runtime::simir::ExternalSuspendKind::validated_wait_sensitivity
                    : runtime::simir::ExternalSuspendKind::wait_sensitivity;
                continue;
            } catch (...) {
                entry.failure = std::current_exception();
                return index + 1U;
            }
        }
        executor.cohort_resume_status_
            = region_native_entries_[index].status;
        executor.cohort_resume_failure_
            = callback_failed
            ? executor.callback_state_.failure
            : region_native_entries_[index].failure;
        executor.cohort_resume_mode_ = CohortResumeMode::consume;
        try {
            entry.result = executor.resume(
                *entry.context, entry.start_instruction);
        } catch (...) {
            entry.failure = std::current_exception();
            return index + 1U;
        }
        (void)status;
    }
    return scanned;
}

[[nodiscard]] bool
LlvmProcessExecutor::cohort_manages_process_state() const noexcept
{
    static_assert(sizeof(bool) == sizeof(std::uint8_t));
    static_assert(sizeof(runtime::simir::ProcessStatus)
        == sizeof(std::uint8_t));
    static const bool enabled
        = std::getenv("FSIM_DISABLE_COHORT_PROCESS_STATE") == nullptr;
    return enabled;
}

[[nodiscard]] const void* LlvmProcessExecutor::cohort_domain() const noexcept
{
    return &jit_;
}

[[nodiscard]] PackedLogic4 LlvmProcessExecutor::read_register(
    const runtime::simir::RegisterId id,
    const std::size_t width) const
{
    const auto& layout = layout_;
    const auto resolved_width = id < layout.register_count
            && width == runtime::simir::ProcessExecutor::native_register_width
        ? layout.register_widths[id]
        : width;
    if (id >= layout.register_count
        || layout.register_widths[id] != resolved_width) {
        const auto layout_width = id < layout.register_count
            ? std::to_string(layout.register_widths[id])
            : std::string { "<missing>" };
        throw compiler::LlvmJitError {
            "compiled process '" + process_.name
            + "' debug-register request " + std::to_string(id) + " width "
            + std::to_string(resolved_width)
            + " does not match frame layout count "
            + std::to_string(layout.register_count) + " width " + layout_width
        };
    }
    if (resolved_width == 0) {
        return PackedLogic4 { };
    }
    if (layout.tracks_register_initialization
        && register_initialized_[id] == 0) {
        throw std::logic_error {
            "compiled process '" + process_.name + "' register "
            + std::to_string(id) + " has not been initialized at instruction "
            + std::to_string(frame_.program_counter)
        };
    }
    const auto kind = process_.register_value_kinds.empty()
        ? runtime::simir::ValueKind::logic4
        : process_.register_value_kinds[id];
    const auto offset = layout.register_word_offsets[id];
    const auto words = (resolved_width + 63U) / 64U;
    if (kind == runtime::simir::ValueKind::logic9) {
        return PackedLogic4::from_logic9_word_planes(
            resolved_width,
            std::span<const std::uint64_t> { register_aval_ }
                .subspan(offset, words),
            std::span<const std::uint64_t> { register_bval_ }
                .subspan(offset, words),
            std::span<const std::uint64_t> { register_logic9_plane2_ }
                .subspan(offset, words),
            std::span<const std::uint64_t> { register_logic9_plane3_ }
                .subspan(offset, words));
    }
    return PackedLogic4::from_word_planes(
        resolved_width,
        std::span<const std::uint64_t> { register_aval_ }.subspan(offset, words),
        std::span<const std::uint64_t> { register_bval_ }.subspan(offset, words));
}

[[nodiscard]] PackedLogic4 LlvmProcessExecutor::snapshot_register(
    const runtime::simir::RegisterId id) const
{
    const auto& layout = layout_;
    if (id >= layout.register_count) {
        throw compiler::LlvmJitError {
            "compiled automatic-frame register is outside the frame layout"
        };
    }
    if (register_initialized_[id] == 0) {
        return PackedLogic4 {
            layout.register_widths[id], runtime::Logic4::x
        };
    }
    return read_register(
        id, runtime::simir::ProcessExecutor::native_register_width);
}

void LlvmProcessExecutor::write_register(
    const runtime::simir::RegisterId id,
    const PackedLogic4& value)
{
    const auto& layout = layout_;
    if (id >= layout.register_count
        || layout.register_widths[id] != value.width()) {
        throw compiler::LlvmJitError {
            "compiled process register write is out of range"
        };
    }
    invalidate_pure_wave_member_binding();
    if (value.width() == 0) {
        register_initialized_[id] = 1;
        return;
    }
    const auto kind = process_.register_value_kinds.empty()
        ? runtime::simir::ValueKind::logic4
        : process_.register_value_kinds[id];
    const auto offset = layout.register_word_offsets[id];
    if (kind == runtime::simir::ValueKind::logic9) {
        const auto words = (value.width() + 63U) / 64U;
        if (value.is_logic9()) {
            std::ranges::copy(
                value.logic9_plane_words(0), register_aval_.begin() + offset);
            std::ranges::copy(
                value.logic9_plane_words(1), register_bval_.begin() + offset);
            std::ranges::copy(
                value.logic9_plane_words(2),
                register_logic9_plane2_.begin() + offset);
            std::ranges::copy(
                value.logic9_plane_words(3),
                register_logic9_plane3_.begin() + offset);
            register_initialized_[id] = 1;
            return;
        }
        for (std::size_t word = 0; word < words; ++word) {
            std::array<std::uint64_t, 4> planes { };
            const auto first_bit = word * 64U;
            const auto bits_in_word
                = std::min<std::size_t>(64U, value.width() - first_bit);
            for (std::size_t bit = 0; bit < bits_in_word; ++bit) {
                const auto encoded = static_cast<std::uint8_t>(
                    value.get_logic9(first_bit + bit));
                const auto mask = std::uint64_t { 1 } << bit;
                for (std::size_t plane = 0; plane < planes.size(); ++plane) {
                    if ((encoded & (1U << plane)) != 0U) {
                        planes[plane] |= mask;
                    }
                }
            }
            register_aval_[offset + word] = planes[0];
            register_bval_[offset + word] = planes[1];
            register_logic9_plane2_[offset + word] = planes[2];
            register_logic9_plane3_[offset + word] = planes[3];
        }
    } else {
        std::ranges::copy(
            value.aval_words(), register_aval_.begin() + offset);
        std::ranges::copy(
            value.bval_words(), register_bval_.begin() + offset);
    }
    register_initialized_[id] = 1;
}

template <typename Boundary>
void LlvmProcessExecutor::require_boundary(
    const runtime::simir::InstructionIndex instruction,
    const std::string_view status) const
{
    if (!fsim::runtime::simir::operation_holds<Boundary>(
            process_.operations[instruction])) {
        throw compiler::LlvmJitError(
            "compiled process reported " + std::string { status }
            + " at the wrong SimIR instruction");
    }
}

void LlvmProcessExecutor::capture_failure(CallbackState& state) noexcept
{
    if (!state.failure) {
        state.failure = std::current_exception();
    }
}

std::uint32_t LlvmProcessExecutor::record_code_coverage_counter(
    void* context,
    const std::uint32_t process,
    const std::uint32_t instruction,
    const std::uint32_t counter) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
        return 1U;
    }
    try {
        const auto status = state.context->record_code_coverage_counter(
            ::fsim::runtime::CodeCoverageCounterId { counter });
        if (status
            == runtime::simir::CodeCoverageCounterRuntimeStatus::Unavailable) {
            throw runtime::simir::InterpreterError(
                process,
                instruction,
                "code coverage counter service is unavailable");
        }
        if (status
            == runtime::simir::CodeCoverageCounterRuntimeStatus::OutOfRange) {
            throw runtime::simir::InterpreterError(
                process,
                instruction,
                "code coverage counter is out of range");
        }
        return 0U;
    } catch (...) {
        capture_failure(state);
        return 1U;
    }
}

std::uint32_t LlvmProcessExecutor::sample_coverage(
    void* context,
    const std::uint32_t process,
    const std::uint32_t instruction,
    fsim_jit_frame_v1* frame) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
        return 1U;
    }
    try {
        if (state.executor == nullptr || state.context == nullptr
            || state.process == nullptr || state.generated_process != process
            || instruction >= state.process->operations.size()
            || frame != &state.executor->frame_) {
            throw std::logic_error("invalid native coverage sample callback");
        }
        const auto expanded
            = state.process->operations.expanded(instruction);
        const auto* sample = runtime::simir::operation_get_if<
            runtime::simir::CoverageSample>(&expanded);
        if (sample == nullptr) {
            throw std::logic_error(
                "native coverage sample callback targets another operation");
        }
        std::vector<PackedLogic4> actuals;
        actuals.reserve(sample->actuals.size());
        for (std::size_t index = 0; index < sample->actuals.size(); ++index) {
            actuals.push_back(state.executor->read_register(
                sample->actuals[index], sample->actual_widths[index]));
        }
        state.context->sample_coverage(*sample, actuals, instruction);
        return 0U;
    } catch (...) {
        capture_failure(state);
        return 1U;
    }
}

std::uint32_t LlvmProcessExecutor::execute_class_property_operation(
    void* context,
    const std::uint32_t process,
    const std::uint32_t instruction,
    fsim_jit_frame_v1* frame) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
        return 1U;
    }
    try {
        if (state.executor == nullptr || state.context == nullptr
            || state.process == nullptr || state.generated_process != process
            || instruction >= state.process->operations.size()
            || frame != &state.executor->frame_) {
            throw std::logic_error(
                "invalid native class-property callback");
        }
        const auto expanded
            = state.process->operations.expanded(instruction);
        const auto& stored = expanded;
        const auto read_handle = [&](const runtime::simir::RegisterId id) {
            return state.executor->read_register(
                id,
                runtime::simir::ProcessExecutor::native_register_width)
                .low_word().aval;
        };
        if (const auto* read = runtime::simir::operation_get_if<
                runtime::simir::ClassPropertyRead>(&stored)) {
            state.executor->write_register(
                read->destination,
                state.context->read_class_property(
                    read_handle(read->receiver), read->property_identity,
                    read->width, instruction));
        } else if (const auto* write = runtime::simir::operation_get_if<
                       runtime::simir::ClassPropertyWrite>(&stored)) {
            state.context->write_class_property(
                read_handle(write->receiver), write->property_identity,
                state.executor->read_register(
                    write->source,
                    state.executor->layout_.register_widths.at(
                        write->source)),
                instruction);
        } else if (const auto* static_read = runtime::simir::operation_get_if<
                       runtime::simir::ClassStaticPropertyRead>(&stored)) {
            state.executor->write_register(
                static_read->destination,
                state.context->read_class_static_property(
                    static_read->property_identity,
                    static_read->width,
                    instruction));
        } else if (const auto* static_write = runtime::simir::operation_get_if<
                       runtime::simir::ClassStaticPropertyWrite>(&stored)) {
            state.context->write_class_static_property(
                static_write->property_identity,
                state.executor->read_register(
                    static_write->source,
                    state.executor->layout_.register_widths.at(
                        static_write->source)),
                instruction);
        } else {
            throw std::logic_error(
                "native class-property callback targets another operation");
        }
        return 0U;
    } catch (...) {
        capture_failure(state);
        return 1U;
    }
}

std::uint32_t LlvmProcessExecutor::query_event_triggered(
    void* context,
    const std::uint32_t process,
    const std::uint32_t instruction,
    fsim_jit_frame_v1* frame) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
        return 1U;
    }
    try {
        if (state.executor == nullptr || state.context == nullptr
            || state.process == nullptr || state.generated_process != process
            || instruction >= state.process->operations.size()
            || frame != &state.executor->frame_) {
            throw std::logic_error(
                "invalid native event-trigger callback");
        }
        const auto expanded
            = state.process->operations.expanded(instruction);
        const auto* query = runtime::simir::operation_get_if<
            runtime::simir::EventTriggered>(&expanded);
        if (query == nullptr) {
            throw std::logic_error(
                "native event-trigger callback targets another operation");
        }
        const auto triggered
            = state.context->event_triggered(query->event, instruction);
        state.executor->write_register(
            query->destination,
            PackedLogic4::from_aval_bval(1U, triggered ? 1U : 0U, 0U));
        return 0U;
    } catch (...) {
        capture_failure(state);
        return 1U;
    }
}

void LlvmProcessExecutor::invalidate_signal_read_cache(
    CallbackState& state) noexcept
{
    state.signal_read_cache_ids.fill(
        std::numeric_limits<std::uint32_t>::max());
}

bool LlvmProcessExecutor::has_buffered_update_words() const noexcept
{
    if (!pending_update_words_.empty()) {
        return true;
    }
    if (!direct_update_active_words_.empty()) {
        if (std::ranges::any_of(
            direct_update_active_words_,
            [](const std::uint64_t active) { return active != 0U; })) {
            return true;
        }
    }
    return std::ranges::any_of(
        direct_update_slots_,
        [](const fsim_jit_update_slot_v1& slot) {
            return slot.active != 0U;
        });
}

bool LlvmProcessExecutor::has_buffered_logic9_updates() const noexcept
{
    return std::ranges::any_of(
        buffered_logic9_update_views_,
        [](const runtime::simir::ProcessLogic9UpdateSlotView& slot) {
            return slot.mask != nullptr && *slot.mask != 0U;
        });
}

void LlvmProcessExecutor::flush_buffered_updates(
    runtime::simir::ProcessExecutionContext& context,
    const bool allow_slot_batch)
{
    flush_buffered_logic9_updates(context);

    // Keep queued callback words on the existing word-update path.
    if (allow_slot_batch && pending_update_words_.empty()
        && has_buffered_update_words()
        && !direct_update_slot_views_.empty()
        && callback_state_.supports_direct_word_updates
        && !jit_process_profile().enabled
        && context.direct_update_domain() != nullptr) {
        auto batch = runtime::simir::ProcessUpdateSlotBatch {
            process_.id,
            direct_update_slot_views_,
            direct_update_active_words_
        };
        if (context.write_validated_update_slot_batches(
                std::span { &batch, 1U })) {
            return;
        }
    }
    flush_update_words(context);
}

bool LlvmProcessExecutor::buffer_logic9_update(
    const runtime::simir::SignalId signal,
    const std::uint32_t offset,
    const std::uint32_t width,
    const fsim_jit_logic9_word_v1& value)
{
    const auto found = buffered_logic9_updates_.size() == 1U
        ? buffered_logic9_updates_.begin()
        : std::ranges::lower_bound(
            buffered_logic9_updates_, signal, { },
            &BufferedLogic9Update::signal);
    if (found == buffered_logic9_updates_.end()
        || found->signal != signal) {
        return false;
    }
    if (width == 0U || offset > found->width
        || width > found->width - offset) {
        throw std::logic_error {
            "invalid generated Logic9 buffered update"
        };
    }
    const auto source_mask = width == 64U
        ? std::numeric_limits<std::uint64_t>::max()
        : (UINT64_C(1) << width) - UINT64_C(1);
    const auto target_mask = source_mask << offset;
    for (std::size_t plane = 0; plane < found->planes.size(); ++plane) {
        const auto shifted = (value.planes[plane] & source_mask) << offset;
        found->planes[plane]
            = (found->planes[plane] & ~target_mask)
            | (shifted & target_mask);
    }
    found->mask |= target_mask;
    return true;
}

void LlvmProcessExecutor::flush_buffered_logic9_updates(
    runtime::simir::ProcessExecutionContext& context)
{
    if (!has_buffered_logic9_updates()) {
        return;
    }
    if (context.write_validated_logic9_update_batch({
            process_.id, buffered_logic9_update_views_ })) {
        return;
    }
    for (const auto& slot : buffered_logic9_update_views_) {
        if (slot.mask == nullptr || slot.planes == nullptr) {
            continue;
        }
        auto remaining = std::exchange(*slot.mask, UINT64_C(0));
        if (remaining == 0U) {
            continue;
        }
        const auto full_mask = slot.width == 64U
            ? std::numeric_limits<std::uint64_t>::max()
            : (UINT64_C(1) << slot.width) - UINT64_C(1);
        remaining &= full_mask;
        if (remaining == full_mask) {
            context.write_update(
                slot.signal,
                PackedLogic4::from_logic9_word({
                    slot.width,
                    { slot.planes[0], slot.planes[1],
                        slot.planes[2], slot.planes[3] }
                }));
            continue;
        }
        std::uint32_t bit { };
        while (bit < slot.width) {
            while (bit < slot.width
                && ((remaining >> bit) & UINT64_C(1)) == 0U) {
                ++bit;
            }
            if (bit == slot.width) {
                break;
            }
            const auto offset = bit;
            while (bit < slot.width
                && ((remaining >> bit) & UINT64_C(1)) != 0U) {
                ++bit;
            }
            const auto run_width = bit - offset;
            const auto run_mask = run_width == 64U
                ? std::numeric_limits<std::uint64_t>::max()
                : (UINT64_C(1) << run_width) - UINT64_C(1);
            context.write_update_slice(
                slot.signal,
                PackedLogic4::from_logic9_word({
                    run_width,
                    { (slot.planes[0] >> offset) & run_mask,
                        (slot.planes[1] >> offset) & run_mask,
                        (slot.planes[2] >> offset) & run_mask,
                        (slot.planes[3] >> offset) & run_mask }
                }),
                offset);
        }
    }
}

void LlvmProcessExecutor::flush_update_words(
    runtime::simir::ProcessExecutionContext& context)
{
    if (!has_buffered_update_words()) {
        return;
    }
    const auto direct_owners = context.direct_single_driver_processes();
    const auto stable_owners = context.stable_single_writer_processes();
    const auto direct_aval = context.direct_signal_aval();
    const auto direct_bval = context.direct_signal_bval();
    const auto direct_wide_aval = context.direct_wide_signal_aval();
    const auto direct_wide_bval = context.direct_wide_signal_bval();
    const auto direct_wide_offsets = context.direct_wide_signal_offsets();
    const auto unchanged_direct_update = [&](
        const runtime::simir::SignalId signal,
        const fsim_jit_update_slot_v1& slot) {
        if (!stable_direct_update_suppression_allowed_
            || signal >= direct_owners.size()
            || signal >= stable_owners.size()
            || direct_owners[signal] != process_.id
            || stable_owners[signal] != process_.id) {
            return false;
        }
        if (slot.width <= 64U) {
            return signal < direct_aval.size()
                && signal < direct_bval.size()
                && (((slot.aval ^ direct_aval[signal]) & slot.mask) == 0U)
                && (((slot.bval ^ direct_bval[signal]) & slot.mask) == 0U);
        }
        if (signal >= direct_wide_offsets.size()
            || direct_wide_aval.size() != direct_wide_bval.size()) {
            return false;
        }
        const auto offset = direct_wide_offsets[signal];
        if (offset > direct_wide_aval.size()
            || slot.word_count > direct_wide_aval.size() - offset) {
            return false;
        }
        for (std::uint32_t word = 0; word < slot.word_count; ++word) {
            const auto mask = slot.wide_mask[word];
            if ((((slot.wide_aval[word] ^ direct_wide_aval[offset + word])
                     & mask)
                    != 0U)
                || (((slot.wide_bval[word]
                          ^ direct_wide_bval[offset + word])
                         & mask)
                    != 0U)) {
                return false;
            }
        }
        return true;
    };
    const auto append_runs = [&](const runtime::simir::SignalId signal,
                                 const std::uint32_t word_offset,
                                 const std::uint32_t word_width,
                                 const std::uint64_t aval,
                                 const std::uint64_t bval,
                                 std::uint64_t remaining,
                                 const bool whole_signal) {
        const auto full_mask = word_width == 64U
            ? std::numeric_limits<std::uint64_t>::max()
            : (std::uint64_t { 1 } << word_width) - 1U;
        remaining &= full_mask;
        if (remaining == 0U) {
            return;
        }
        if (whole_signal && remaining == full_mask) {
            pending_update_words_.push_back({
                signal,
                { word_width, aval & full_mask, bval & full_mask },
                0U,
                false
            });
            return;
        }
        std::uint32_t bit { };
        while (bit < word_width) {
            while (bit < word_width
                && ((remaining >> bit) & UINT64_C(1)) == 0U) {
                ++bit;
            }
            if (bit == word_width) {
                break;
            }
            const auto offset = bit;
            while (bit < word_width
                && ((remaining >> bit) & UINT64_C(1)) != 0U) {
                ++bit;
            }
            const auto run_width = bit - offset;
            const auto run_mask = run_width == 64U
                ? std::numeric_limits<std::uint64_t>::max()
                : (std::uint64_t { 1 } << run_width) - 1U;
            pending_update_words_.push_back({
                signal,
                { run_width,
                    (aval >> offset) & run_mask,
                    (bval >> offset) & run_mask },
                word_offset + offset,
                true
            });
        }
    };
    const auto consume_index = [&](const std::size_t index) {
        auto& slot = direct_update_slots_[index];
        if (slot.active == 0U) {
            return;
        }
        slot.active = 0U;
        const auto signal = direct_update_signals_[index];
        const auto width = slot.width;
        if (unchanged_direct_update(signal, slot)) {
            if (width <= 64U) {
                slot.mask = 0U;
            } else {
                std::fill_n(slot.wide_mask, slot.word_count, UINT64_C(0));
            }
            return;
        }
        if (width <= 64U) {
            const auto remaining = slot.mask;
            slot.mask = 0U;
            append_runs(
                signal, 0U, width, slot.aval, slot.bval, remaining, true);
            return;
        }
        for (std::uint32_t word = 0; word < slot.word_count; ++word) {
            const auto offset = word * 64U;
            const auto word_width = std::min(64U, width - offset);
            const auto remaining = slot.wide_mask[word];
            slot.wide_mask[word] = 0U;
            append_runs(
                signal,
                offset,
                word_width,
                slot.wide_aval[word],
                slot.wide_bval[word],
                remaining,
                false);
        }
    };
    if (direct_update_active_words_.empty()) {
        for (std::size_t index = 0;
             index < direct_update_slots_.size(); ++index) {
            consume_index(index);
        }
    } else {
        for (std::size_t word_index = 0;
             word_index < direct_update_active_words_.size(); ++word_index) {
            auto active = direct_update_active_words_[word_index];
            while (active != 0U) {
                const auto bit = static_cast<std::size_t>(
                    std::countr_zero(active));
                const auto index = word_index * 64U + bit;
                if (index < direct_update_slots_.size()) {
                    consume_index(index);
                }
                active &= active - UINT64_C(1);
            }
        }
        std::ranges::fill(direct_update_active_words_, UINT64_C(0));
    }
    if (!pending_update_words_.empty()) {
        try {
            context.write_validated_update_words(pending_update_words_);
        } catch (...) {
            pending_update_words_.clear();
            throw;
        }
        pending_update_words_.clear();
    }
}

std::uint32_t LlvmProcessExecutor::mapped_signal_sparse(
    const CallbackState& state, const std::uint32_t signal)
{
    const auto found = std::ranges::lower_bound(
        state.signal_remap, signal, { }, &SignalRemap::value_type::first);
    if (found == state.signal_remap.end() || found->first != signal) {
        return signal;
    }
    return found->second;
}

std::uint32_t LlvmProcessExecutor::mapped_signal(
    const CallbackState& state, const std::uint32_t signal)
{
    if (!state.dense_signal_remap.empty()) {
        if (signal < state.dense_signal_remap_base) {
            return signal;
        }
        const auto offset = signal - state.dense_signal_remap_base;
        return offset < state.dense_signal_remap.size()
            ? state.dense_signal_remap[offset]
            : signal;
    }
    return state.signal_remap.empty()
        ? signal
        : mapped_signal_sparse(state, signal);
}




#endif

} // namespace fsim::app::application_detail
