// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_impl.hpp"
#include "llvm_jit_region_activation_internal.hpp"

#include <llvm/IR/Constants.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/Error.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <utility>

namespace fsim::compiler {
using namespace llvm_detail;
using runtime::simir::Process;
using runtime::simir::ValueKind;

namespace {

void validate_runtime_instance_v2(
    const fsim_jit_runtime_instance_v2& runtime)
{
    if (runtime.abi_version != FSIM_JIT_RUNTIME_ABI_VERSION_V2) {
        throw LlvmJitError("JIT runtime-instance ABI version mismatch");
    }
    if (runtime.struct_size < sizeof(fsim_jit_runtime_instance_v2)) {
        throw LlvmJitError("JIT runtime-instance ABI structure is too small");
    }
    if (runtime.services == nullptr) {
        throw LlvmJitError("JIT runtime-instance has no service table");
    }
    const auto& services = *runtime.services;
    if (services.abi_version != FSIM_JIT_SERVICES_ABI_VERSION_V2) {
        throw LlvmJitError("JIT services ABI version mismatch");
    }
    constexpr auto required_service_prefix
        = offsetof(fsim_jit_services_v2, write_projected_signal_packed);
    if (services.struct_size < required_service_prefix) {
        throw LlvmJitError("JIT services ABI structure is too small");
    }
    if (runtime.reserved != 0U
        || runtime.direct_update_reserved != 0U
        || runtime.direct_signal_reserved != 0U
        || runtime.direct_update_active_reserved != 0U
        || runtime.fused_activation_reserved != 0U) {
        throw LlvmJitError("JIT runtime-instance reserved fields are nonzero");
    }
    if (services.read_signal == nullptr
        || services.write_signal == nullptr
        || services.assert_failed == nullptr) {
        throw LlvmJitError("JIT services require the core callbacks");
    }
}

template <typename ProcessInfo>
void validate_native_service_callbacks(
    const ProcessInfo& info, const fsim_jit_runtime_instance_v2& runtime)
{
    validate_runtime_instance_v2(runtime);
    const auto& services = *runtime.services;
    if (info.uses_coverage_sample && services.sample_coverage == nullptr) {
        throw LlvmJitError(
            "JIT services require sample_coverage for this process");
    }
    if (info.uses_class_property_operation
        && services.execute_class_property_operation == nullptr) {
        throw LlvmJitError(
            "JIT services require class-property callbacks for this process");
    }
    if (info.uses_event_triggered && services.query_event_triggered == nullptr) {
        throw LlvmJitError(
            "JIT services require query_event_triggered for this process");
    }
    const auto require = [](const bool missing, const char* const callback) {
        if (missing) {
            throw LlvmJitError(
                std::string("JIT services require ") + callback
                + " for this process");
        }
    };
    require(info.uses_write_update && services.write_update == nullptr, "write_update");
    require(info.uses_write_after && services.write_after == nullptr, "write_after");
    require(info.uses_write_inertial && services.write_inertial == nullptr, "write_inertial");
    require(
        info.uses_write_blocking_slice && services.write_signal_slice == nullptr,
        "write_signal_slice");
    require(
        info.uses_write_update_slice && services.write_update_slice == nullptr,
        "write_update_slice");
    require(
        info.uses_write_after_slice && services.write_after_slice == nullptr,
        "write_after_slice");
    require(
        info.uses_write_inertial_slice && services.write_inertial_slice == nullptr,
        "write_inertial_slice");
    require(
        info.uses_force_signal_slice
            && (services.force_signal_slice == nullptr
                || (info.frame_layout.uses_logic9
                    && services.force_signal_slice_logic9 == nullptr)),
        "force_signal_slice callbacks");
    require(
        info.uses_release_signal_slice
            && services.release_signal_slice == nullptr,
        "release_signal_slice");
    require(
        info.uses_force_driver_signal_slice
            && (services.force_driver_signal_slice == nullptr
                || (info.frame_layout.uses_logic9
                    && services.force_driver_signal_slice_logic9 == nullptr)),
        "force_driver_signal_slice callbacks");
    require(
        info.uses_release_driver_signal_slice
            && services.release_driver_signal_slice == nullptr,
        "release_driver_signal_slice");
    require(info.uses_write_projected && services.write_projected == nullptr, "write_projected");
    require(
        info.uses_write_projected_slice && services.write_projected_slice == nullptr,
        "write_projected_slice");
    require(
        info.uses_write_projected_waveform
            && services.write_projected_waveform == nullptr,
        "write_projected_waveform");
    require(
        info.uses_write_projected_waveform_slice
            && services.write_projected_waveform_slice == nullptr,
        "write_projected_waveform_slice");
    require(info.uses_signal_event && services.signal_event == nullptr, "signal_event");
    require(
        info.uses_signal_last_value && services.signal_last_value == nullptr,
        "signal_last_value");
    require(
        info.uses_signal_last_event && services.signal_last_event == nullptr,
        "signal_last_event");
    require(
        info.uses_simulation_time && services.read_simulation_time == nullptr,
        "read_simulation_time");
    require(info.uses_vital_timing && services.vital_timing_check == nullptr, "vital_timing_check");
    require(info.uses_vital_delay && services.vital_delay == nullptr, "vital_delay");
    require(info.uses_signal_active && services.signal_active == nullptr, "signal_active");
    require(
        info.uses_signal_last_active && services.signal_last_active == nullptr,
        "signal_last_active");
    require(info.uses_signal_driving && services.signal_driving == nullptr, "signal_driving");
    require(info.uses_signal_driving_value
        && (services.signal_driving_value == nullptr
            || services.signal_driving_value_logic9 == nullptr),
        "signal_driving_value callbacks");
    require(info.uses_output && services.write_output == nullptr, "write_output");
    require(info.uses_postponed_output && services.schedule_output == nullptr, "schedule_output");
    require(info.uses_report && services.write_report == nullptr, "write_report");
    require(info.uses_formatted_output && services.write_formatted == nullptr, "write_formatted");
    require(info.uses_time_output && services.write_time == nullptr, "write_time");
    require(info.uses_monitor_install && services.install_monitor == nullptr, "install_monitor");
    require(info.uses_monitor_control && services.control_monitor == nullptr, "control_monitor");
    require(info.uses_random_value && services.random_value == nullptr, "random_value");
    require(
        info.uses_code_coverage
            && services.record_code_coverage_counter == nullptr,
        "record_code_coverage_counter");
    if (info.frame_layout.uses_logic9) {
        require(services.read_signal_logic9 == nullptr, "read_signal_logic9");
        require(services.write_signal_logic9 == nullptr, "write_signal_logic9");
        require(services.write_update_logic9 == nullptr, "write_update_logic9");
        require(services.write_after_logic9 == nullptr, "write_after_logic9");
        require(services.write_signal_slice_logic9 == nullptr, "write_signal_slice_logic9");
        require(services.write_update_slice_logic9 == nullptr, "write_update_slice_logic9");
        require(services.write_after_slice_logic9 == nullptr, "write_after_slice_logic9");
        require(services.signal_last_value_logic9 == nullptr, "signal_last_value_logic9");
        require(services.write_inertial_logic9 == nullptr, "write_inertial_logic9");
        require(services.write_inertial_slice_logic9 == nullptr, "write_inertial_slice_logic9");
        require(services.write_projected_logic9 == nullptr, "write_projected_logic9");
        require(services.write_projected_slice_logic9 == nullptr, "write_projected_slice_logic9");
        require(
            services.write_projected_waveform_logic9 == nullptr,
            "write_projected_waveform_logic9");
        require(
            services.write_projected_waveform_slice_logic9 == nullptr,
            "write_projected_waveform_slice_logic9");
        require(services.write_formatted_logic9 == nullptr, "write_formatted_logic9");
    }
    if (info.uses_strings) {
        require(services.load_string == nullptr || services.copy_string == nullptr
            || services.read_string_object == nullptr || services.write_string_object == nullptr
            || services.concatenate_strings == nullptr || services.compare_strings == nullptr
            || services.string_length == nullptr || services.string_index == nullptr
            || services.string_replace_byte == nullptr || services.write_string_output == nullptr,
            "mutable-string callbacks");
    }
    if (info.uses_files) {
        require(services.file_open == nullptr || services.file_close == nullptr
            || services.file_write == nullptr || services.file_read_line == nullptr
            || services.file_end_of_file == nullptr || services.file_error == nullptr,
            "text-file callbacks");
    }
    if (info.uses_containers) {
        require(services.container_operation == nullptr
            || services.container_read_word == nullptr
            || services.container_write_word == nullptr,
            "bounded-container callbacks");
    }
    if (info.uses_wide_container_operation) {
        require(services.container_read_packed == nullptr
            || services.container_write_packed == nullptr,
            "packed-container callbacks");
    }
    if (info.uses_container_read_index64) {
        constexpr auto index64_service_end
            = offsetof(fsim_jit_services_v2,
                container_read_packed_index64)
            + sizeof(services.container_read_packed_index64);
        if (services.struct_size < index64_service_end) {
            throw LlvmJitError(
                "JIT services ABI structure is too small for "
                "container_read_packed_index64");
        }
        require(services.container_read_packed_index64 == nullptr,
            "container_read_packed_index64");
    }
    require(info.uses_exact_signal_operation
        && services.execute_signal_operation == nullptr,
        "execute_signal_operation");
    require(info.uses_wide_signal_read && services.read_signal_packed == nullptr,
        "read_signal_packed");
    require(info.uses_wide_signal_write && services.write_signal_packed == nullptr,
        "write_signal_packed");
    if (info.uses_wide_projected_write) {
        constexpr auto projected_service_end
            = offsetof(fsim_jit_services_v2,
                write_projected_signal_packed)
            + sizeof(services.write_projected_signal_packed);
        if (services.struct_size < projected_service_end) {
            throw LlvmJitError(
                "JIT services ABI structure is too small for "
                "write_projected_signal_packed");
        }
        require(
            services.write_projected_signal_packed == nullptr,
            "write_projected_signal_packed");
    }
}

template <typename ProcessInfo>
void validate_required_direct_read_backing(
    const ProcessInfo& info,
    const fsim_jit_runtime_instance_v2& runtime,
    const std::optional<std::span<const JitDirectReadInstanceBinding>> bindings
        = std::nullopt)
{
    const auto& layout = info.frame_layout;
    const auto slot_count = layout.direct_read_signals.size();
    if (info.direct_read_widths.size() != slot_count
        || info.direct_read_value_kinds.size() != slot_count) {
        throw LlvmJitError(
            "JIT process direct-read metadata is inconsistent");
    }
    if (slot_count > std::numeric_limits<std::uint32_t>::max()
        || runtime.direct_read_signal_count != slot_count
        || (slot_count != 0U && runtime.direct_read_signals == nullptr)) {
        throw LlvmJitError(
            "JIT runtime ABI does not provide every required direct-read slot");
    }

    if (bindings && bindings->size() != slot_count) {
        throw LlvmJitError("JIT direct-read instance binding count is invalid");
    }
    for (std::size_t slot = 0U; slot < slot_count; ++slot) {
        const auto signal = bindings
            ? (*bindings)[slot].signal : layout.direct_read_signals[slot];
        const auto width = info.direct_read_widths[slot];
        const auto kind = info.direct_read_value_kinds[slot];
        if (bindings && ((*bindings)[slot].width != width
                || (*bindings)[slot].kind != kind)) {
            throw LlvmJitError(
                "JIT direct-read instance type differs from the compiled slot");
        }
        if (width == 0U
            || (kind != fsim::runtime::simir::ValueKind::logic4
                && kind != fsim::runtime::simir::ValueKind::logic9)
            || runtime.direct_read_signals[slot] != signal
            || signal >= runtime.direct_signal_count) {
            throw LlvmJitError(
                "JIT runtime ABI direct-read map does not match the compiled layout");
        }

        // These ABI planes are read-only for this entry. Equal base pointers
        // are valid and do not violate a generated no-alias assumption.
        if (width <= 64U) {
            if (runtime.direct_signal_aval == nullptr
                || runtime.direct_signal_bval == nullptr) {
                throw LlvmJitError(
                    "JIT runtime ABI narrow direct-read planes are null");
            }
            if (kind == fsim::runtime::simir::ValueKind::logic9
                && (runtime.direct_signal_logic9_plane0 == nullptr
                    || runtime.direct_signal_logic9_plane1 == nullptr
                    || runtime.direct_signal_logic9_plane2 == nullptr
                    || runtime.direct_signal_logic9_plane3 == nullptr)) {
                throw LlvmJitError(
                    "JIT runtime ABI narrow Logic9 direct-read planes are null");
            }
            continue;
        }

        if (runtime.direct_wide_signal_aval == nullptr
            || runtime.direct_wide_signal_bval == nullptr
            || runtime.direct_wide_signal_offsets == nullptr
            || signal >= runtime.direct_wide_signal_offset_count) {
            throw LlvmJitError(
                "JIT runtime ABI wide direct-read backing is incomplete");
        }
        const auto offset = runtime.direct_wide_signal_offsets[signal];
        const auto words = (static_cast<std::uint64_t>(width) + 63U) / 64U;
        if (offset > runtime.direct_wide_word_count
            || words > runtime.direct_wide_word_count - offset) {
            throw LlvmJitError(
                "JIT runtime ABI wide direct-read span is too small");
        }
        if (kind == fsim::runtime::simir::ValueKind::logic9
            && (runtime.direct_wide_signal_logic9_plane2 == nullptr
                || runtime.direct_wide_signal_logic9_plane3 == nullptr)) {
            throw LlvmJitError(
                "JIT runtime ABI wide Logic9 direct-read planes are null");
        }
    }
}

struct NativeAddressRange {
    std::uintptr_t begin { };
    std::uintptr_t end { };
};

[[nodiscard]] NativeAddressRange checked_native_range(
    const void* const pointer,
    const std::size_t count,
    const std::size_t element_size)
{
    if (count == 0U) {
        return { };
    }
    if (pointer == nullptr
        || element_size == 0U
        || count > std::numeric_limits<std::size_t>::max() / element_size) {
        throw LlvmJitError(
            "tiered direct-read storage range is not representable");
    }
    const auto begin = reinterpret_cast<std::uintptr_t>(pointer);
    const auto bytes = count * element_size;
    if (bytes > std::numeric_limits<std::uintptr_t>::max() - begin) {
        throw LlvmJitError(
            "tiered direct-read storage range overflows address space");
    }
    return { begin, begin + bytes };
}

[[nodiscard]] bool native_ranges_overlap(
    const NativeAddressRange left,
    const NativeAddressRange right) noexcept
{
    return left.begin != left.end && right.begin != right.end
        && left.begin < right.end && right.begin < left.end;
}

template <typename ProcessInfo>
void validate_tiered_direct_read_register_disjointness(
    const ProcessInfo& info,
    const fsim_jit_runtime_instance_v2& runtime,
    const fsim_jit_frame_v2& frame)
{
    if (!info.tiered_read_dedup_safe) {
        return;
    }
    const auto& layout = info.frame_layout;
    const auto reject_overlap = [&](
        const void* const read_pointer,
        const std::size_t read_count,
        const std::size_t read_size,
        const void* const write_pointer,
        const std::size_t write_count,
        const std::size_t write_size) {
        if (native_ranges_overlap(
                checked_native_range(
                    read_pointer, read_count, read_size),
                checked_native_range(
                    write_pointer, write_count, write_size))) {
            throw LlvmJitError(
                "tiered direct-read inputs overlap writable JIT frame registers");
        }
    };

    const auto register_words = layout.register_word_count;
    const auto register_count = layout.register_count;
    const auto input_count = runtime.direct_signal_count;
    reject_overlap(runtime.direct_read_signals,
        runtime.direct_read_signal_count, sizeof(std::uint32_t),
        frame.register_aval, register_words, sizeof(std::uint64_t));
    reject_overlap(runtime.direct_read_signals,
        runtime.direct_read_signal_count, sizeof(std::uint32_t),
        frame.register_bval, register_words, sizeof(std::uint64_t));
    reject_overlap(runtime.direct_signal_aval, input_count,
        sizeof(std::uint64_t), frame.register_aval,
        register_words, sizeof(std::uint64_t));
    reject_overlap(runtime.direct_signal_aval, input_count,
        sizeof(std::uint64_t), frame.register_bval,
        register_words, sizeof(std::uint64_t));
    reject_overlap(runtime.direct_signal_bval, input_count,
        sizeof(std::uint64_t), frame.register_aval,
        register_words, sizeof(std::uint64_t));
    reject_overlap(runtime.direct_signal_bval, input_count,
        sizeof(std::uint64_t), frame.register_bval,
        register_words, sizeof(std::uint64_t));
    if (layout.tracks_register_initialization) {
        reject_overlap(runtime.direct_read_signals,
            runtime.direct_read_signal_count, sizeof(std::uint32_t),
            frame.register_initialized, register_count, sizeof(std::uint8_t));
        reject_overlap(runtime.direct_signal_aval, input_count,
            sizeof(std::uint64_t), frame.register_initialized,
            register_count, sizeof(std::uint8_t));
        reject_overlap(runtime.direct_signal_bval, input_count,
            sizeof(std::uint64_t), frame.register_initialized,
            register_count, sizeof(std::uint8_t));
    }

    if (layout.uses_logic9) {
        reject_overlap(runtime.direct_read_signals,
            runtime.direct_read_signal_count, sizeof(std::uint32_t),
            frame.register_logic9_plane2, register_words,
            sizeof(std::uint64_t));
        reject_overlap(runtime.direct_read_signals,
            runtime.direct_read_signal_count, sizeof(std::uint32_t),
            frame.register_logic9_plane3, register_words,
            sizeof(std::uint64_t));
        reject_overlap(runtime.direct_signal_aval, input_count,
            sizeof(std::uint64_t), frame.register_logic9_plane2,
            register_words, sizeof(std::uint64_t));
        reject_overlap(runtime.direct_signal_aval, input_count,
            sizeof(std::uint64_t), frame.register_logic9_plane3,
            register_words, sizeof(std::uint64_t));
        reject_overlap(runtime.direct_signal_bval, input_count,
            sizeof(std::uint64_t), frame.register_logic9_plane2,
            register_words, sizeof(std::uint64_t));
        reject_overlap(runtime.direct_signal_bval, input_count,
            sizeof(std::uint64_t), frame.register_logic9_plane3,
            register_words, sizeof(std::uint64_t));
    }
}

template <typename ProcessInfo>
void validate_native_activation_v2(
    const ProcessInfo& info,
    const fsim_jit_runtime_instance_v2& runtime,
    const fsim_jit_frame_v2& frame,
    const fsim_jit_resume_result_v2& result,
    const bool require_direct_update_slots,
    const bool require_direct_read_signals,
    const bool direct_backing_prevalidated = false,
    const std::optional<std::span<const JitDirectReadInstanceBinding>> bindings
        = std::nullopt)
{
    validate_native_service_callbacks(info, runtime);
    if (info.requires_direct_read_signals != require_direct_read_signals) {
        throw LlvmJitError(
            "JIT direct-read requirement does not match its native entry");
    }
    if (direct_backing_prevalidated && !require_direct_read_signals) {
        throw LlvmJitError(
            "JIT required-direct-read lease targets a guarded entry");
    }
    if (require_direct_read_signals && !direct_backing_prevalidated) {
        validate_required_direct_read_backing(info, runtime, bindings);
    }
    if (frame.abi_version != FSIM_JIT_FRAME_ABI_VERSION_V2) {
        throw LlvmJitError("JIT frame ABI version mismatch");
    }
    if (frame.struct_size < sizeof(fsim_jit_frame_v2)) {
        throw LlvmJitError("JIT frame ABI structure is too small");
    }
    const auto& layout = info.frame_layout;
    if (frame.layout_id_low != layout.layout_id_low
        || frame.layout_id_high != layout.layout_id_high
        || frame.register_count != layout.register_count) {
        throw LlvmJitError("JIT frame layout mismatch");
    }
    if ((layout.register_word_count != 0U
            && (frame.register_aval == nullptr
                || frame.register_bval == nullptr))
        || (frame.register_count != 0U
            && frame.register_initialized == nullptr)) {
        throw LlvmJitError("JIT frame register storage is null");
    }
    if (layout.register_word_count != 0U
        && frame.register_aval == frame.register_bval) {
        throw LlvmJitError(
            "JIT frame aval and bval register storage must be distinct");
    }
    if (layout.uses_logic9 && layout.register_word_count != 0U
        && (frame.register_logic9_plane2 == nullptr
            || frame.register_logic9_plane3 == nullptr)) {
        throw LlvmJitError("JIT frame Logic9 register storage is null");
    }
    if (frame.native_call_reserved != 0U
        || frame.native_call_depth
            > FSIM_JIT_NATIVE_CALL_STACK_CAPACITY_V2) {
        throw LlvmJitError("JIT frame native call stack state is invalid");
    }
    if (result.abi_version != FSIM_JIT_RESUME_RESULT_ABI_VERSION_V2) {
        throw LlvmJitError("JIT resume-result ABI version mismatch");
    }
    if (result.struct_size < sizeof(fsim_jit_resume_result_v2)) {
        throw LlvmJitError("JIT resume-result ABI structure is too small");
    }
    if (frame.state == FSIM_JIT_FRAME_STATE_READY_V2
        && frame.program_counter >= info.operation_count) {
        throw LlvmJitError(
            "JIT frame program counter is outside the operation stream");
    }
    if (frame.state > FSIM_JIT_FRAME_STATE_RUNTIME_ERROR_V2) {
        throw LlvmJitError("JIT frame state is invalid");
    }
    const auto direct_update_slot_count
        = layout.direct_update_signals.size();
    const auto direct_update_active_word_count
        = direct_update_slot_count / 64U
            + (direct_update_slot_count % 64U != 0U ? 1U : 0U);
    if (info.direct_update_widths.size() != direct_update_slot_count) {
        throw LlvmJitError(
            "JIT process direct-update width metadata is inconsistent");
    }
    if (require_direct_update_slots && direct_update_slot_count != 0U
        && (runtime.direct_update_slots == nullptr
            || runtime.direct_update_slot_count < direct_update_slot_count
            || runtime.direct_update_active_words == nullptr
            || runtime.direct_update_active_word_count
                < direct_update_active_word_count)) {
        throw LlvmJitError(
            "JIT runtime ABI requires every direct-update slot promised at "
            "lowering time");
    }
    if (runtime.direct_update_slots != nullptr) {
        if (runtime.direct_update_slot_count < direct_update_slot_count) {
            throw LlvmJitError(
                "JIT runtime ABI direct-update slot array is too small");
        }
        if (runtime.direct_update_active_words != nullptr
            && runtime.direct_update_active_word_count
                < direct_update_active_word_count) {
            throw LlvmJitError(
                "JIT runtime ABI direct-update activity bitmap is too small");
        }
        for (std::size_t slot_index = 0U;
             slot_index < direct_update_slot_count; ++slot_index) {
            const auto width = info.direct_update_widths[slot_index];
            if (width <= 64U) {
                continue;
            }
            const auto& slot = runtime.direct_update_slots[slot_index];
            const auto word_count = static_cast<std::uint32_t>(
                (static_cast<std::uint64_t>(width) + 63U) / 64U);
            if (slot.width != width || slot.word_count < word_count
                || slot.wide_aval == nullptr || slot.wide_bval == nullptr
                || slot.wide_mask == nullptr) {
                throw LlvmJitError(
                    "JIT runtime ABI has a malformed wide direct-update slot");
            }
        }
    }
    validate_tiered_direct_read_register_disjointness(info, runtime, frame);
}

void validate_region_activation_backing(
    const fsim_jit_runtime_instance_v2& runtime,
    const fsim_jit_frame_v2& frame,
    const JitProcessFrameLayout& layout,
    const llvm_detail::RegionKernelActivationBacking& backing)
{
    const auto reject = [] {
        throw LlvmJitError(
            "region activation certificate storage does not match its ABI");
    };
    const auto fits_u32 = [](const std::size_t value) {
        return value <= std::numeric_limits<std::uint32_t>::max();
    };
    if (runtime.flags != 0U
        || runtime.static_trigger_mask != 0U
        || runtime.services != backing.services
        || runtime.context != backing.callback_context
        || backing.services == nullptr || backing.callback_context == nullptr
        || backing.signal_widths.size() != backing.signal_value_kinds.size()
        || !fits_u32(backing.signal_widths.size())
        || runtime.direct_signal_count != backing.signal_widths.size()
        || backing.direct_signal_aval.size() != backing.signal_widths.size()
        || backing.direct_signal_bval.size() != backing.signal_widths.size()
        || runtime.direct_signal_aval != backing.direct_signal_aval.data()
        || runtime.direct_signal_bval != backing.direct_signal_bval.data()
        || runtime.direct_read_signals != backing.direct_read_signals.data()
        || runtime.direct_read_signal_count
            != backing.direct_read_signals.size()
        || !fits_u32(backing.direct_read_signals.size())
        || backing.direct_read_signals.size()
            != layout.direct_read_signals.size()
        || !std::equal(backing.direct_read_signals.begin(),
            backing.direct_read_signals.end(),
            layout.direct_read_signals.begin())
        || backing.direct_update_signals.size()
            != layout.direct_update_signals.size()
        || !fits_u32(backing.direct_update_signals.size())
        || !std::equal(backing.direct_update_signals.begin(),
            backing.direct_update_signals.end(),
            layout.direct_update_signals.begin())
        || runtime.code_coverage_hit_counters != nullptr
        || runtime.code_coverage_counter_values != nullptr
        || runtime.code_coverage_hit_count != 0U
        || runtime.code_coverage_counter_count != 0U
        || runtime.fused_activation_words != nullptr
        || runtime.fused_activation_word_count != 0U
        || !llvm_detail::activation_wide_backing_matches(runtime,
            backing.signal_widths, backing.signal_value_kinds,
            backing.direct_update_signals, backing)) {
        reject();
    }

    if (layout.register_widths.size() != layout.register_count
        || layout.register_word_offsets.size() != layout.register_count
        || backing.register_widths.size() != layout.register_count
        || backing.register_word_offsets.size() != layout.register_count
        || !std::equal(backing.register_widths.begin(),
            backing.register_widths.end(), layout.register_widths.begin())
        || !std::equal(backing.register_word_offsets.begin(),
            backing.register_word_offsets.end(),
            layout.register_word_offsets.begin())
        || backing.register_aval.size() != layout.register_word_count
        || backing.register_bval.size() != layout.register_word_count
        || backing.register_initialized.size() != layout.register_count
        || backing.register_value_kinds.size() != layout.register_count
        || backing.register_values_persistent.size() != layout.register_count
        || backing.register_export_mask.size() != layout.register_count
        || layout.register_values_persistent.size() != layout.register_count
        || !std::equal(backing.register_values_persistent.begin(),
            backing.register_values_persistent.end(),
            layout.register_values_persistent.begin())
        || frame.register_aval != backing.register_aval.data()
        || frame.register_bval != backing.register_bval.data()
        || frame.register_initialized != backing.register_initialized.data()
        || backing.register_logic9_plane2.size()
            != (layout.uses_logic9 ? layout.register_word_count : 0U)
        || backing.register_logic9_plane3.size()
            != (layout.uses_logic9 ? layout.register_word_count : 0U)
        || frame.register_logic9_plane2
            != backing.register_logic9_plane2.data()
        || frame.register_logic9_plane3
            != backing.register_logic9_plane3.data()
        || (backing.direct_signal_aval.size() != 0U
            && (runtime.direct_signal_aval == nullptr
                || runtime.direct_signal_bval == nullptr
                || runtime.direct_signal_aval == runtime.direct_signal_bval))
        || (layout.register_word_count != 0U
            && frame.register_aval == frame.register_bval)
        || backing.direct_update_slots.size()
            != layout.direct_update_signals.size()
        || !fits_u32(backing.direct_update_slots.size())) {
        reject();
    }

    const auto required_active_words
        = backing.direct_update_slots.size() / 64U
            + (backing.direct_update_slots.size() % 64U != 0U ? 1U : 0U);
    if (backing.direct_update_active_words.size() != required_active_words
        || !fits_u32(required_active_words)
        || runtime.direct_update_slots != backing.direct_update_slots.data()
        || runtime.direct_update_slot_count
            != backing.direct_update_slots.size()
        || runtime.direct_update_active_words
            != backing.direct_update_active_words.data()
        || runtime.direct_update_active_word_count != required_active_words) {
        reject();
    }

    bool has_logic9 { };
    bool has_export { };
    for (std::size_t reg = 0U;
         reg < backing.register_widths.size(); ++reg) {
        const auto kind = backing.register_value_kinds[reg];
        const auto export_value = backing.register_export_mask[reg];
        if ((kind != fsim::runtime::simir::ValueKind::logic4
                    && kind != fsim::runtime::simir::ValueKind::logic9)
                || export_value > 1U
                || backing.register_widths[reg] != layout.register_widths[reg]
                || (export_value != 0U
                    && (backing.register_widths[reg] == 0U
                        || layout.register_values_persistent[reg] == 0U
                        || !layout.tracks_register_initialization))) {
            reject();
        }
        has_logic9 = has_logic9
            || kind == fsim::runtime::simir::ValueKind::logic9;
        has_export = has_export || export_value != 0U;
    }
    if (layout.uses_logic9 != has_logic9 || has_export != layout.uses_logic9) {
        reject();
    }
    for (std::size_t signal = 0U;
         signal < backing.signal_widths.size(); ++signal) {
        if (backing.signal_widths[signal] == 0U
            || (backing.signal_value_kinds[signal]
                    != fsim::runtime::simir::ValueKind::logic4
                && backing.signal_value_kinds[signal]
                    != fsim::runtime::simir::ValueKind::logic9)) {
            reject();
        }
    }
    for (const auto signal : backing.direct_read_signals) {
        if (signal >= backing.signal_widths.size()) {
            reject();
        }
    }
    for (std::size_t slot_index = 0U;
         slot_index < backing.direct_update_slots.size(); ++slot_index) {
        const auto signal = backing.direct_update_signals[slot_index];
        if (signal >= backing.signal_widths.size()) {
            reject();
        }
    }

    if (!llvm_detail::activation_register_word_layout_matches(
            layout.register_widths, layout.register_word_offsets,
            layout.register_word_count)) {
        reject();
    }
}

} // namespace

void JitRequiredDirectReadLease::capture_backing(
    const fsim_jit_runtime_instance_v2& runtime,
    const fsim_jit_frame_v2& frame,
    const fsim_jit_resume_result_v2& result) noexcept
{
    pointers_ = {
        &runtime,
        &frame,
        &result,
        runtime.services,
        runtime.context,
        runtime.direct_update_slots,
        runtime.direct_signal_aval,
        runtime.direct_signal_bval,
        runtime.direct_read_signals,
        runtime.direct_wide_signal_aval,
        runtime.direct_wide_signal_bval,
        runtime.direct_wide_signal_offsets,
        runtime.direct_update_active_words,
        runtime.direct_wide_signal_logic9_plane2,
        runtime.direct_wide_signal_logic9_plane3,
        runtime.direct_signal_logic9_plane0,
        runtime.direct_signal_logic9_plane1,
        runtime.direct_signal_logic9_plane2,
        runtime.direct_signal_logic9_plane3,
        runtime.code_coverage_hit_counters,
        runtime.code_coverage_counter_values,
        runtime.fused_activation_words,
        frame.register_aval,
        frame.register_bval,
        frame.register_initialized,
        frame.register_logic9_plane2,
        frame.register_logic9_plane3,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr
    };
    shape_ = {
        runtime.abi_version,
        runtime.struct_size,
        runtime.flags,
        runtime.reserved,
        runtime.direct_update_slot_count,
        runtime.direct_update_reserved,
        runtime.direct_read_signal_count,
        runtime.direct_signal_count,
        runtime.direct_signal_reserved,
        runtime.direct_wide_signal_offset_count,
        runtime.direct_wide_word_count,
        runtime.direct_update_active_word_count,
        runtime.direct_update_active_reserved,
        runtime.code_coverage_hit_count,
        runtime.code_coverage_counter_count,
        runtime.fused_activation_word_count,
        runtime.fused_activation_reserved,
        0U,
        0U,
        frame.abi_version,
        frame.struct_size,
        frame.layout_id_low,
        frame.layout_id_high,
        frame.register_count,
        frame.native_call_depth,
        frame.native_call_reserved,
        result.abi_version,
        result.struct_size,
        0U,
        0U,
        0U,
        0U
    };
}

bool JitRequiredDirectReadLease::matches_backing(
    const void* const owner,
    const void* const entry,
    const JitProcessBinding process,
    const fsim_jit_runtime_instance_v2& runtime,
    const fsim_jit_frame_v2& frame,
    const fsim_jit_resume_result_v2& result) const noexcept
{
    if (owner_ != owner || entry_ != entry || process_ != process) {
        return false;
    }
    JitRequiredDirectReadLease current;
    current.capture_backing(runtime, frame, result);
    return pointers_ == current.pointers_ && shape_ == current.shape_;
}

llvm_detail::RegionKernelActivationCertificate
LlvmJit::bind_region_activation(
    const JitProcessBinding process,
    const fsim_jit_runtime_instance_v2& runtime,
    fsim_jit_frame_v2& frame,
    fsim_jit_resume_result_v2& result,
    const JitProcessFrameLayout& layout,
    const llvm_detail::RegionKernelActivationBacking& backing)
{
    if (!impl_ || process.owner_ != impl_.get() || process.entry_ == nullptr) {
        throw LlvmJitError("invalid region activation process binding");
    }
    const auto* const entry
        = static_cast<const LlvmJit::Impl::NativeEntry*>(process.entry_);
    if (entry->function == nullptr
        || !(entry->info.frame_layout == layout)) {
        throw LlvmJitError("invalid region activation native entry");
    }
    validate_native_activation_v2(entry->info, runtime, frame, result,
        impl_->options.require_direct_update_slots,
        entry->info.requires_direct_read_signals);
    validate_region_activation_backing(runtime, frame, layout, backing);
    if (frame.state != FSIM_JIT_FRAME_STATE_READY_V2
        || frame.program_counter != 0U || frame.native_call_depth != 0U
        || result.abi_version != FSIM_JIT_RESUME_RESULT_ABI_VERSION_V2
        || result.struct_size < sizeof(fsim_jit_resume_result_v2)) {
        throw LlvmJitError(
            "region activation certificate requires an initialized frame");
    }

    llvm_detail::RegionKernelActivationCertificate certificate;
    certificate.owner_ = impl_.get();
    certificate.entry_ = entry;
    certificate.function_ = entry->function;
    certificate.process_ = process;
    certificate.runtime_address_ = &runtime;
    certificate.frame_address_ = &frame;
    certificate.result_address_ = &result;
    return certificate;
}

bool LlvmJit::matches_region_activation(
    const llvm_detail::RegionKernelActivationCertificate& certificate,
    const JitProcessBinding process,
    const fsim_jit_runtime_instance_v2& runtime,
    const fsim_jit_frame_v2& frame,
    const fsim_jit_resume_result_v2& result) const noexcept
{
    return certificate
        && impl_ != nullptr
        && certificate.owner_ == impl_.get()
        && certificate.entry_ == process.entry_
        && certificate.process_ == process
        && process.owner_ == impl_.get()
        && certificate.runtime_address_ == &runtime
        && certificate.frame_address_ == &frame
        && certificate.result_address_ == &result;
}

bool LlvmJit::resume_region_activation(
    const llvm_detail::RegionKernelActivationCertificate& certificate,
    const JitProcessBinding process,
    const fsim_jit_runtime_instance_v2& runtime,
    fsim_jit_frame_v2& frame,
    fsim_jit_resume_result_v2& result,
    std::uint32_t& raw_status) const noexcept
{
    std::exception_ptr ignored_failure;
    return resume_region_activation(certificate, process, runtime, frame,
        result, raw_status, ignored_failure);
}

bool LlvmJit::resume_region_activation(
    const llvm_detail::RegionKernelActivationCertificate& certificate,
    const JitProcessBinding process,
    const fsim_jit_runtime_instance_v2& runtime,
    fsim_jit_frame_v2& frame,
    fsim_jit_resume_result_v2& result,
    std::uint32_t& raw_status,
    std::exception_ptr& failure) const noexcept
{
    failure = { };
    raw_status = FSIM_JIT_RESUME_STATUS_RUNTIME_ERROR_V2;
    if (!matches_region_activation(
            certificate, process, runtime, frame, result)) {
        return false;
    }
    try {
        raw_status = certificate.function_(&runtime, &frame, &result);
    } catch (...) {
        failure = std::current_exception();
        return false;
    }
    return true;
}

llvm_detail::RegionPreparedOutputCertificate
LlvmJit::bind_region_prepared_output(
    const JitProcessBinding process,
    const fsim_jit_runtime_instance_v2& runtime,
    fsim_jit_frame_v2& frame,
    fsim_jit_resume_result_v2& result,
    const JitProcessFrameLayout& layout,
    const llvm_detail::RegionKernelActivationBacking& backing)
{
    if (!impl_ || process.owner_ != impl_.get() || process.entry_ == nullptr) {
        throw LlvmJitError("invalid prepared-output process binding");
    }
    const auto* const entry
        = static_cast<const LlvmJit::Impl::NativeEntry*>(process.entry_);
    if (entry->function == nullptr || !(entry->info.frame_layout == layout)) {
        throw LlvmJitError("invalid prepared-output native entry");
    }
    validate_native_activation_v2(entry->info, runtime, frame, result,
        impl_->options.require_direct_update_slots,
        entry->info.requires_direct_read_signals);
    validate_region_activation_backing(runtime, frame, layout, backing);
    if (frame.state != FSIM_JIT_FRAME_STATE_READY_V2
        || frame.program_counter != 0U || frame.native_call_depth != 0U
        || result.abi_version != FSIM_JIT_RESUME_RESULT_ABI_VERSION_V2
        || result.struct_size < sizeof(fsim_jit_resume_result_v2)) {
        throw LlvmJitError(
            "prepared-output certificate requires an initialized frame");
    }

    const std::string private_symbol
        = entry->symbol + "_prepared_output_prefix_v1";
    const auto address = unwrap(impl_->jit->lookup(private_symbol),
        "cannot materialize prepared-output entry '" + private_symbol + "'");
    auto* const function
        = address.template toPtr<llvm_detail::NativeRegionPreparedOutputProcess>();
    if (function == nullptr) {
        throw LlvmJitError("LLVM returned a null prepared-output entry");
    }
    const std::string successor_symbol
        = entry->symbol + "_prepared_output_successor_v1";
    const auto successor_address
        = unwrap(impl_->jit->lookup(successor_symbol),
            "cannot materialize prepared-output successor entry '"
                + successor_symbol + "'");
    auto* const successor_function = successor_address.template toPtr<
        llvm_detail::NativeRegionPreparedOutputSuccessorProcess>();
    if (successor_function == nullptr) {
        throw LlvmJitError("LLVM returned a null successor-mask entry");
    }

    llvm_detail::RegionPreparedOutputCertificate certificate;
    certificate.owner_ = impl_.get();
    certificate.entry_ = entry;
    certificate.function_ = function;
    certificate.successor_function_ = successor_function;
    certificate.process_ = process;
    certificate.runtime_address_ = &runtime;
    certificate.frame_address_ = &frame;
    certificate.result_address_ = &result;
    return certificate;
}

bool LlvmJit::matches_region_prepared_output(
    const llvm_detail::RegionPreparedOutputCertificate& certificate,
    const JitProcessBinding process,
    const fsim_jit_runtime_instance_v2& runtime,
    const fsim_jit_frame_v2& frame,
    const fsim_jit_resume_result_v2& result) const noexcept
{
    return certificate && impl_ != nullptr
        && certificate.owner_ == impl_.get()
        && certificate.entry_ == process.entry_
        && certificate.process_ == process
        && process.owner_ == impl_.get()
        && certificate.runtime_address_ == &runtime
        && certificate.frame_address_ == &frame
        && certificate.result_address_ == &result;
}

bool LlvmJit::resume_region_prepared_output(
    const llvm_detail::RegionPreparedOutputCertificate& certificate,
    const JitProcessBinding process,
    const fsim_jit_runtime_instance_v2& runtime,
    fsim_jit_frame_v2& frame,
    fsim_jit_resume_result_v2& result,
    const fsim::runtime::simir::RegionPreparedOutputBatchV1& outputs,
    std::uint32_t& raw_status,
    runtime::simir::RegionPreparedOutputSuccessorMasksV1* const successors)
    const noexcept
{
    std::exception_ptr ignored_failure;
    return resume_region_prepared_output(certificate, process, runtime,
        frame, result, outputs, raw_status, successors, ignored_failure);
}

bool LlvmJit::resume_region_prepared_output(
    const llvm_detail::RegionPreparedOutputCertificate& certificate,
    const JitProcessBinding process,
    const fsim_jit_runtime_instance_v2& runtime,
    fsim_jit_frame_v2& frame,
    fsim_jit_resume_result_v2& result,
    const fsim::runtime::simir::RegionPreparedOutputBatchV1& outputs,
    std::uint32_t& raw_status,
    runtime::simir::RegionPreparedOutputSuccessorMasksV1* const successors,
    std::exception_ptr& failure)
    const noexcept
{
    failure = { };
    raw_status = FSIM_JIT_RESUME_STATUS_RUNTIME_ERROR_V2;
    if (!matches_region_prepared_output(certificate, process, runtime,
            frame, result)) {
        return false;
    }
    if (successors != nullptr) {
        if (reinterpret_cast<std::uintptr_t>(successors)
            % alignof(runtime::simir::
                RegionPreparedOutputSuccessorMasksV1) != 0U) {
            return false;
        }
        const auto maximum = std::numeric_limits<std::uintptr_t>::max();
        const auto masks_begin = reinterpret_cast<std::uintptr_t>(
            successors->member_masks);
        const auto slots_begin = reinterpret_cast<std::uintptr_t>(
            outputs.slots);
        const std::size_t successor_count = successors->slot_count;
        const std::size_t output_count = outputs.slot_count;
        if (certificate.successor_function_ == nullptr
            || successors->abi_version
                != runtime::simir::
                    kRegionPreparedOutputSuccessorMasksAbiVersionV1
            || successors->struct_size != sizeof(*successors)
            || successors->slot_count != outputs.slot_count
            || successors->reserved != 0U
            || successors->member_masks == nullptr
            || reinterpret_cast<std::uintptr_t>(successors)
                % alignof(runtime::simir::
                    RegionPreparedOutputSuccessorMasksV1) != 0U
            || outputs.abi_version
                != runtime::simir::kRegionPreparedOutputBatchAbiVersionV1
            || outputs.struct_size != sizeof(outputs)
            || outputs.reserved != 0U
            || masks_begin % alignof(std::uint64_t) != 0U
            || successor_count
                > std::numeric_limits<std::size_t>::max()
                    / sizeof(std::uint64_t)
            || outputs.slots == nullptr
            || slots_begin
                % alignof(runtime::simir::RegionPreparedOutputSlotV1) != 0U
            || output_count
                > std::numeric_limits<std::size_t>::max()
                    / sizeof(runtime::simir::RegionPreparedOutputSlotV1)) {
            return false;
        }
        const auto masks_bytes = successor_count * sizeof(std::uint64_t);
        if (masks_begin > maximum - masks_bytes) {
            return false;
        }
        const auto masks_end = masks_begin + masks_bytes;
        const auto slots_bytes = output_count
            * sizeof(runtime::simir::RegionPreparedOutputSlotV1);
        if (slots_begin > maximum - slots_bytes
            || (masks_begin < slots_begin + slots_bytes
                && slots_begin < masks_end)) {
            return false;
        }
        const auto batch_begin = reinterpret_cast<std::uintptr_t>(&outputs);
        if (batch_begin > maximum - sizeof(outputs)
            || (masks_begin < batch_begin + sizeof(outputs)
                && batch_begin < masks_end)) {
            return false;
        }
        const auto sidecar_begin
            = reinterpret_cast<std::uintptr_t>(successors);
        if (sidecar_begin > maximum - sizeof(*successors)
            || (masks_begin < sidecar_begin + sizeof(*successors)
                && sidecar_begin < masks_end)) {
            return false;
        }
        if ((sidecar_begin < batch_begin + sizeof(outputs)
                && batch_begin < sidecar_begin + sizeof(*successors))
            || (slots_begin < batch_begin + sizeof(outputs)
                && batch_begin < slots_begin + slots_bytes)
            || (slots_begin < sidecar_begin + sizeof(*successors)
                && sidecar_begin < slots_begin + slots_bytes)) {
            return false;
        }
    }
    try {
        raw_status = successors == nullptr
            ? certificate.function_(&runtime, &frame, &result, &outputs)
            : certificate.successor_function_(&runtime, &frame, &result,
                  &outputs, successors);
    } catch (...) {
        failure = std::current_exception();
        return false;
    }
    return true;
}

llvm_detail::RegionDirectReadyOutputCertificate
LlvmJit::bind_region_direct_ready_output(
    const JitProcessBinding process,
    const fsim_jit_runtime_instance_v2& runtime,
    fsim_jit_frame_v2& frame,
    fsim_jit_resume_result_v2& result,
    const JitProcessFrameLayout& layout,
    const llvm_detail::RegionKernelActivationBacking& backing)
{
    if (!impl_ || process.owner_ != impl_.get() || process.entry_ == nullptr) {
        throw LlvmJitError("invalid direct-ready process binding");
    }
    const auto* const entry
        = static_cast<const LlvmJit::Impl::NativeEntry*>(process.entry_);
    if (entry->function == nullptr || !(entry->info.frame_layout == layout)) {
        throw LlvmJitError("invalid direct-ready native entry");
    }
    validate_native_activation_v2(entry->info, runtime, frame, result,
        impl_->options.require_direct_update_slots,
        entry->info.requires_direct_read_signals);
    validate_region_activation_backing(runtime, frame, layout, backing);
    if (frame.state != FSIM_JIT_FRAME_STATE_READY_V2
        || frame.program_counter != 0U || frame.native_call_depth != 0U
        || result.abi_version != FSIM_JIT_RESUME_RESULT_ABI_VERSION_V2
        || result.struct_size < sizeof(fsim_jit_resume_result_v2)) {
        throw LlvmJitError(
            "direct-ready certificate requires an initialized frame");
    }

    const std::string private_symbol
        = entry->symbol + "_direct_ready_window_prepared_v1";
    const auto address = unwrap(impl_->jit->lookup(private_symbol),
        "cannot materialize direct-ready entry '" + private_symbol + "'");
    auto* const function = address.template toPtr<
        llvm_detail::NativeRegionDirectReadyOutputProcess>();
    if (function == nullptr) {
        throw LlvmJitError("LLVM returned a null direct-ready entry");
    }

    llvm_detail::NativeRegionDirectReadySuccessorOutputProcess*
        successor_function { };
    const std::string successor_symbol
        = entry->symbol + "_direct_ready_window_successor_masks_v1";
    auto successor_address = impl_->jit->lookup(successor_symbol);
    if (successor_address) {
        successor_function = successor_address->template toPtr<
            llvm_detail::NativeRegionDirectReadySuccessorOutputProcess>();
        if (successor_function == nullptr) {
            throw LlvmJitError(
                "LLVM returned a null direct-ready successor entry");
        }
    } else {
        // This optional symbol is absent for kernels whose immutable mapping
        // cannot represent a successor mask, and may be absent in legacy
        // cached modules. The direct-ready V1 entry remains usable.
        llvm::consumeError(successor_address.takeError());
    }

    llvm_detail::RegionDirectReadyOutputCertificate certificate;
    certificate.owner_ = impl_.get();
    certificate.entry_ = entry;
    certificate.function_ = function;
    certificate.successor_function_ = successor_function;
    certificate.process_ = process;
    certificate.runtime_address_ = &runtime;
    certificate.frame_address_ = &frame;
    certificate.result_address_ = &result;
    return certificate;
}

bool LlvmJit::matches_region_direct_ready_output(
    const llvm_detail::RegionDirectReadyOutputCertificate& certificate,
    const JitProcessBinding process,
    const fsim_jit_runtime_instance_v2& runtime,
    const fsim_jit_frame_v2& frame,
    const fsim_jit_resume_result_v2& result) const noexcept
{
    return certificate && impl_ != nullptr
        && certificate.owner_ == impl_.get()
        && certificate.entry_ == process.entry_
        && certificate.process_ == process
        && process.owner_ == impl_.get()
        && certificate.runtime_address_ == &runtime
        && certificate.frame_address_ == &frame
        && certificate.result_address_ == &result;
}

bool LlvmJit::resume_region_direct_ready_output(
    const llvm_detail::RegionDirectReadyOutputCertificate& certificate,
    const JitProcessBinding process,
    const fsim_jit_runtime_instance_v2& runtime,
    fsim_jit_frame_v2& frame,
    fsim_jit_resume_result_v2& result,
    const runtime::simir::RegionDirectReadyWindowV1& input_window,
    const runtime::simir::RegionPreparedOutputBatchV1& outputs,
    runtime::simir::RegionPreparedOutputSuccessorMasksV1* const successors,
    std::uint32_t& raw_status) const noexcept
{
    std::exception_ptr ignored_failure;
    return resume_region_direct_ready_output(certificate, process, runtime,
        frame, result, input_window, outputs, successors, raw_status,
        ignored_failure);
}

bool LlvmJit::resume_region_direct_ready_output(
    const llvm_detail::RegionDirectReadyOutputCertificate& certificate,
    const JitProcessBinding process,
    const fsim_jit_runtime_instance_v2& runtime,
    fsim_jit_frame_v2& frame,
    fsim_jit_resume_result_v2& result,
    const runtime::simir::RegionDirectReadyWindowV1& input_window,
    const runtime::simir::RegionPreparedOutputBatchV1& outputs,
    runtime::simir::RegionPreparedOutputSuccessorMasksV1* const successors,
    std::uint32_t& raw_status,
    std::exception_ptr& failure) const noexcept
{
    failure = { };
    raw_status = FSIM_JIT_RESUME_STATUS_RUNTIME_ERROR_V2;
    if (!matches_region_direct_ready_output(certificate, process, runtime,
            frame, result)) {
        return false;
    }
    if (successors != nullptr
        && (certificate.successor_function_ == nullptr
            || successors->abi_version
                != runtime::simir::
                    kRegionPreparedOutputSuccessorMasksAbiVersionV1
            || successors->struct_size != sizeof(*successors)
            || successors->slot_count != outputs.slot_count
            || successors->reserved != 0U
            || successors->member_masks == nullptr)) {
        return false;
    }
    if (successors != nullptr) {
        const std::size_t successor_count = successors->slot_count;
        const std::size_t output_count = outputs.slot_count;
        const auto maximum = std::numeric_limits<std::uintptr_t>::max();
        const auto masks_begin = reinterpret_cast<std::uintptr_t>(
            successors->member_masks);
        const auto slots_begin = reinterpret_cast<std::uintptr_t>(
            outputs.slots);
        const auto sidecar_begin = reinterpret_cast<std::uintptr_t>(
            successors);
        const auto batch_begin = reinterpret_cast<std::uintptr_t>(&outputs);
        if (outputs.abi_version
                != runtime::simir::kRegionPreparedOutputBatchAbiVersionV1
            || outputs.struct_size != sizeof(outputs)
            || outputs.reserved != 0U
            || outputs.slots == nullptr
            || slots_begin
                % alignof(runtime::simir::RegionPreparedOutputSlotV1) != 0U
            || masks_begin % alignof(std::uint64_t) != 0U
            || successor_count
                > std::numeric_limits<std::size_t>::max()
                    / sizeof(std::uint64_t)
            || output_count
                > std::numeric_limits<std::size_t>::max()
                    / sizeof(runtime::simir::RegionPreparedOutputSlotV1)) {
            return false;
        }
        const auto masks_bytes = successor_count * sizeof(std::uint64_t);
        const auto slots_bytes = output_count
            * sizeof(runtime::simir::RegionPreparedOutputSlotV1);
        if (masks_begin > maximum - masks_bytes
            || slots_begin > maximum - slots_bytes
            || sidecar_begin > maximum - sizeof(*successors)
            || batch_begin > maximum - sizeof(outputs)) {
            return false;
        }
        const auto masks_end = masks_begin + masks_bytes;
        const auto slots_end = slots_begin + slots_bytes;
        const auto sidecar_end = sidecar_begin + sizeof(*successors);
        const auto batch_end = batch_begin + sizeof(outputs);
        const auto overlaps = [](const std::uintptr_t left_begin,
                                 const std::uintptr_t left_end,
                                 const std::uintptr_t right_begin,
                                 const std::uintptr_t right_end) noexcept {
            return left_begin < right_end && right_begin < left_end;
        };
        if (overlaps(masks_begin, masks_end, slots_begin, slots_end)
            || overlaps(masks_begin, masks_end, sidecar_begin, sidecar_end)
            || overlaps(masks_begin, masks_end, batch_begin, batch_end)
            || overlaps(slots_begin, slots_end, sidecar_begin, sidecar_end)
            || overlaps(slots_begin, slots_end, batch_begin, batch_end)
            || overlaps(sidecar_begin, sidecar_end, batch_begin, batch_end)) {
            return false;
        }
    }
    try {
        raw_status = successors == nullptr
            ? certificate.function_(
                  &runtime, &frame, &result, &input_window, &outputs)
            : certificate.successor_function_(
                  &runtime, &frame, &result, &input_window, &outputs,
                  successors);
    } catch (...) {
        failure = std::current_exception();
        return false;
    }
    return true;
}

JitResumeStatus
LlvmJit::resume(const JitProcessHandle process,
    const fsim_jit_runtime_instance_v2& runtime,
    fsim_jit_frame_v2& frame,
    fsim_jit_resume_result_v2& result) const
{
    return resume(bind(process), runtime, frame, result);
}

JitResumeStatus LlvmJit::resume_prevalidated(
    const JitProcessBinding process,
    const fsim_jit_runtime_instance_v2& runtime,
    fsim_jit_frame_v2& frame,
    fsim_jit_resume_result_v2& result) const
{
    if (!impl_ || process.owner_ != impl_.get() || process.entry_ == nullptr) {
        throw LlvmJitError("invalid prevalidated LLVM process binding");
    }
    return resume(process, runtime, frame, result);
}

std::optional<JitRequiredDirectReadLease>
LlvmJit::bind_required_direct_read_prevalidated(
    const JitProcessBinding process,
    const fsim_jit_runtime_instance_v2& runtime,
    fsim_jit_frame_v2& frame,
    fsim_jit_resume_result_v2& result) const
{
    return bind_required_direct_read_impl(
        process, runtime, frame, result, std::nullopt);
}

std::optional<JitRequiredDirectReadLease>
LlvmJit::bind_mapped_required_direct_read_prevalidated(
    const JitProcessBinding process,
    const fsim_jit_runtime_instance_v2& runtime,
    fsim_jit_frame_v2& frame,
    fsim_jit_resume_result_v2& result,
    const std::span<const JitDirectReadInstanceBinding> bindings) const
{
    return bind_required_direct_read_impl(
        process, runtime, frame, result, bindings);
}

std::optional<JitRequiredDirectReadLease>
LlvmJit::bind_required_direct_read_impl(
    const JitProcessBinding process,
    const fsim_jit_runtime_instance_v2& runtime,
    fsim_jit_frame_v2& frame,
    fsim_jit_resume_result_v2& result,
    const std::optional<std::span<const JitDirectReadInstanceBinding>> bindings) const
{
    if (!impl_ || process.owner_ != impl_.get() || process.entry_ == nullptr) {
        return std::nullopt;
    }
    const auto* const entry
        = static_cast<const Impl::NativeEntry*>(process.entry_);
    if (!entry->info.requires_direct_read_signals) {
        return std::nullopt;
    }
    try {
        validate_native_activation_v2(
            entry->info, runtime, frame, result,
            impl_->options.require_direct_update_slots, true, false, bindings);
    } catch (const LlvmJitError&) {
        return std::nullopt;
    }
    if (frame.state != FSIM_JIT_FRAME_STATE_READY_V2
        || frame.program_counter >= entry->info.operation_count
        || frame.native_call_depth != 0U) {
        return std::nullopt;
    }

    JitRequiredDirectReadLease lease;
    lease.owner_ = impl_.get();
    lease.entry_ = entry;
    lease.process_ = process;
    lease.capture_backing(runtime, frame, result);
    return lease;
}

std::optional<JitResumeStatus>
LlvmJit::resume_required_direct_read_prevalidated(
    const JitRequiredDirectReadLease& lease,
    const JitProcessBinding process,
    const fsim_jit_runtime_instance_v2& runtime,
    fsim_jit_frame_v2& frame,
    fsim_jit_resume_result_v2& result) const
{
    if (!impl_ || process.owner_ != impl_.get() || process.entry_ == nullptr
        || !lease.matches_backing(
            impl_.get(), process.entry_, process, runtime, frame, result)) {
        return std::nullopt;
    }
    const auto& entry
        = *static_cast<const Impl::NativeEntry*>(process.entry_);
    if (frame.state != FSIM_JIT_FRAME_STATE_READY_V2
        || frame.program_counter >= entry.info.operation_count
        || frame.native_call_depth != 0U) {
        return std::nullopt;
    }
    bool native_entered { };
    try {
        const auto status = resume_impl(
            process, runtime, frame, result, true, &native_entered);
        return native_entered
            ? std::optional<JitResumeStatus> { status }
            : std::nullopt;
    } catch (const LlvmJitError&) {
        if (native_entered) {
            throw;
        }
        return std::nullopt;
    }
}

std::size_t LlvmJit::resume_cohort_prevalidated(
    const std::span<JitProcessCohortResumeEntry> entries) const
{
    return resume_cohort_prevalidated_impl(entries, false);
}

std::size_t LlvmJit::resume_ordered_cohort_prevalidated(
    const std::span<JitProcessCohortResumeEntry> entries) const
{
    constexpr std::size_t maximum_members = 64U;
    if (entries.size() < 2U || entries.size() > maximum_members
        || std::ranges::any_of(entries, [](const auto& entry) {
            return entry.active != nullptr || entry.queued == nullptr
                || entry.waiting_on_static == nullptr || entry.process_status == nullptr;
        })) {
        return 0U;
    }
    return resume_cohort_prevalidated_impl(entries, true);
}

std::size_t LlvmJit::Impl::resume_ordered_prevalidated_members(
    const std::span<JitProcessCohortResumeEntry> entries,
    const std::span<const NativeEntry*> native_entries,
    const bool manages_process_state)
{
    if (entries.size() != native_entries.size()) {
        throw LlvmJitError(
            "ordered LLVM process fallback has mismatched members");
    }
    if (ordered_cohort_profile_enabled) {
        ordered_cohort_fallback_batches.fetch_add(
            1U, std::memory_order_relaxed);
    }

    std::size_t executed { };
    for (std::size_t index = 0U; index < entries.size(); ++index) {
        auto& entry = entries[index];
        const auto& native = *native_entries[index];
        entry.failure = { };
        if (manages_process_state) {
            *entry.queued = 0U;
            *entry.waiting_on_static = 0U;
            *entry.process_status = 1U;
        }

        // The v2 service contract forbids callbacks from unwinding across
        // this generated C ABI. Callback failures return runtime-error
        // statuses and are captured below.
        entry.status = native.function(
            entry.runtime, entry.frame, entry.result);
        ++executed;
        if (ordered_cohort_profile_enabled) {
            ordered_cohort_fallback_members.fetch_add(
                1U, std::memory_order_relaxed);
        }
        if (entry.status == FSIM_JIT_RESUME_STATUS_WAIT_SENSITIVITY_V2) {
            if (manages_process_state) {
                *entry.waiting_on_static = 1U;
                *entry.process_status = 2U;
            }
            continue;
        }
        if (entry.status == FSIM_JIT_RESUME_STATUS_RUNTIME_ERROR_V2) {
            try {
                if (const auto reason = decode_generated_runtime_error(
                        entry.result->delay)) {
                    throw LlvmJitGeneratedRuntimeError(
                        entry.result->instruction, *reason);
                }
                throw LlvmJitError(
                    "generated process returned an invalid runtime error "
                    "reason");
            } catch (...) {
                entry.failure = std::current_exception();
            }
        }
        return executed;
    }
    return executed;
}

std::size_t LlvmJit::resume_cohort_prevalidated_impl(
    const std::span<JitProcessCohortResumeEntry> entries,
    const bool ordered_bounded) const
{
    if (!impl_ || entries.size() < 2U
        || entries.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw LlvmJitError("invalid LLVM process cohort");
    }

    constexpr std::size_t inline_cohort_capacity = 512U;
    std::array<const Impl::NativeEntry*, inline_cohort_capacity>
        inline_native_entries { };
    std::vector<const Impl::NativeEntry*> overflow_native_entries;
    std::span<const Impl::NativeEntry*> native_entries;
    if (entries.size() <= inline_cohort_capacity) {
        native_entries = {
            inline_native_entries.data(), entries.size()
        };
    } else {
        overflow_native_entries.resize(entries.size());
        native_entries = overflow_native_entries;
    }
    std::size_t prepared_index { };
    std::size_t key = entries.size();
    const bool region_mode = std::ranges::all_of(
        entries, [](const auto& entry) { return entry.active != nullptr; });
    const bool has_partial_region = std::ranges::any_of(
                                        entries, [](const auto& entry) { return entry.active != nullptr; })
        && !region_mode;
    if (has_partial_region) {
        throw LlvmJitError(
            "LLVM process region has incomplete active state");
    }
    const bool manages_process_state = std::ranges::all_of(
        entries,
        [](const auto& entry) {
            return entry.queued != nullptr
                && entry.waiting_on_static != nullptr
                && entry.process_status != nullptr;
        });
    const bool has_partial_process_state = std::ranges::any_of(
        entries,
        [](const auto& entry) {
            const auto count = static_cast<unsigned>(entry.queued != nullptr)
                + static_cast<unsigned>(entry.waiting_on_static != nullptr)
                + static_cast<unsigned>(entry.process_status != nullptr);
            return count != 0U && count != 3U;
        });
    if (has_partial_process_state
        || (!manages_process_state
            && std::ranges::any_of(
                entries,
                [](const auto& entry) {
                    return entry.queued != nullptr
                        || entry.waiting_on_static != nullptr
                        || entry.process_status != nullptr;
                }))) {
        throw LlvmJitError(
            "LLVM process cohort has incomplete scheduler state");
    }
    if (manages_process_state) {
        key ^= static_cast<std::size_t>(0x51f15e5dU)
            + (key << 6U) + (key >> 2U);
    }
    if (region_mode) {
        key ^= static_cast<std::size_t>(0x8f31a9c7U)
            + (key << 6U) + (key >> 2U);
    }
    for (const auto& entry : entries) {
        if (entry.process.owner_ != impl_.get()
            || entry.process.entry_ == nullptr || entry.runtime == nullptr
            || entry.frame == nullptr || entry.result == nullptr) {
            throw LlvmJitError("invalid prevalidated LLVM process cohort entry");
        }
        const auto* const native
            = static_cast<const Impl::NativeEntry*>(entry.process.entry_);
        if (!region_mode || *entry.active != 0U) {
            validate_native_activation_v2(
                native->info, *entry.runtime, *entry.frame, *entry.result,
                impl_->options.require_direct_update_slots,
                native->info.requires_direct_read_signals);
        }
        native_entries[prepared_index++] = native;
        const auto member_hash = std::hash<const void*> { }(native);
        key ^= member_hash + static_cast<std::size_t>(0x9e3779b9U)
            + (key << 6U) + (key >> 2U);
    }

    NativeCohort* cohort { };
    {
        std::unique_lock lock { impl_->cohort_mutex };
        const auto found = impl_->cohort_functions.find(key);
        if (found != impl_->cohort_functions.end()) {
            const auto match = std::ranges::find_if(
                found->second,
                [&](const auto& candidate) {
                    return std::ranges::equal(
                               candidate->members, native_entries)
                        && candidate->manages_process_state
                        == manages_process_state
                        && candidate->region_mode == region_mode;
                });
            if (match != found->second.end()) {
                cohort = (*match)->function;
            }
        }
        if (cohort == nullptr) {
            if (ordered_bounded) {
                constexpr std::size_t maximum_materializations = 16U;
                if (impl_->ordered_cohort_materialization_attempts
                    >= maximum_materializations) {
                    if (impl_->ordered_cohort_profile_enabled) {
                        ++impl_->ordered_cohort_budget_misses;
                    }
                    lock.unlock();
                    return impl_->resume_ordered_prevalidated_members(
                        entries, native_entries, manages_process_state);
                }
                // Charge attempts before LLVM owns any generated objects, so
                // failed materialization retries cannot bypass the code budget.
                ++impl_->ordered_cohort_materialization_attempts;
            }
            const auto cohort_number = impl_->next_cohort++;
            const auto symbol = "fsim_process_cohort_"
                + std::to_string(cohort_number);
            auto context = std::make_unique<llvm::LLVMContext>();
            auto module = std::make_unique<llvm::Module>(
                symbol + ".module", *context);
            module->setDataLayout(impl_->jit->getDataLayout());
            module->setTargetTriple(impl_->jit->getTargetTriple());

            auto* const i32 = llvm::Type::getInt32Ty(*context);
            auto* const i8 = llvm::Type::getInt8Ty(*context);
            auto* const pointer = llvm::PointerType::getUnqual(*context);
            auto* const process_type = llvm::FunctionType::get(
                i32, { pointer, pointer, pointer }, false);
            auto* const cohort_type = llvm::FunctionType::get(
                i32,
                { pointer, pointer, pointer, pointer,
                    pointer, pointer, pointer, pointer, i32 },
                false);
            auto* const function = llvm::Function::Create(
                cohort_type, llvm::Function::ExternalLinkage,
                symbol, *module);
            function->setCallingConv(llvm::CallingConv::C);
            auto arguments = function->arg_begin();
            auto* const runtimes = &*arguments++;
            auto* const frames = &*arguments++;
            auto* const results = &*arguments++;
            auto* const statuses = &*arguments++;
            auto* const queued_states = &*arguments++;
            auto* const waiting_states = &*arguments++;
            auto* const process_statuses = &*arguments++;
            auto* const active_states = &*arguments++;
            auto* const count = &*arguments;

            auto* const entry_block = llvm::BasicBlock::Create(
                *context, "entry", function);
            llvm::IRBuilder<> builder(entry_block);
            auto* const valid_block = llvm::BasicBlock::Create(
                *context, "run", function);
            auto* const invalid_block = llvm::BasicBlock::Create(
                *context, "invalid", function);
            builder.CreateCondBr(
                builder.CreateICmpEQ(
                    count,
                    llvm::ConstantInt::get(i32, entries.size())),
                valid_block, invalid_block);
            builder.SetInsertPoint(invalid_block);
            builder.CreateRet(llvm::ConstantInt::get(i32, 0U));
            builder.SetInsertPoint(valid_block);

            for (std::size_t index = 0; index < native_entries.size(); ++index) {
                auto* const offset = llvm::ConstantInt::get(i32, index);
                const auto load_pointer = [&](llvm::Value* array) {
                    return builder.CreateLoad(
                        pointer,
                        builder.CreateGEP(pointer, array, offset));
                };
                auto* const queued_state = load_pointer(queued_states);
                auto* const waiting_state = load_pointer(waiting_states);
                auto* const process_status = load_pointer(process_statuses);
                if (region_mode) {
                    auto* const active_state = load_pointer(active_states);
                    auto* const run_member = llvm::BasicBlock::Create(
                        *context, "active", function);
                    auto* const continue_region = llvm::BasicBlock::Create(
                        *context, "continue", function);
                    builder.CreateCondBr(
                        builder.CreateICmpNE(
                            builder.CreateLoad(i8, active_state),
                            llvm::ConstantInt::get(i8, 0U)),
                        run_member, continue_region);
                    builder.SetInsertPoint(run_member);
                    builder.CreateStore(
                        llvm::ConstantInt::get(i8, 0U), active_state);
                    if (manages_process_state) {
                        builder.CreateStore(
                            llvm::ConstantInt::get(i8, 0U), queued_state);
                        builder.CreateStore(
                            llvm::ConstantInt::get(i8, 0U), waiting_state);
                        builder.CreateStore(
                            llvm::ConstantInt::get(i8, 1U), process_status);
                    }
                    auto callee = module->getOrInsertFunction(
                        native_entries[index]->symbol, process_type);
                    llvm::cast<llvm::Function>(callee.getCallee())
                        ->addFnAttr(llvm::Attribute::NoUnwind);
                    auto* const status = builder.CreateCall(
                        callee,
                        { load_pointer(runtimes), load_pointer(frames),
                            load_pointer(results) });
                    builder.CreateStore(
                        status,
                        builder.CreateGEP(i32, statuses, offset));
                    auto* const rearm = llvm::BasicBlock::Create(
                        *context, "rearm", function);
                    auto* const stop = llvm::BasicBlock::Create(
                        *context, "stop", function);
                    builder.CreateCondBr(
                        builder.CreateICmpEQ(
                            status,
                            llvm::ConstantInt::get(
                                i32,
                                FSIM_JIT_RESUME_STATUS_WAIT_SENSITIVITY_V2)),
                        rearm, stop);
                    builder.SetInsertPoint(stop);
                    builder.CreateRet(llvm::ConstantInt::get(
                        i32, static_cast<std::uint32_t>(index + 1U)));
                    builder.SetInsertPoint(rearm);
                    if (manages_process_state) {
                        builder.CreateStore(
                            llvm::ConstantInt::get(i8, 1U), waiting_state);
                        builder.CreateStore(
                            llvm::ConstantInt::get(i8, 2U), process_status);
                    }
                    builder.CreateBr(continue_region);
                    builder.SetInsertPoint(continue_region);
                    if (index + 1U == native_entries.size()) {
                        builder.CreateRet(llvm::ConstantInt::get(
                            i32,
                            static_cast<std::uint32_t>(
                                native_entries.size())));
                    }
                    continue;
                }
                if (manages_process_state) {
                    builder.CreateStore(
                        llvm::ConstantInt::get(i8, 0U), queued_state);
                    builder.CreateStore(
                        llvm::ConstantInt::get(i8, 0U), waiting_state);
                    builder.CreateStore(
                        llvm::ConstantInt::get(i8, 1U), process_status);
                }
                auto callee = module->getOrInsertFunction(
                    native_entries[index]->symbol, process_type);
                llvm::cast<llvm::Function>(callee.getCallee())
                    ->addFnAttr(llvm::Attribute::NoUnwind);
                auto* const status = builder.CreateCall(
                    callee,
                    { load_pointer(runtimes), load_pointer(frames),
                        load_pointer(results) });
                builder.CreateStore(
                    status,
                    builder.CreateGEP(i32, statuses, offset));
                const auto executed = static_cast<std::uint32_t>(index + 1U);
                if (static_cast<std::size_t>(executed)
                    == native_entries.size()) {
                    if (manages_process_state) {
                        auto* const rearm = llvm::BasicBlock::Create(
                            *context, "rearm", function);
                        auto* const finish = llvm::BasicBlock::Create(
                            *context, "finish", function);
                        builder.CreateCondBr(
                            builder.CreateICmpEQ(
                                status,
                                llvm::ConstantInt::get(
                                    i32,
                                    FSIM_JIT_RESUME_STATUS_WAIT_SENSITIVITY_V2)),
                            rearm, finish);
                        builder.SetInsertPoint(rearm);
                        builder.CreateStore(
                            llvm::ConstantInt::get(i8, 1U), waiting_state);
                        builder.CreateStore(
                            llvm::ConstantInt::get(i8, 2U), process_status);
                        builder.CreateBr(finish);
                        builder.SetInsertPoint(finish);
                    }
                    builder.CreateRet(
                        llvm::ConstantInt::get(i32, executed));
                    break;
                }
                auto* const next = llvm::BasicBlock::Create(
                    *context, "next", function);
                auto* const stop = llvm::BasicBlock::Create(
                    *context, "stop", function);
                builder.CreateCondBr(
                    builder.CreateICmpEQ(
                        status,
                        llvm::ConstantInt::get(
                            i32,
                            FSIM_JIT_RESUME_STATUS_WAIT_SENSITIVITY_V2)),
                    next, stop);
                builder.SetInsertPoint(stop);
                builder.CreateRet(llvm::ConstantInt::get(i32, executed));
                builder.SetInsertPoint(next);
                if (manages_process_state) {
                    builder.CreateStore(
                        llvm::ConstantInt::get(i8, 1U), waiting_state);
                    builder.CreateStore(
                        llvm::ConstantInt::get(i8, 2U), process_status);
                }
            }

            if (auto message = verify_error(*module); !message.empty()) {
                throw LlvmJitError(
                    "generated invalid LLVM process cohort: " + message);
            }
            apply_jit_module_no_unwind_contract(*module);
            optimize_module(*module, impl_->options.optimization);
            require_jit_module_no_unwind_contract(*module);
            if (auto error = impl_->jit->addIRModule(
                    llvm::orc::ThreadSafeModule(
                        std::move(module), std::move(context)))) {
                throw LlvmJitError(
                    "cannot add LLVM process cohort: "
                    + llvm_error(std::move(error)));
            }
            auto address = unwrap(
                impl_->jit->lookup(symbol),
                "cannot materialize LLVM process cohort");
            cohort = address.template toPtr<NativeCohort>();
            if (cohort == nullptr) {
                throw LlvmJitError(
                    "LLVM returned a null process cohort address");
            }
            auto members = std::vector<const Impl::NativeEntry*> {
                native_entries.begin(), native_entries.end()
            };
            impl_->cohort_functions[key].push_back(
                std::make_unique<Impl::NativeCohortEntry>(
                    Impl::NativeCohortEntry {
                        cohort, std::move(members), manages_process_state,
                        region_mode }));
        }
    }

    std::array<const fsim_jit_runtime_instance_v2*, inline_cohort_capacity>
        inline_runtimes { };
    std::array<fsim_jit_frame_v2*, inline_cohort_capacity>
        inline_frames { };
    std::array<fsim_jit_resume_result_v2*, inline_cohort_capacity>
        inline_results { };
    std::array<std::uint32_t, inline_cohort_capacity> inline_statuses { };
    std::array<std::uint8_t*, inline_cohort_capacity> inline_queued { };
    std::array<std::uint8_t*, inline_cohort_capacity> inline_waiting { };
    std::array<std::uint8_t*, inline_cohort_capacity>
        inline_process_statuses { };
    std::array<std::uint8_t*, inline_cohort_capacity> inline_active { };
    std::vector<const fsim_jit_runtime_instance_v2*> overflow_runtimes;
    std::vector<fsim_jit_frame_v2*> overflow_frames;
    std::vector<fsim_jit_resume_result_v2*> overflow_results;
    std::vector<std::uint32_t> overflow_statuses;
    std::vector<std::uint8_t*> overflow_queued;
    std::vector<std::uint8_t*> overflow_waiting;
    std::vector<std::uint8_t*> overflow_process_statuses;
    std::vector<std::uint8_t*> overflow_active;
    std::span<const fsim_jit_runtime_instance_v2*> runtimes;
    std::span<fsim_jit_frame_v2*> frames;
    std::span<fsim_jit_resume_result_v2*> results;
    std::span<std::uint32_t> statuses;
    std::span<std::uint8_t*> queued;
    std::span<std::uint8_t*> waiting;
    std::span<std::uint8_t*> process_statuses;
    std::span<std::uint8_t*> active;
    if (entries.size() <= inline_cohort_capacity) {
        runtimes = { inline_runtimes.data(), entries.size() };
        frames = { inline_frames.data(), entries.size() };
        results = { inline_results.data(), entries.size() };
        statuses = { inline_statuses.data(), entries.size() };
        queued = { inline_queued.data(), entries.size() };
        waiting = { inline_waiting.data(), entries.size() };
        process_statuses = {
            inline_process_statuses.data(), entries.size()
        };
        active = { inline_active.data(), entries.size() };
    } else {
        overflow_runtimes.resize(entries.size());
        overflow_frames.resize(entries.size());
        overflow_results.resize(entries.size());
        overflow_statuses.resize(entries.size());
        overflow_queued.resize(entries.size());
        overflow_waiting.resize(entries.size());
        overflow_process_statuses.resize(entries.size());
        overflow_active.resize(entries.size());
        runtimes = overflow_runtimes;
        frames = overflow_frames;
        results = overflow_results;
        statuses = overflow_statuses;
        queued = overflow_queued;
        waiting = overflow_waiting;
        process_statuses = overflow_process_statuses;
        active = overflow_active;
    }
    std::ranges::fill(
        statuses, std::numeric_limits<std::uint32_t>::max());
    for (std::size_t entry_index = 0U;
        entry_index < entries.size(); ++entry_index) {
        runtimes[entry_index] = entries[entry_index].runtime;
        frames[entry_index] = entries[entry_index].frame;
        results[entry_index] = entries[entry_index].result;
        queued[entry_index] = entries[entry_index].queued;
        waiting[entry_index] = entries[entry_index].waiting_on_static;
        process_statuses[entry_index]
            = entries[entry_index].process_status;
        active[entry_index] = entries[entry_index].active;
    }
    const auto executed = cohort(
        runtimes.data(), frames.data(), results.data(), statuses.data(),
        queued.data(), waiting.data(), process_statuses.data(),
        active.data(),
        static_cast<std::uint32_t>(entries.size()));
    if (executed == 0U || executed > entries.size()) {
        throw LlvmJitError(
            "generated LLVM process cohort returned an invalid count");
    }
    for (std::size_t index = 0; index < executed; ++index) {
        if (statuses[index]
            == std::numeric_limits<std::uint32_t>::max()) {
            continue;
        }
        entries[index].status = statuses[index];
        if (statuses[index] == FSIM_JIT_RESUME_STATUS_RUNTIME_ERROR_V2) {
            try {
                if (const auto reason = decode_generated_runtime_error(
                        entries[index].result->delay)) {
                    throw LlvmJitGeneratedRuntimeError(
                        entries[index].result->instruction, *reason);
                }
                throw LlvmJitError(
                    "generated process returned an invalid runtime error reason");
            } catch (...) {
                entries[index].failure = std::current_exception();
            }
        }
    }
    return executed;
}

JitProcessCohortBinding LlvmJit::bind_cohort_prevalidated(
    const std::span<JitProcessCohortResumeEntry> entries) const
{
    if (!impl_ || entries.size() < 2U
        || entries.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw LlvmJitError("invalid LLVM process cohort binding");
    }

    std::vector<const Impl::NativeEntry*> members;
    members.reserve(entries.size());
    std::size_t key = entries.size();
    const bool region_mode = std::ranges::all_of(
        entries, [](const auto& entry) { return entry.active != nullptr; });
    if (region_mode || std::ranges::any_of(entries, [](const auto& entry) {
            return entry.active != nullptr;
        })) {
        throw LlvmJitError(
            "stable LLVM cohort binding does not accept region entries");
    }
    const bool manages_process_state = std::ranges::all_of(
        entries,
        [](const auto& entry) {
            return entry.queued != nullptr
                && entry.waiting_on_static != nullptr
                && entry.process_status != nullptr;
        });
    if (std::ranges::any_of(
            entries,
            [&](const auto& entry) {
                const auto count
                    = static_cast<unsigned>(entry.queued != nullptr)
                    + static_cast<unsigned>(
                        entry.waiting_on_static != nullptr)
                    + static_cast<unsigned>(
                        entry.process_status != nullptr);
                return entry.process.owner_ != impl_.get()
                    || entry.process.entry_ == nullptr
                    || entry.runtime == nullptr || entry.frame == nullptr
                    || entry.result == nullptr
                    || (count != 0U && count != 3U)
                    || (manages_process_state && count != 3U);
            })) {
        throw LlvmJitError("invalid LLVM process cohort binding entry");
    }
    if (manages_process_state) {
        key ^= static_cast<std::size_t>(0x51f15e5dU)
            + (key << 6U) + (key >> 2U);
    }
    for (const auto& entry : entries) {
        const auto* const native
            = static_cast<const Impl::NativeEntry*>(entry.process.entry_);
        members.push_back(native);
        const auto member_hash = std::hash<const void*> { }(native);
        key ^= member_hash + static_cast<std::size_t>(0x9e3779b9U)
            + (key << 6U) + (key >> 2U);
    }

    const Impl::NativeCohortEntry* native_cohort { };
    std::scoped_lock lock { impl_->cohort_mutex };
    const auto found = impl_->cohort_functions.find(key);
    if (found != impl_->cohort_functions.end()) {
        const auto match = std::ranges::find_if(
            found->second,
            [&](const auto& candidate) {
                return std::ranges::equal(candidate->members, members)
                    && candidate->manages_process_state
                    == manages_process_state
                    && !candidate->region_mode;
            });
        if (match != found->second.end()) {
            native_cohort = match->get();
        }
    }
    if (native_cohort == nullptr) {
        throw LlvmJitError(
            "LLVM process cohort must be materialized before binding");
    }

    if (impl_->next_bound_cohort_generation == 0U) {
        throw LlvmJitError("LLVM process cohort binding generation exhausted");
    }
    auto bound = std::make_shared<Impl::NativeBoundCohortEntry>();
    bound->function = native_cohort->function;
    bound->generation = impl_->next_bound_cohort_generation++;
    bound->members = std::move(members);
    bound->manages_process_state = manages_process_state;
    bound->region_mode = false;
    bound->runtimes.reserve(entries.size());
    bound->frames.reserve(entries.size());
    bound->results.reserve(entries.size());
    bound->statuses.resize(entries.size());
    bound->queued.reserve(entries.size());
    bound->waiting.reserve(entries.size());
    bound->process_statuses.reserve(entries.size());
    bound->active.reserve(entries.size());
    for (const auto& entry : entries) {
        bound->runtimes.push_back(entry.runtime);
        bound->frames.push_back(entry.frame);
        bound->results.push_back(entry.result);
        bound->queued.push_back(entry.queued);
        bound->waiting.push_back(entry.waiting_on_static);
        bound->process_statuses.push_back(entry.process_status);
        bound->active.push_back(entry.active);
    }
    auto* const result = bound.get();
    impl_->bound_cohorts.push_back(std::move(bound));
    return JitProcessCohortBinding {
        impl_.get(), result, result->generation };
}

bool LlvmJit::release_cohort_binding(
    const JitProcessCohortBinding cohort) const
{
    if (!impl_ || cohort.owner_ != impl_.get()) {
        throw LlvmJitError("invalid prevalidated LLVM cohort binding");
    }
    const std::scoped_lock lock { impl_->cohort_mutex };
    const auto found = std::ranges::find_if(
        impl_->bound_cohorts, [&](const auto& candidate) {
            return candidate.get() == cohort.entry_
                && candidate->generation == cohort.generation_;
        });
    if (found != impl_->bound_cohorts.end()) {
        impl_->bound_cohorts.erase(found);
        return true;
    }
    return false;
}

std::size_t LlvmJit::active_cohort_binding_count() const
{
    if (!impl_) {
        throw LlvmJitError("cannot use a moved-from LlvmJit");
    }
    const std::scoped_lock lock { impl_->cohort_mutex };
    return impl_->bound_cohorts.size();
}

std::size_t LlvmJit::resume_cohort_prevalidated(
    const JitProcessCohortBinding cohort,
    const std::span<JitProcessCohortResumeEntry> entries) const
{
    if (!impl_ || cohort.owner_ != impl_.get() || cohort.entry_ == nullptr) {
        throw LlvmJitError("invalid prevalidated LLVM cohort binding");
    }
    std::shared_ptr<Impl::NativeBoundCohortEntry> bound;
    {
        const std::scoped_lock lock { impl_->cohort_mutex };
        const auto found = std::ranges::find_if(
            impl_->bound_cohorts, [&](const auto& candidate) {
                return candidate.get() == cohort.entry_
                    && candidate->generation == cohort.generation_;
            });
        if (found == impl_->bound_cohorts.end()) {
            throw LlvmJitError("stale prevalidated LLVM cohort binding");
        }
        bound = *found;
    }
    if (entries.size() != bound->members.size()) {
        throw LlvmJitError("prevalidated LLVM cohort binding size changed");
    }

    for (std::size_t index = 0; index < bound->members.size(); ++index) {
        if (bound->runtimes[index] == nullptr
            || bound->frames[index] == nullptr
            || bound->results[index] == nullptr
            || entries[index].process.owner_ != impl_.get()
            || entries[index].process.entry_ != bound->members[index]
            || entries[index].runtime != bound->runtimes[index]
            || entries[index].frame != bound->frames[index]
            || entries[index].result != bound->results[index]) {
            throw LlvmJitError(
                "prevalidated LLVM cohort activation identity changed");
        }
        validate_native_activation_v2(
            bound->members[index]->info, *bound->runtimes[index],
            *bound->frames[index], *bound->results[index],
            impl_->options.require_direct_update_slots,
            bound->members[index]->info.requires_direct_read_signals);
    }

    const auto executed = bound->function(
        bound->runtimes.data(), bound->frames.data(), bound->results.data(),
        bound->statuses.data(), bound->queued.data(), bound->waiting.data(),
        bound->process_statuses.data(), bound->active.data(),
        static_cast<std::uint32_t>(bound->members.size()));
    if (executed == 0U || executed > entries.size()) {
        throw LlvmJitError(
            "bound LLVM process cohort returned an invalid count");
    }
    for (std::size_t index = 0; index < executed; ++index) {
        entries[index].failure = { };
        entries[index].status = bound->statuses[index];
        if (bound->statuses[index]
            == FSIM_JIT_RESUME_STATUS_RUNTIME_ERROR_V2) {
            try {
                if (const auto reason = decode_generated_runtime_error(
                        bound->results[index]->delay)) {
                    throw LlvmJitGeneratedRuntimeError(
                        bound->results[index]->instruction, *reason);
                }
                throw LlvmJitError(
                    "generated process returned an invalid runtime error "
                    "reason");
            } catch (...) {
                entries[index].failure = std::current_exception();
            }
        }
    }
    return executed;
}

JitResumeStatus
LlvmJit::resume(const JitProcessBinding process,
    const fsim_jit_runtime_instance_v2& runtime,
    fsim_jit_frame_v2& frame,
    fsim_jit_resume_result_v2& result) const
{
    return resume_impl(process, runtime, frame, result, false, nullptr);
}

JitResumeStatus LlvmJit::resume_impl(
    const JitProcessBinding process,
    const fsim_jit_runtime_instance_v2& runtime,
    fsim_jit_frame_v2& frame,
    fsim_jit_resume_result_v2& result,
    const bool direct_backing_prevalidated,
    bool* const native_entered) const
{
    if (native_entered != nullptr) {
        *native_entered = false;
    }
    if (!impl_) {
        throw LlvmJitError("cannot use a moved-from LlvmJit");
    }
    validate_runtime_instance_v2(runtime);
    if (result.abi_version != FSIM_JIT_RESUME_RESULT_ABI_VERSION_V2) {
        throw LlvmJitError("JIT resume-result ABI version mismatch");
    }
    if (result.struct_size < sizeof(fsim_jit_resume_result_v2)) {
        throw LlvmJitError("JIT resume-result ABI structure is too small");
    }

    if (process.owner_ != impl_.get() || process.entry_ == nullptr) {
        throw LlvmJitError("invalid LLVM process binding");
    }
    const auto& entry
        = *static_cast<const Impl::NativeEntry*>(process.entry_);
    validate_native_activation_v2(
        entry.info, runtime, frame, result,
        impl_->options.require_direct_update_slots,
        entry.info.requires_direct_read_signals,
        direct_backing_prevalidated);
    const auto terminal_result =
        [&](const std::uint32_t status) -> JitResumeStatus {
        result.status = status;
        result.instruction = frame.last_instruction;
        result.delay = 0;
        return static_cast<JitResumeStatus>(status);
    };
    switch (frame.state) {
    case FSIM_JIT_FRAME_STATE_READY_V2:
        if (frame.program_counter >= entry.info.operation_count) {
            throw LlvmJitError(
                "JIT frame program counter is outside the operation stream");
        }
        break;
    case FSIM_JIT_FRAME_STATE_COMPLETED_V2:
        return terminal_result(FSIM_JIT_RESUME_STATUS_COMPLETED_V2);
    case FSIM_JIT_FRAME_STATE_STOPPED_V2:
        return terminal_result(FSIM_JIT_RESUME_STATUS_STOPPED_V2);
    case FSIM_JIT_FRAME_STATE_ASSERTION_FAILED_V2:
        return terminal_result(FSIM_JIT_RESUME_STATUS_ASSERTION_FAILED_V2);
    case FSIM_JIT_FRAME_STATE_RUNTIME_ERROR_V2:
        if (const auto reason = decode_generated_runtime_error(frame.program_counter)) {
            throw LlvmJitGeneratedRuntimeError(
                frame.last_instruction, *reason);
        }
        throw LlvmJitError(
            "JIT frame contains an invalid generated runtime error reason");
    default:
        throw LlvmJitError("JIT frame state is invalid");
    }

    if (native_entered != nullptr) {
        *native_entered = true;
    }
    const auto raw_status = entry.function(&runtime, &frame, &result);
    if (raw_status != result.status) {
        throw LlvmJitError(
            "generated process returned an inconsistent resume status");
    }
    switch (raw_status) {
    case FSIM_JIT_RESUME_STATUS_COMPLETED_V2:
        if (frame.state != FSIM_JIT_FRAME_STATE_COMPLETED_V2) {
            throw LlvmJitError("generated process returned an invalid frame state");
        }
        return JitResumeStatus::completed;
    case FSIM_JIT_RESUME_STATUS_ASSERTION_FAILED_V2:
        if (frame.state != FSIM_JIT_FRAME_STATE_ASSERTION_FAILED_V2) {
            throw LlvmJitError("generated process returned an invalid frame state");
        }
        return JitResumeStatus::assertion_failed;
    case FSIM_JIT_RESUME_STATUS_WAIT_FOR_V2:
        if (frame.state != FSIM_JIT_FRAME_STATE_READY_V2) {
            throw LlvmJitError("generated process returned an invalid frame state");
        }
        return JitResumeStatus::wait_for;
    case FSIM_JIT_RESUME_STATUS_WAIT_ON_V2:
        if (frame.state != FSIM_JIT_FRAME_STATE_READY_V2) {
            throw LlvmJitError("generated process returned an invalid frame state");
        }
        return JitResumeStatus::wait_on;
    case FSIM_JIT_RESUME_STATUS_WAIT_SENSITIVITY_V2:
        if (frame.state != FSIM_JIT_FRAME_STATE_READY_V2) {
            throw LlvmJitError("generated process returned an invalid frame state");
        }
        return JitResumeStatus::wait_sensitivity;
    case FSIM_JIT_RESUME_STATUS_WAIT_FOREVER_V2:
        if (frame.state != FSIM_JIT_FRAME_STATE_READY_V2) {
            throw LlvmJitError("generated process returned an invalid frame state");
        }
        return JitResumeStatus::wait_forever;
    case FSIM_JIT_RESUME_STATUS_DEBUG_POINT_V2:
        if (frame.state != FSIM_JIT_FRAME_STATE_READY_V2) {
            throw LlvmJitError("generated process returned an invalid frame state");
        }
        return JitResumeStatus::debug_point;
    case FSIM_JIT_RESUME_STATUS_YIELDED_V2:
        if (frame.state != FSIM_JIT_FRAME_STATE_READY_V2) {
            throw LlvmJitError("generated process returned an invalid frame state");
        }
        return JitResumeStatus::yielded;
    case FSIM_JIT_RESUME_STATUS_PAUSED_V2:
        if (frame.state != FSIM_JIT_FRAME_STATE_READY_V2) {
            throw LlvmJitError("generated process returned an invalid frame state");
        }
        return JitResumeStatus::paused;
    case FSIM_JIT_RESUME_STATUS_FORK_V2:
        if (frame.state != FSIM_JIT_FRAME_STATE_READY_V2) {
            throw LlvmJitError("generated process returned an invalid frame state");
        }
        return JitResumeStatus::fork;
    case FSIM_JIT_RESUME_STATUS_FORK_END_V2:
        if (frame.state != FSIM_JIT_FRAME_STATE_READY_V2) {
            throw LlvmJitError("generated process returned an invalid frame state");
        }
        return JitResumeStatus::fork_end;
    case FSIM_JIT_RESUME_STATUS_WAIT_FORK_V2:
        if (frame.state != FSIM_JIT_FRAME_STATE_READY_V2) {
            throw LlvmJitError("generated process returned an invalid frame state");
        }
        return JitResumeStatus::wait_fork;
    case FSIM_JIT_RESUME_STATUS_DISABLE_FORK_V2:
        if (frame.state != FSIM_JIT_FRAME_STATE_READY_V2) {
            throw LlvmJitError("generated process returned an invalid frame state");
        }
        return JitResumeStatus::disable_fork;
    case FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY_V2:
        if (frame.state != FSIM_JIT_FRAME_STATE_READY_V2) {
            throw LlvmJitError("generated process returned an invalid frame state");
        }
        return JitResumeStatus::simir_boundary;
    case FSIM_JIT_RESUME_STATUS_STOPPED_V2:
        if (frame.state != FSIM_JIT_FRAME_STATE_STOPPED_V2) {
            throw LlvmJitError("generated process returned an invalid frame state");
        }
        return JitResumeStatus::stopped;
    case FSIM_JIT_RESUME_STATUS_RUNTIME_ERROR_V2:
        if (frame.state != FSIM_JIT_FRAME_STATE_RUNTIME_ERROR_V2) {
            throw LlvmJitError("generated process returned an invalid frame state");
        }
        if (const auto reason = decode_generated_runtime_error(result.delay)) {
            throw LlvmJitGeneratedRuntimeError(
                result.instruction, *reason);
        }
        throw LlvmJitError(
            "generated process returned an invalid runtime error reason");
    default:
        throw LlvmJitError("generated process returned an unknown resume status");
    }
}

JitExecutionStatus
LlvmJit::execute(const JitProcessHandle process,
    const fsim_jit_runtime_instance_v2& runtime) const
{
    const auto binding = bind(process);
    const auto& entry
        = *static_cast<const Impl::NativeEntry*>(binding.entry_);
    if (entry.info.requires_resume) {
        throw LlvmJitUnsupportedError(
            "compiled process can suspend; use initialize_frame() and resume()");
    }

    std::vector<std::uint64_t> register_aval(
        entry.info.frame_layout.register_word_count);
    std::vector<std::uint64_t> register_bval(
        entry.info.frame_layout.register_word_count);
    std::vector<std::uint8_t> register_initialized(
        entry.info.frame_layout.register_count);
    std::vector<std::uint64_t> register_logic9_plane2(
        entry.info.frame_layout.uses_logic9
            ? entry.info.frame_layout.register_word_count
            : 0);
    std::vector<std::uint64_t> register_logic9_plane3(
        entry.info.frame_layout.uses_logic9
            ? entry.info.frame_layout.register_word_count
            : 0);
    fsim_jit_frame_v2 frame { };
    initialize_frame(
        binding,
        frame,
        register_aval,
        register_bval,
        register_initialized,
        register_logic9_plane2,
        register_logic9_plane3);
    fsim_jit_resume_result_v2 result {
        FSIM_JIT_RESUME_RESULT_ABI_VERSION_V2,
        static_cast<std::uint32_t>(sizeof(fsim_jit_resume_result_v2)),
        0,
        FSIM_JIT_INVALID_INSTRUCTION_V2,
        0,
    };
    switch (resume(binding, runtime, frame, result)) {
    case JitResumeStatus::completed:
        return JitExecutionStatus::completed;
    case JitResumeStatus::assertion_failed:
        return JitExecutionStatus::assertion_failed;
    case JitResumeStatus::stopped:
        return JitExecutionStatus::stopped;
    case JitResumeStatus::wait_for:
    case JitResumeStatus::wait_on:
    case JitResumeStatus::wait_sensitivity:
    case JitResumeStatus::wait_forever:
    case JitResumeStatus::yielded:
    case JitResumeStatus::debug_point:
    case JitResumeStatus::paused:
    case JitResumeStatus::fork:
    case JitResumeStatus::fork_end:
    case JitResumeStatus::wait_fork:
    case JitResumeStatus::disable_fork:
    case JitResumeStatus::simir_boundary:
        throw LlvmJitError(
            "compiled process suspended during one-shot execution");
    default:
        throw LlvmJitError("generated process returned an unknown resume status");
    }
}

} // namespace fsim::compiler
