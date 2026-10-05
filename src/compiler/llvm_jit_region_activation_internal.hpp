// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/compiler/llvm_jit.hpp"
#include "fsim/runtime/simir_region_activation.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <span>
#include <string_view>
#include <utility>

namespace fsim::compiler {
class LlvmRegionKernelExecutor;
}

namespace fsim::compiler::llvm_detail {

enum class RegionKernelBodySelection : std::uint8_t {
    never_entered,
    four_state,
    known_logic4,
    guarded_constant_inputs,
};

struct RegionKernelTestAccess final {
    [[nodiscard]] static std::string_view native_code_identity(
        const fsim::compiler::LlvmRegionKernelExecutor& executor) noexcept;

    [[nodiscard]] static const void* native_body_identity(
        const fsim::compiler::LlvmRegionKernelExecutor& executor) noexcept;

    [[nodiscard]] static const void* prepared_template_identity(
        const fsim::compiler::LlvmRegionKernelExecutor& executor) noexcept;

    [[nodiscard]] static std::size_t prepared_template_build_count() noexcept;

    [[nodiscard]] static std::size_t
    prepared_template_validation_count() noexcept;

    [[nodiscard]] static const void* frame_identity(
        const fsim::compiler::LlvmRegionKernelExecutor& executor) noexcept;

    [[nodiscard]] static const void* register_storage_identity(
        const fsim::compiler::LlvmRegionKernelExecutor& executor) noexcept;

    [[nodiscard]] static const runtime::simir::RegionConeActivationKernel*
    source_kernel(
        const fsim::compiler::LlvmRegionKernelExecutor& executor) noexcept;

    [[nodiscard]] static RegionKernelBodySelection last_body_selection(
        const fsim::compiler::LlvmRegionKernelExecutor& executor) noexcept;

    [[nodiscard]] static const runtime::simir::Process* synthetic_process(
        const fsim::compiler::LlvmRegionKernelExecutor& executor) noexcept;

    [[nodiscard]] static std::span<const std::uint64_t> direct_signal_aval(
        const fsim::compiler::LlvmRegionKernelExecutor& executor) noexcept;

    [[nodiscard]] static std::span<const std::uint64_t> direct_signal_bval(
        const fsim::compiler::LlvmRegionKernelExecutor& executor) noexcept;
};

/// Exact storage owned by a persistent region-kernel executor.
///
/// This view is compiler-internal. It is inspected once while binding the
/// wrapper's stable runtime/frame layout. The wrapper owns every backing
/// vector and does not resize it for the certificate lifetime.
struct RegionKernelActivationBacking final {
    const fsim_jit_services_v2* services { };
    void* callback_context { };
    std::span<const std::uint32_t> signal_widths;
    std::span<const runtime::simir::ValueKind> signal_value_kinds;
    std::span<const std::uint32_t> direct_read_signals;
    std::span<const std::uint32_t> direct_update_signals;
    std::span<const std::uint32_t> register_widths;
    std::span<const std::uint32_t> register_word_offsets;
    std::span<std::uint64_t> direct_signal_aval;
    std::span<std::uint64_t> direct_signal_bval;
    std::span<std::uint64_t> direct_signal_logic9_plane0;
    std::span<std::uint64_t> direct_signal_logic9_plane1;
    std::span<std::uint64_t> direct_signal_logic9_plane2;
    std::span<std::uint64_t> direct_signal_logic9_plane3;
    // Signal offsets are a full prefix over every synthetic signal.
    std::span<const std::uint32_t> direct_wide_signal_offsets;
    std::span<std::uint64_t> direct_wide_signal_aval;
    std::span<std::uint64_t> direct_wide_signal_bval;
    std::span<std::uint64_t> direct_wide_signal_logic9_plane2;
    std::span<std::uint64_t> direct_wide_signal_logic9_plane3;
    std::span<std::uint64_t> register_aval;
    std::span<std::uint64_t> register_bval;
    std::span<std::uint64_t> register_logic9_plane2;
    std::span<std::uint64_t> register_logic9_plane3;
    std::span<std::uint8_t> register_initialized;
    std::span<const runtime::simir::ValueKind> register_value_kinds;
    std::span<const std::uint8_t> register_values_persistent;
    std::span<const std::uint8_t> register_export_mask;
    std::span<fsim_jit_update_slot_v2> direct_update_slots;
    // One offset per slot; narrow slots carry size_t::max() as a sentinel.
    std::span<const std::size_t> direct_update_wide_word_offsets;
    std::span<std::uint64_t> direct_update_wide_aval;
    std::span<std::uint64_t> direct_update_wide_bval;
    std::span<std::uint64_t> direct_update_wide_mask;
    std::span<std::uint64_t> direct_update_active_words;
};

[[nodiscard]] inline std::uint32_t activation_words_for_width(const std::uint32_t width) noexcept
{
    return width / 64U + (width % 64U != 0U ? 1U : 0U);
}

[[nodiscard]] inline bool activation_register_word_layout_matches(
        const std::span<const std::uint32_t> widths,
        const std::span<const std::uint32_t> word_offsets, const std::uint32_t total_words) noexcept
{
    if (widths.size() != word_offsets.size()) {
        return false;
    }
    std::uint64_t next_word { };
    constexpr auto max_u32 = std::numeric_limits<std::uint32_t>::max();
    for (std::size_t reg = 0U; reg < widths.size(); ++reg) {
        const auto words = activation_words_for_width(widths[reg]);
        if (next_word > max_u32 || word_offsets[reg] != next_word || words > max_u32 - next_word) {
            return false;
        }
        next_word += words;
    }
    return next_word == total_words;
}

[[nodiscard]] inline bool activation_word_spans_overlap(const std::span<const std::uint64_t> left,
        const std::span<const std::uint64_t> right) noexcept
{
    if (left.empty() || right.empty()) {
        return false;
    }
    const auto less = std::less<const std::uint64_t*> { };
    const auto* const left_end = left.data() + left.size();
    const auto* const right_end = right.data() + right.size();
    return less(left.data(), right_end) && less(right.data(), left_end);
}

/// Check the immutable wide-plane shape before the wrapper receives a trusted
/// entry. Mutable slot contents are checked again by the wrapper after each
/// activation; this predicate certifies only its stable allocation and map.
[[nodiscard]] inline bool activation_wide_backing_matches(
        const fsim_jit_runtime_instance_v2& runtime,
        const std::span<const std::uint32_t> signal_widths,
        const std::span<const fsim::runtime::simir::ValueKind> signal_value_kinds,
        const std::span<const std::uint32_t> direct_update_signals,
        const RegionKernelActivationBacking& backing) noexcept
{
    constexpr auto max_u32 = std::numeric_limits<std::uint32_t>::max();
    constexpr auto no_wide_offset = std::numeric_limits<std::size_t>::max();
    const auto fits_u32 = [](const std::size_t value) {
        return value <= std::numeric_limits<std::uint32_t>::max();
    };

    if (!fits_u32(signal_widths.size())
        || backing.signal_widths.size() != signal_widths.size()
        || !std::equal(backing.signal_widths.begin(),
            backing.signal_widths.end(), signal_widths.begin())
        || backing.signal_value_kinds.size() != signal_value_kinds.size()
        || !std::equal(backing.signal_value_kinds.begin(),
            backing.signal_value_kinds.end(), signal_value_kinds.begin())
        || signal_widths.size() != signal_value_kinds.size()
        || backing.register_value_kinds.size() != backing.register_widths.size()
        || backing.register_values_persistent.size() != backing.register_widths.size()
        || backing.register_export_mask.size() != backing.register_widths.size()
            || runtime.direct_wide_signal_aval != backing.direct_wide_signal_aval.data()
            || runtime.direct_wide_signal_bval != backing.direct_wide_signal_bval.data()
            || runtime.direct_wide_signal_offsets != backing.direct_wide_signal_offsets.data()
            || runtime.direct_wide_signal_offset_count != backing.direct_wide_signal_offsets.size()
            || runtime.direct_wide_signal_logic9_plane2
                != backing.direct_wide_signal_logic9_plane2.data()
            || runtime.direct_wide_signal_logic9_plane3
                != backing.direct_wide_signal_logic9_plane3.data()
            || runtime.direct_signal_logic9_plane0
                != backing.direct_signal_logic9_plane0.data()
            || runtime.direct_signal_logic9_plane1
                != backing.direct_signal_logic9_plane1.data()
            || runtime.direct_signal_logic9_plane2
                != backing.direct_signal_logic9_plane2.data()
            || runtime.direct_signal_logic9_plane3
                != backing.direct_signal_logic9_plane3.data()
            || backing.direct_update_slots.size() != direct_update_signals.size()
            || backing.direct_update_wide_word_offsets.size() != direct_update_signals.size()
            || runtime.direct_update_slots != backing.direct_update_slots.data()
            || runtime.direct_update_slot_count != direct_update_signals.size()) {
        return false;
    }

    bool has_wide_signal { };
    bool has_narrow_logic9_signal { };
    bool has_wide_logic9_signal { };
    bool has_logic9_register { };
    for (std::size_t signal = 0U; signal < signal_widths.size(); ++signal) {
        const auto width = signal_widths[signal];
        const auto words = activation_words_for_width(width);
        if (width == 0U || words == 0U) {
            return false;
        }
        const auto kind = signal_value_kinds[signal];
        if (kind != fsim::runtime::simir::ValueKind::logic4
                && kind != fsim::runtime::simir::ValueKind::logic9) {
            return false;
        }
        has_wide_signal = has_wide_signal || width > 64U;
        has_narrow_logic9_signal = has_narrow_logic9_signal
                || (width <= 64U && kind == fsim::runtime::simir::ValueKind::logic9);
        has_wide_logic9_signal = has_wide_logic9_signal
                || (width > 64U && kind == fsim::runtime::simir::ValueKind::logic9);
    }

    for (std::size_t reg = 0U; reg < backing.register_widths.size(); ++reg) {
        const auto kind = backing.register_value_kinds[reg];
        const auto export_value = backing.register_export_mask[reg];
        const auto persistent = backing.register_values_persistent[reg];
        if ((kind != fsim::runtime::simir::ValueKind::logic4
                    && kind != fsim::runtime::simir::ValueKind::logic9)
                || export_value > 1U || persistent > 1U
                || (export_value != 0U
                    && (backing.register_widths[reg] == 0U || persistent == 0U))) {
            return false;
        }
        has_logic9_register = has_logic9_register
                || kind == fsim::runtime::simir::ValueKind::logic9;
    }

    if (!has_wide_signal) {
        if (!backing.direct_wide_signal_offsets.empty() || !backing.direct_wide_signal_aval.empty()
                || !backing.direct_wide_signal_bval.empty()
                || runtime.direct_wide_word_count != 0U) {
            return false;
        }
    } else if (backing.direct_wide_signal_offsets.size() != signal_widths.size()) {
        return false;
    }

    std::uint64_t signal_word_count { };
    for (std::size_t signal = 0U; signal < signal_widths.size(); ++signal) {
        const auto width = signal_widths[signal];
        const auto words = activation_words_for_width(width);
        if (signal_word_count > max_u32 || words > max_u32 - signal_word_count
                || (has_wide_signal
                        && backing.direct_wide_signal_offsets[signal] != signal_word_count)) {
            return false;
        }
        signal_word_count += words;
    }
    if (has_wide_signal
            && (runtime.direct_wide_word_count != signal_word_count
                    || backing.direct_wide_signal_aval.size() != signal_word_count
                    || backing.direct_wide_signal_bval.size() != signal_word_count
                    || backing.direct_wide_signal_aval.data() == nullptr
                    || backing.direct_wide_signal_bval.data() == nullptr)) {
        return false;
    }

    if (has_narrow_logic9_signal) {
        const auto matches = [&](const std::span<const std::uint64_t> plane) {
            return plane.size() == signal_widths.size() && !plane.empty();
        };
        if (!matches(backing.direct_signal_logic9_plane0)
                || !matches(backing.direct_signal_logic9_plane1)
                || !matches(backing.direct_signal_logic9_plane2)
                || !matches(backing.direct_signal_logic9_plane3)) {
            return false;
        }
    } else if (!backing.direct_signal_logic9_plane0.empty()
            || !backing.direct_signal_logic9_plane1.empty()
            || !backing.direct_signal_logic9_plane2.empty()
            || !backing.direct_signal_logic9_plane3.empty()) {
        return false;
    }
    if (has_wide_logic9_signal) {
        if (backing.direct_wide_signal_logic9_plane2.size()
                    != signal_word_count
                || backing.direct_wide_signal_logic9_plane3.size()
                    != signal_word_count) {
            return false;
        }
    } else if (!backing.direct_wide_signal_logic9_plane2.empty()
            || !backing.direct_wide_signal_logic9_plane3.empty()) {
        return false;
    }

    std::size_t update_word_count { };
    for (std::size_t slot_index = 0U; slot_index < direct_update_signals.size(); ++slot_index) {
        const auto signal = direct_update_signals[slot_index];
        if (signal >= signal_widths.size()) {
            return false;
        }
        const auto width = signal_widths[signal];
        const auto words = activation_words_for_width(width);
        const auto& slot = backing.direct_update_slots[slot_index];
        if (width == 0U || words == 0U
                || signal_value_kinds[signal]
                    != fsim::runtime::simir::ValueKind::logic4
                || slot.width != width || slot.active != 0U
                || slot.reserved != 0U || slot.aval != 0U || slot.bval != 0U
                || slot.logic9_plane2 != 0U || slot.logic9_plane3 != 0U || slot.mask != 0U) {
            return false;
        }
        if (width <= 64U) {
            if (backing.direct_update_wide_word_offsets[slot_index] != no_wide_offset
                    || slot.word_count != 1U || slot.wide_aval != nullptr
                    || slot.wide_bval != nullptr || slot.wide_mask != nullptr) {
                return false;
            }
            continue;
        }
        if (words > std::numeric_limits<std::size_t>::max() - update_word_count
                || backing.direct_update_wide_word_offsets[slot_index] != update_word_count
                || slot.word_count != words || slot.wide_aval == nullptr
                || slot.wide_bval == nullptr || slot.wide_mask == nullptr) {
            return false;
        }
        update_word_count += words;
    }

    if (backing.direct_update_wide_aval.size() != update_word_count
            || backing.direct_update_wide_bval.size() != update_word_count
            || backing.direct_update_wide_mask.size() != update_word_count
            || (update_word_count != 0U
                    && (backing.direct_update_wide_aval.data() == nullptr
                            || backing.direct_update_wide_bval.data() == nullptr
                            || backing.direct_update_wide_mask.data() == nullptr))) {
        return false;
    }
    for (std::size_t slot_index = 0U; slot_index < direct_update_signals.size(); ++slot_index) {
        const auto signal = direct_update_signals[slot_index];
        const auto width = signal_widths[signal];
        if (width <= 64U) {
            continue;
        }
        const auto offset = backing.direct_update_wide_word_offsets[slot_index];
        const auto words = activation_words_for_width(width);
        if (offset > update_word_count || words > update_word_count - offset) {
            return false;
        }
        const auto& slot = backing.direct_update_slots[slot_index];
        if (slot.wide_aval != backing.direct_update_wide_aval.data() + offset
                || slot.wide_bval != backing.direct_update_wide_bval.data() + offset
                || slot.wide_mask != backing.direct_update_wide_mask.data() + offset) {
            return false;
        }
    }

    const auto has_logic9_frame = has_logic9_register;
    if (has_logic9_frame) {
        if (backing.register_logic9_plane2.size() != backing.register_aval.size()
                || backing.register_logic9_plane3.size() != backing.register_aval.size()) {
            return false;
        }
    } else if (!backing.register_logic9_plane2.empty()
            || !backing.register_logic9_plane3.empty()) {
        return false;
    }

    const std::array<std::span<const std::uint64_t>, 17U> word_planes {
        std::span<const std::uint64_t> { backing.direct_signal_aval },
        std::span<const std::uint64_t> { backing.direct_signal_bval },
        std::span<const std::uint64_t> {
            backing.direct_signal_logic9_plane0 },
        std::span<const std::uint64_t> {
            backing.direct_signal_logic9_plane1 },
        std::span<const std::uint64_t> {
            backing.direct_signal_logic9_plane2 },
        std::span<const std::uint64_t> {
            backing.direct_signal_logic9_plane3 },
        std::span<const std::uint64_t> { backing.direct_wide_signal_aval },
        std::span<const std::uint64_t> { backing.direct_wide_signal_bval },
        std::span<const std::uint64_t> {
            backing.direct_wide_signal_logic9_plane2 },
        std::span<const std::uint64_t> {
            backing.direct_wide_signal_logic9_plane3 },
        std::span<const std::uint64_t> { backing.direct_update_wide_aval },
        std::span<const std::uint64_t> { backing.direct_update_wide_bval },
        std::span<const std::uint64_t> { backing.direct_update_wide_mask },
        std::span<const std::uint64_t> { backing.register_aval },
        std::span<const std::uint64_t> { backing.register_bval },
        std::span<const std::uint64_t> { backing.register_logic9_plane2 },
        std::span<const std::uint64_t> { backing.register_logic9_plane3 },
    };
    for (std::size_t first = 0U; first < word_planes.size(); ++first) {
        for (std::size_t second = first + 1U; second < word_planes.size(); ++second) {
            if (activation_word_spans_overlap(word_planes[first], word_planes[second])) {
                return false;
            }
        }
    }
    return true;
}

/// Non-public proof minted after one persistent region wrapper validates its
/// fixed native entry and owned ABI/storage descriptors. The owner retains
/// the LlvmJit, runtime, frame, result, and backing vectors for at least the
/// certificate lifetime and does not resize those vectors after binding.
class RegionKernelActivationCertificate final {
public:
    RegionKernelActivationCertificate() noexcept = default;
    RegionKernelActivationCertificate(RegionKernelActivationCertificate&& other) noexcept
        : owner_(std::exchange(other.owner_, nullptr))
        , entry_(std::exchange(other.entry_, nullptr))
        , function_(std::exchange(other.function_, nullptr))
        , process_(std::exchange(other.process_, JitProcessBinding { }))
        , runtime_address_(std::exchange(other.runtime_address_, nullptr))
        , frame_address_(std::exchange(other.frame_address_, nullptr))
        , result_address_(std::exchange(other.result_address_, nullptr))
    {
    }

    RegionKernelActivationCertificate& operator=(RegionKernelActivationCertificate&& other) noexcept
    {
        if (this != &other) {
            owner_ = std::exchange(other.owner_, nullptr);
            entry_ = std::exchange(other.entry_, nullptr);
            function_ = std::exchange(other.function_, nullptr);
            process_ = std::exchange(other.process_, JitProcessBinding { });
            runtime_address_ = std::exchange(other.runtime_address_, nullptr);
            frame_address_ = std::exchange(other.frame_address_, nullptr);
            result_address_ = std::exchange(other.result_address_, nullptr);
        }
        return *this;
    }
    RegionKernelActivationCertificate(const RegionKernelActivationCertificate&) = delete;
    RegionKernelActivationCertificate& operator=(const RegionKernelActivationCertificate&) = delete;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return owner_ != nullptr && entry_ != nullptr && function_ != nullptr
                && static_cast<bool>(process_) && runtime_address_ != nullptr
                && frame_address_ != nullptr && result_address_ != nullptr;
    }

private:
    friend class fsim::compiler::LlvmJit;

    const void* owner_ { };
    const void* entry_ { };
    fsim_jit_process_v2* function_ { };
    JitProcessBinding process_ { };
    const fsim_jit_runtime_instance_v2* runtime_address_ { };
    const fsim_jit_frame_v2* frame_address_ { };
    const fsim_jit_resume_result_v2* result_address_ { };
};

using NativeRegionPreparedOutputProcess = std::uint32_t(
    const fsim_jit_runtime_instance_v2*, fsim_jit_frame_v2*,
    fsim_jit_resume_result_v2*,
    const runtime::simir::RegionPreparedOutputBatchV1*);
using NativeRegionPreparedOutputSuccessorProcess = std::uint32_t(
    const fsim_jit_runtime_instance_v2*, fsim_jit_frame_v2*,
    fsim_jit_resume_result_v2*,
    const runtime::simir::RegionPreparedOutputBatchV1*,
    runtime::simir::RegionPreparedOutputSuccessorMasksV1*);
using NativeRegionDirectReadyOutputProcess = std::uint32_t(
    const fsim_jit_runtime_instance_v2*, fsim_jit_frame_v2*,
    fsim_jit_resume_result_v2*,
    const runtime::simir::RegionDirectReadyWindowV1*,
    const runtime::simir::RegionPreparedOutputBatchV1*);
using NativeRegionDirectReadySuccessorOutputProcess = std::uint32_t(
    const fsim_jit_runtime_instance_v2*, fsim_jit_frame_v2*,
    fsim_jit_resume_result_v2*,
    const runtime::simir::RegionDirectReadyWindowV1*,
    const runtime::simir::RegionPreparedOutputBatchV1*,
    runtime::simir::RegionPreparedOutputSuccessorMasksV1*);

/// Separate certificate for the typed private four-argument entry. Ordinary
/// process bindings continue to name only their three-argument v2 function.
class RegionPreparedOutputCertificate final {
public:
    RegionPreparedOutputCertificate() noexcept = default;
    RegionPreparedOutputCertificate(RegionPreparedOutputCertificate&& other) noexcept
        : owner_(std::exchange(other.owner_, nullptr))
        , entry_(std::exchange(other.entry_, nullptr))
        , function_(std::exchange(other.function_, nullptr))
        , successor_function_(
              std::exchange(other.successor_function_, nullptr))
        , process_(std::exchange(other.process_, JitProcessBinding { }))
        , runtime_address_(std::exchange(other.runtime_address_, nullptr))
        , frame_address_(std::exchange(other.frame_address_, nullptr))
        , result_address_(std::exchange(other.result_address_, nullptr))
    {
    }

    RegionPreparedOutputCertificate& operator=(
        RegionPreparedOutputCertificate&& other) noexcept
    {
        if (this != &other) {
            owner_ = std::exchange(other.owner_, nullptr);
            entry_ = std::exchange(other.entry_, nullptr);
            function_ = std::exchange(other.function_, nullptr);
            successor_function_ = std::exchange(
                other.successor_function_, nullptr);
            process_ = std::exchange(other.process_, JitProcessBinding { });
            runtime_address_ = std::exchange(other.runtime_address_, nullptr);
            frame_address_ = std::exchange(other.frame_address_, nullptr);
            result_address_ = std::exchange(other.result_address_, nullptr);
        }
        return *this;
    }
    RegionPreparedOutputCertificate(const RegionPreparedOutputCertificate&) = delete;
    RegionPreparedOutputCertificate& operator=(
        const RegionPreparedOutputCertificate&) = delete;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return owner_ != nullptr && entry_ != nullptr && function_ != nullptr
            && static_cast<bool>(process_) && runtime_address_ != nullptr
            && frame_address_ != nullptr && result_address_ != nullptr;
    }

private:
    friend class fsim::compiler::LlvmJit;

    const void* owner_ { };
    const void* entry_ { };
    NativeRegionPreparedOutputProcess* function_ { };
    NativeRegionPreparedOutputSuccessorProcess* successor_function_ { };
    JitProcessBinding process_ { };
    const fsim_jit_runtime_instance_v2* runtime_address_ { };
    const fsim_jit_frame_v2* frame_address_ { };
    const fsim_jit_resume_result_v2* result_address_ { };
};

/// Separate certificate for the private ready-window wrapper. Its distinct
/// function type and symbol prevent ordinary prepared-output callers from
/// invoking the input-copying entry without descriptor validation.
class RegionDirectReadyOutputCertificate final {
public:
    RegionDirectReadyOutputCertificate() noexcept = default;
    RegionDirectReadyOutputCertificate(
        RegionDirectReadyOutputCertificate&& other) noexcept
        : owner_(std::exchange(other.owner_, nullptr))
        , entry_(std::exchange(other.entry_, nullptr))
        , function_(std::exchange(other.function_, nullptr))
        , successor_function_(
            std::exchange(other.successor_function_, nullptr))
        , process_(std::exchange(other.process_, JitProcessBinding { }))
        , runtime_address_(std::exchange(other.runtime_address_, nullptr))
        , frame_address_(std::exchange(other.frame_address_, nullptr))
        , result_address_(std::exchange(other.result_address_, nullptr))
    {
    }

    RegionDirectReadyOutputCertificate& operator=(
        RegionDirectReadyOutputCertificate&& other) noexcept
    {
        if (this != &other) {
            owner_ = std::exchange(other.owner_, nullptr);
            entry_ = std::exchange(other.entry_, nullptr);
            function_ = std::exchange(other.function_, nullptr);
            successor_function_ = std::exchange(
                other.successor_function_, nullptr);
            process_ = std::exchange(other.process_, JitProcessBinding { });
            runtime_address_ = std::exchange(other.runtime_address_, nullptr);
            frame_address_ = std::exchange(other.frame_address_, nullptr);
            result_address_ = std::exchange(other.result_address_, nullptr);
        }
        return *this;
    }
    RegionDirectReadyOutputCertificate(
        const RegionDirectReadyOutputCertificate&) = delete;
    RegionDirectReadyOutputCertificate& operator=(
        const RegionDirectReadyOutputCertificate&) = delete;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return owner_ != nullptr && entry_ != nullptr && function_ != nullptr
            && static_cast<bool>(process_) && runtime_address_ != nullptr
            && frame_address_ != nullptr && result_address_ != nullptr;
    }

    [[nodiscard]] bool has_successor_masks() const noexcept
    {
        return successor_function_ != nullptr;
    }

private:
    friend class fsim::compiler::LlvmJit;

    const void* owner_ { };
    const void* entry_ { };
    NativeRegionDirectReadyOutputProcess* function_ { };
    NativeRegionDirectReadySuccessorOutputProcess* successor_function_ { };
    JitProcessBinding process_ { };
    const fsim_jit_runtime_instance_v2* runtime_address_ { };
    const fsim_jit_frame_v2* frame_address_ { };
    const fsim_jit_resume_result_v2* result_address_ { };
};

/// Nonblocking guard for executor-owned activation scratch.
class RegionKernelInvocationGuard final {
public:
    explicit RegionKernelInvocationGuard(std::atomic_flag& in_use) noexcept
        : in_use_(&in_use)
        , acquired_(!in_use.test_and_set(std::memory_order_acquire))
    {
    }

    ~RegionKernelInvocationGuard()
    {
        if (acquired_) {
            in_use_->clear(std::memory_order_release);
        }
    }

    RegionKernelInvocationGuard(const RegionKernelInvocationGuard&) = delete;
    RegionKernelInvocationGuard& operator=(const RegionKernelInvocationGuard&) = delete;

    [[nodiscard]] explicit operator bool() const noexcept { return acquired_; }

private:
    std::atomic_flag* in_use_ { };
    bool acquired_ { };
};

} // namespace fsim::compiler::llvm_detail
