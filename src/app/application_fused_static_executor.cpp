// SPDX-License-Identifier: Apache-2.0
#include "application_fused_static_executor.hpp"

#include <algorithm>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <string>
#include <vector>

namespace fsim::app {
namespace {

class LlvmFusedStaticExecutor final
    : public runtime::simir::FusedStaticCohortExecutor
    , public runtime::simir::FusedMaskedRegionExecutor {
public:
    LlvmFusedStaticExecutor(
        compiler::LlvmJit& jit,
        const compiler::JitProcessHandle handle,
        const std::span<const runtime::simir::SignalId> actual_signals,
        const std::span<const std::uint32_t> canonical_widths,
        const std::span<const runtime::simir::SignalId> output_order,
        const std::span<const runtime::simir::ValueKind> canonical_kinds,
        const bool projected)
        : jit_(jit)
        , binding_(jit.bind(handle))
        , layout_(jit.frame_layout(binding_))
        , actual_signals_(actual_signals.begin(), actual_signals.end())
        , canonical_widths_(canonical_widths.begin(), canonical_widths.end())
        , canonical_kinds_(canonical_kinds.begin(), canonical_kinds.end())
        , projected_(projected)
    {
        if ((!projected_ && layout_.direct_update_signals.empty())
            || actual_signals_.size() != canonical_widths_.size()
            || (!canonical_kinds_.empty()
                && canonical_kinds_.size() != actual_signals_.size())
            || (layout_.uses_logic9
                && canonical_kinds_.size() != actual_signals_.size())) {
            throw std::logic_error {
                "fused native kernel has no direct aggregate slots"
            };
        }
        register_aval_.resize(layout_.register_word_count);
        register_bval_.resize(layout_.register_word_count);
        register_initialized_.resize(layout_.register_count);
        if (layout_.uses_logic9) {
            register_logic9_plane2_.resize(layout_.register_word_count);
            register_logic9_plane3_.resize(layout_.register_word_count);
        }
        jit_.initialize_frame(binding_, frame_, register_aval_,
            register_bval_, register_initialized_,
            register_logic9_plane2_, register_logic9_plane3_);

        direct_read_signals_.reserve(layout_.direct_read_signals.size());
        for (const auto signal : layout_.direct_read_signals) {
            if (signal >= actual_signals_.size()) {
                throw std::logic_error {
                    "fused native read has no elaborated signal binding"
                };
            }
            direct_read_signals_.push_back(actual_signals_[signal]);
        }
        slots_.resize(layout_.direct_update_signals.size());
        active_words_.resize((slots_.size() + 63U) / 64U);
        std::size_t wide_words { };
        for (const auto signal : layout_.direct_update_signals) {
            if (signal >= canonical_widths_.size()
                || canonical_widths_[signal] == 0U) {
                throw std::logic_error {
                    "fused native update has no elaborated width"
                };
            }
            if (canonical_widths_[signal] > 64U) {
                wide_words += (canonical_widths_[signal] + 63U) / 64U;
            }
        }
        wide_aval_.resize(wide_words);
        wide_bval_.resize(wide_words);
        wide_mask_.resize(wide_words);
        views_.reserve(slots_.size());
        std::size_t wide_offset { };
        for (std::size_t index = 0U; index < slots_.size(); ++index) {
            const auto signal = layout_.direct_update_signals[index];
            auto& slot = slots_[index];
            slot.width = canonical_widths_[signal];
            slot.word_count = (slot.width + 63U) / 64U;
            if (slot.width > 64U) {
                slot.wide_aval = wide_aval_.data() + wide_offset;
                slot.wide_bval = wide_bval_.data() + wide_offset;
                slot.wide_mask = wide_mask_.data() + wide_offset;
                wide_offset += slot.word_count;
            }
            const bool wide = slot.width > 64U;
            views_.push_back(runtime::simir::ProcessUpdateSlotView {
                actual_signals_[signal], slot.width, slot.word_count,
                &slot.active,
                wide ? slot.wide_aval : &slot.aval,
                wide ? slot.wide_bval : &slot.bval,
                wide ? slot.wide_mask : &slot.mask
            });
        }
        if (!projected_ && views_.size() != output_order.size()) {
            throw std::logic_error {
                "fused native output count differs from graph plan"
            };
        }
        if (!projected_) {
            auto ordered = std::vector<runtime::simir::ProcessUpdateSlotView> { };
            ordered.reserve(views_.size());
            for (const auto signal : output_order) {
                const auto found = std::ranges::find(
                    views_, signal,
                    &runtime::simir::ProcessUpdateSlotView::signal);
                if (found == views_.end()
                    || std::ranges::find(ordered, signal,
                        &runtime::simir::ProcessUpdateSlotView::signal)
                        != ordered.end()) {
                    throw std::logic_error {
                        "fused native output binding differs from graph plan"
                    };
                }
                ordered.push_back(*found);
            }
            views_ = std::move(ordered);
        }
        if (projected_) {
            output_order_.assign(output_order.begin(), output_order.end());
            projected_writes_.resize(output_order_.size());
            projected_selected_.reserve(output_order_.size());
            projected_seen_.resize(output_order_.size());
            projected_index_by_canonical_.resize(actual_signals_.size(),
                std::numeric_limits<std::size_t>::max());
            for (std::size_t index = 0U; index < output_order_.size(); ++index) {
                bool found { };
                for (std::size_t canonical = 0U;
                     canonical < actual_signals_.size(); ++canonical) {
                    if (actual_signals_[canonical] == output_order_[index]) {
                        if (found) {
                            throw std::logic_error {
                                "fused projected output has multiple canonical signals"
                            };
                        }
                        projected_index_by_canonical_[canonical] = index;
                        projected_writes_[index].signal = output_order_[index];
                        found = true;
                    }
                }
                if (!found) {
                    throw std::logic_error {
                        "fused projected output lacks canonical signal"
                    };
                }
            }
        }
    }

    std::optional<runtime::simir::FusedStaticCohortResume>
    resume(const runtime::simir::ProcessCohortNativeContext& context) override
    {
        return resume_impl(context, { });
    }

    std::optional<runtime::simir::FusedStaticCohortResume>
    resume(const runtime::simir::ProcessCohortNativeContext& context,
        const std::span<const std::uint64_t> activation_words) override
    {
        if (activation_words.empty()
            || activation_words.size()
                > std::numeric_limits<std::uint32_t>::max()) {
            return std::nullopt;
        }
        return resume_impl(context, activation_words);
    }

private:
    std::optional<runtime::simir::FusedStaticCohortResume>
    resume_impl(const runtime::simir::ProcessCohortNativeContext& context,
        const std::span<const std::uint64_t> activation_words)
    {
        if (projected_ && !activation_words.empty()
            && activation_words.size()
                != (output_order_.size() + 63U) / 64U) {
            return std::nullopt;
        }
        if (!context.supports_direct_word_updates
            || context.execution_points_enabled
            || context.signal_aval.size() != context.signal_bval.size()
            || context.signal_aval.size()
                > std::numeric_limits<std::uint32_t>::max()
            || context.wide_signal_aval.size()
                != context.wide_signal_bval.size()
            || context.wide_signal_aval.size()
                > std::numeric_limits<std::uint32_t>::max()
            || context.wide_signal_offsets.size()
                > std::numeric_limits<std::uint32_t>::max()) {
            return std::nullopt;
        }
        for (std::size_t index = 0U;
             index < layout_.direct_read_signals.size(); ++index) {
            const auto canonical = layout_.direct_read_signals[index];
            const auto actual = direct_read_signals_[index];
            if (actual >= context.signal_aval.size()) {
                return std::nullopt;
            }
            const bool logic9 = !canonical_kinds_.empty()
                && canonical_kinds_[canonical]
                    == runtime::simir::ValueKind::logic9;
            if (logic9 && canonical_widths_[canonical] <= 64U
                && (actual >= context.signal_logic9_plane0.size()
                    || actual >= context.signal_logic9_plane1.size()
                    || actual >= context.signal_logic9_plane2.size()
                    || actual >= context.signal_logic9_plane3.size())) {
                return std::nullopt;
            }
            if (canonical_widths_[canonical] > 64U
                && (actual >= context.wide_signal_offsets.size()
                    || context.wide_signal_offsets[actual]
                        > context.wide_signal_aval.size()
                    || (canonical_widths_[canonical] + 63U) / 64U
                        > context.wide_signal_aval.size()
                            - context.wide_signal_offsets[actual])) {
                return std::nullopt;
            }
            if (logic9 && canonical_widths_[canonical] > 64U) {
                const auto offset = context.wide_signal_offsets[actual];
                const auto words = (canonical_widths_[canonical] + 63U) / 64U;
                if (offset > context.wide_signal_logic9_plane2.size()
                    || words > context.wide_signal_logic9_plane2.size()
                        - offset
                    || offset > context.wide_signal_logic9_plane3.size()
                    || words > context.wide_signal_logic9_plane3.size()
                        - offset) {
                    return std::nullopt;
                }
            }
        }
        if (layout_.uses_logic9) {
            for (std::size_t canonical = 0U;
                 canonical < canonical_widths_.size(); ++canonical) {
                if (canonical_kinds_[canonical]
                        != runtime::simir::ValueKind::logic9
                    || canonical_widths_[canonical] <= 64U) {
                    continue;
                }
                const auto actual = actual_signals_[canonical];
                const auto words = (canonical_widths_[canonical] + 63U) / 64U;
                if (actual >= context.wide_signal_offsets.size()) {
                    return std::nullopt;
                }
                const auto offset = context.wide_signal_offsets[actual];
                if (offset > context.wide_signal_aval.size()
                    || words > context.wide_signal_aval.size() - offset
                    || offset > context.wide_signal_bval.size()
                    || words > context.wide_signal_bval.size() - offset
                    || offset > context.wide_signal_logic9_plane2.size()
                    || words > context.wide_signal_logic9_plane2.size()
                        - offset
                    || offset > context.wide_signal_logic9_plane3.size()
                    || words > context.wide_signal_logic9_plane3.size()
                        - offset) {
                    return std::nullopt;
                }
            }
        }
        for (auto& slot : slots_) {
            slot.active = 0U;
            slot.mask = 0U;
        }
        std::ranges::fill(wide_mask_, UINT64_C(0));
        std::ranges::fill(active_words_, UINT64_C(0));
        std::ranges::fill(projected_seen_, std::uint8_t { 0U });
        projected_selected_.clear();
        unexpected_callback_ = false;
        unexpected_callback_name_ = nullptr;

        fsim_jit_runtime_v1 runtime { };
        runtime.abi_version = FSIM_JIT_RUNTIME_ABI_VERSION_V1;
        runtime.struct_size = sizeof(runtime);
        runtime.context = this;
        runtime.read_signal = [](void* owner, std::uint32_t,
                                  std::uint64_t* bval) {
            auto& self = *static_cast<LlvmFusedStaticExecutor*>(owner);
            self.unexpected_callback_ = true;
            self.unexpected_callback_name_ = "read_signal";
            *bval = 0U;
            return UINT64_C(0);
        };
        runtime.write_signal = [](void* owner, std::uint32_t,
                                   std::uint64_t, std::uint64_t) {
            auto& self = *static_cast<LlvmFusedStaticExecutor*>(owner);
            self.unexpected_callback_ = true;
            self.unexpected_callback_name_ = "write_signal_or_update";
        };
        runtime.assert_failed = [](void* owner, std::uint32_t,
                                    std::uint32_t, const char*, std::uint64_t) {
            auto& self = *static_cast<LlvmFusedStaticExecutor*>(owner);
            self.unexpected_callback_ = true;
            self.unexpected_callback_name_ = "assert_failed";
        };
        runtime.write_update = runtime.write_signal;
        runtime.write_update_slice = [](void* owner, std::uint32_t,
                                        std::uint32_t, std::uint32_t,
                                        std::uint64_t, std::uint64_t) {
            auto& self = *static_cast<LlvmFusedStaticExecutor*>(owner);
            self.unexpected_callback_ = true;
            self.unexpected_callback_name_ = "write_update_slice";
        };
        runtime.write_projected = [](void* owner,
                                      const std::uint32_t signal,
                                      const std::uint64_t aval,
                                      const std::uint64_t bval,
                                      const std::uint64_t delay,
                                      const std::uint64_t rejection,
                                      const std::uint32_t mode) {
            auto& self = *static_cast<LlvmFusedStaticExecutor*>(owner);
            try {
                if (delay != 0U || rejection != 0U
                    || mode != FSIM_JIT_PROJECTED_INERTIAL
                    || signal >= self.canonical_widths_.size()
                    || self.canonical_widths_[signal] > 64U) {
                    throw std::logic_error { "unsupported fused projected callback" };
                }
                self.record_projected(signal,
                    runtime::PackedLogic4::from_aval_bval(
                        self.canonical_widths_[signal], aval, bval));
            } catch (...) {
                self.unexpected_callback_ = true;
                self.unexpected_callback_name_ = "write_projected";
            }
        };
        runtime.read_signal_packed = [](void* owner,
                                        const std::uint32_t canonical,
                                        const std::uint32_t width,
                                        std::uint64_t* aval,
                                        std::uint64_t* bval,
                                        std::uint64_t* plane2,
                                        std::uint64_t* plane3) {
            auto& self = *static_cast<LlvmFusedStaticExecutor*>(owner);
            const auto* current = self.active_context_;
            const bool logic9 = canonical < self.canonical_kinds_.size()
                && self.canonical_kinds_[canonical]
                    == runtime::simir::ValueKind::logic9;
            if (current == nullptr
                || canonical >= self.actual_signals_.size()
                || width != self.canonical_widths_[canonical]
                || width <= 64U || aval == nullptr || bval == nullptr
                || (plane2 != nullptr) != logic9
                || (plane3 != nullptr) != logic9) {
                self.unexpected_callback_ = true;
                self.unexpected_callback_name_ = "read_signal_packed";
                return UINT32_C(1);
            }
            const auto actual = self.actual_signals_[canonical];
            const auto offset = current->wide_signal_offsets[actual];
            const auto words = (width + 63U) / 64U;
            std::ranges::copy_n(current->wide_signal_aval.data() + offset,
                words, aval);
            std::ranges::copy_n(current->wide_signal_bval.data() + offset,
                words, bval);
            if (logic9) {
                std::ranges::copy_n(
                    current->wide_signal_logic9_plane2.data() + offset,
                    words, plane2);
                std::ranges::copy_n(
                    current->wide_signal_logic9_plane3.data() + offset,
                    words, plane3);
            }
            return UINT32_C(0);
        };
        runtime.write_signal_packed = [](void* owner,
                                         const std::uint32_t signal,
                                         const std::uint32_t offset,
                                         const std::uint32_t width,
                                         const std::uint32_t mode,
                                         const std::uint64_t delay,
                                         const std::uint64_t* aval,
                                         const std::uint64_t* bval,
                                         const std::uint64_t* plane2,
                                         const std::uint64_t* plane3) {
            auto& self = *static_cast<LlvmFusedStaticExecutor*>(owner);
            try {
                const bool logic9 = signal < self.canonical_kinds_.size()
                    && self.canonical_kinds_[signal]
                        == runtime::simir::ValueKind::logic9;
                if (signal >= self.canonical_widths_.size()
                    || width != self.canonical_widths_[signal]
                    || offset != 0U || delay != 0U
                    || mode != FSIM_JIT_PACKED_SIGNAL_WRITE_UPDATE
                    || aval == nullptr || bval == nullptr
                    || !self.projected_
                    || (plane2 != nullptr) != logic9
                    || (plane3 != nullptr) != logic9) {
                    throw std::logic_error { "unsupported fused packed write" };
                }
                const auto words = (width + 63U) / 64U;
                self.record_projected(signal,
                    logic9
                        ? runtime::PackedLogic4::from_logic9_word_planes(
                            width, { aval, words }, { bval, words },
                            { plane2, words }, { plane3, words })
                        : runtime::PackedLogic4::from_word_planes(width,
                            { aval, words }, { bval, words }));
                return UINT32_C(0);
            } catch (...) {
                self.unexpected_callback_ = true;
                self.unexpected_callback_name_ = "write_signal_packed";
                return UINT32_C(1);
            }
        };
        const auto reject_logic9 = [](void* owner, auto...) {
            auto& self = *static_cast<LlvmFusedStaticExecutor*>(owner);
            self.unexpected_callback_ = true;
            self.unexpected_callback_name_ = "other_logic9";
        };
        runtime.read_signal_logic9 = [](void* owner, std::uint32_t,
                                         fsim_jit_logic9_word_v1* value) {
            auto& self = *static_cast<LlvmFusedStaticExecutor*>(owner);
            self.unexpected_callback_ = true;
            self.unexpected_callback_name_ = "read_signal_logic9";
            if (value != nullptr) {
                *value = { };
            }
        };
        runtime.write_signal_logic9 = reject_logic9;
        runtime.write_update_logic9 = reject_logic9;
        runtime.write_after_logic9 = reject_logic9;
        runtime.write_signal_slice_logic9 = reject_logic9;
        runtime.write_update_slice_logic9 = reject_logic9;
        runtime.write_after_slice_logic9 = reject_logic9;
        runtime.signal_last_value_logic9 = reject_logic9;
        runtime.write_inertial_logic9 = reject_logic9;
        runtime.write_inertial_slice_logic9 = reject_logic9;
        runtime.write_projected_logic9 = [](void* owner,
                                             const std::uint32_t signal,
                                             const fsim_jit_logic9_word_v1* value,
                                             const std::uint64_t delay,
                                             const std::uint64_t rejection,
                                             const std::uint32_t mode) {
            auto& self = *static_cast<LlvmFusedStaticExecutor*>(owner);
            try {
                if (value == nullptr || delay != 0U || rejection != 0U
                    || mode != FSIM_JIT_PROJECTED_INERTIAL
                    || signal >= self.canonical_widths_.size()
                    || self.canonical_widths_[signal] > 64U) {
                    throw std::logic_error {
                        "unsupported fused Logic9 projected callback"
                    };
                }
                self.record_projected(signal,
                    runtime::PackedLogic4::from_logic9_word({
                        self.canonical_widths_[signal],
                        { value->planes[0], value->planes[1],
                            value->planes[2], value->planes[3] }
                    }));
            } catch (...) {
                self.unexpected_callback_ = true;
                self.unexpected_callback_name_ = "write_projected_logic9";
            }
        };
        runtime.write_projected_slice_logic9 = reject_logic9;
        runtime.write_projected_waveform_logic9 = reject_logic9;
        runtime.write_projected_waveform_slice_logic9 = reject_logic9;
        runtime.write_formatted_logic9 = reject_logic9;
        runtime.direct_signal_aval = context.signal_aval.data();
        runtime.direct_signal_bval = context.signal_bval.data();
        runtime.direct_signal_count = static_cast<std::uint32_t>(
            context.signal_aval.size());
        runtime.direct_read_signals = direct_read_signals_.data();
        runtime.direct_read_signal_count = static_cast<std::uint32_t>(
            direct_read_signals_.size());
        runtime.direct_wide_signal_aval = context.wide_signal_aval.data();
        runtime.direct_wide_signal_bval = context.wide_signal_bval.data();
        runtime.direct_wide_signal_offsets
            = context.wide_signal_offsets.data();
        runtime.direct_wide_signal_offset_count
            = static_cast<std::uint32_t>(
                context.wide_signal_offsets.size());
        runtime.direct_wide_word_count = static_cast<std::uint32_t>(
            context.wide_signal_aval.size());
        runtime.direct_signal_logic9_plane0
            = context.signal_logic9_plane0.data();
        runtime.direct_signal_logic9_plane1
            = context.signal_logic9_plane1.data();
        runtime.direct_signal_logic9_plane2
            = context.signal_logic9_plane2.data();
        runtime.direct_signal_logic9_plane3
            = context.signal_logic9_plane3.data();
        runtime.direct_wide_signal_logic9_plane2
            = context.wide_signal_logic9_plane2.data();
        runtime.direct_wide_signal_logic9_plane3
            = context.wide_signal_logic9_plane3.data();
        runtime.direct_update_slots = slots_.data();
        runtime.direct_update_slot_count = static_cast<std::uint32_t>(
            slots_.size());
        runtime.direct_update_active_words = active_words_.data();
        runtime.direct_update_active_word_count
            = static_cast<std::uint32_t>(active_words_.size());
        runtime.static_trigger_mask
            = runtime::simir::Process::full_static_trigger_mask;
        runtime.fused_activation_words = activation_words.data();
        runtime.fused_activation_word_count
            = static_cast<std::uint32_t>(activation_words.size());

        fsim_jit_resume_result_v1 result { };
        result.abi_version = FSIM_JIT_RESUME_RESULT_ABI_VERSION_V1;
        result.struct_size = sizeof(result);
        active_context_ = &context;
        compiler::JitResumeStatus status;
        try {
            status = jit_.resume(binding_, runtime, frame_, result);
        } catch (...) {
            active_context_ = nullptr;
            throw;
        }
        active_context_ = nullptr;
        if (unexpected_callback_
            || status != compiler::JitResumeStatus::wait_sensitivity) {
            throw std::logic_error {
                "fused native kernel left its pure static boundary (status="
                    + std::to_string(static_cast<int>(status))
                    + ", callback="
                    + (unexpected_callback_name_ == nullptr
                        ? std::to_string(unexpected_callback_)
                        : unexpected_callback_name_) + ")"
            };
        }
        if (projected_) {
            for (std::size_t index = 0U; index < slots_.size(); ++index) {
                const auto canonical = layout_.direct_update_signals[index];
                if (canonical_kinds_.empty()
                    || canonical_kinds_[canonical]
                        != runtime::simir::ValueKind::logic9) {
                    continue;
                }
                const auto output = projected_index_by_canonical_[canonical];
                if (!activation_words.empty()
                    && output < output_order_.size()
                    && (activation_words[output / 64U]
                        & (UINT64_C(1) << (output % 64U))) == 0U) {
                    continue;
                }
                if (output >= projected_writes_.size()
                    || slots_[index].width > 64U
                    || slots_[index].mask
                        != (slots_[index].width == 64U
                            ? std::numeric_limits<std::uint64_t>::max()
                            : (UINT64_C(1) << slots_[index].width) - 1U)) {
                    throw std::logic_error {
                        "fused Logic9 projected slot is incomplete"
                    };
                }
                const auto& slot = slots_[index];
                record_projected(canonical,
                    runtime::PackedLogic4::from_logic9_word({
                        slot.width,
                        { slot.aval, slot.bval,
                            slot.logic9_plane2, slot.logic9_plane3 }
                    }));
            }
            for (std::size_t index = 0U; index < projected_seen_.size(); ++index) {
                const bool required = activation_words.empty()
                    || (activation_words[index / 64U]
                        & (UINT64_C(1) << (index % 64U))) != 0U;
                if (projected_seen_[index] != static_cast<std::uint8_t>(required)) {
                    throw std::logic_error {
                        "fused projected kernel returned an invalid sparse output set"
                    };
                }
                if (required && !activation_words.empty()) {
                    projected_selected_.push_back(projected_writes_[index]);
                }
            }
            return runtime::simir::FusedStaticCohortResume {
                {}, activation_words.empty()
                    ? std::span<const runtime::simir::FusedStaticProjectedWrite> {
                        projected_writes_ }
                    : std::span<const runtime::simir::FusedStaticProjectedWrite> {
                        projected_selected_ }
            };
        }
        return runtime::simir::FusedStaticCohortResume { views_, {} };
    }

    void record_projected(const std::uint32_t canonical,
                          runtime::PackedLogic4 value)
    {
        if (!projected_ || canonical >= actual_signals_.size()
            || value.width() != canonical_widths_[canonical]
            || (!canonical_kinds_.empty()
                && value.is_logic9()
                    != (canonical_kinds_[canonical]
                        == runtime::simir::ValueKind::logic9))) {
            throw std::logic_error { "invalid fused projected value" };
        }
        const auto index = projected_index_by_canonical_[canonical];
        if (index >= projected_writes_.size() || projected_seen_[index] != 0U) {
            throw std::logic_error { "duplicate fused projected output" };
        }
        projected_writes_[index].value = std::move(value);
        projected_seen_[index] = 1U;
    }

    compiler::LlvmJit& jit_;
    compiler::JitProcessBinding binding_;
    compiler::JitProcessFrameLayout layout_;
    std::vector<runtime::simir::SignalId> actual_signals_;
    std::vector<std::uint32_t> canonical_widths_;
    std::vector<runtime::simir::ValueKind> canonical_kinds_;
    fsim_jit_frame_v1 frame_ { };
    std::vector<std::uint64_t> register_aval_;
    std::vector<std::uint64_t> register_bval_;
    std::vector<std::uint8_t> register_initialized_;
    std::vector<std::uint64_t> register_logic9_plane2_;
    std::vector<std::uint64_t> register_logic9_plane3_;
    std::vector<std::uint32_t> direct_read_signals_;
    std::vector<fsim_jit_update_slot_v1> slots_;
    std::vector<std::uint64_t> active_words_;
    std::vector<std::uint64_t> wide_aval_;
    std::vector<std::uint64_t> wide_bval_;
    std::vector<std::uint64_t> wide_mask_;
    std::vector<runtime::simir::ProcessUpdateSlotView> views_;
    std::vector<runtime::simir::SignalId> output_order_;
    std::vector<std::size_t> projected_index_by_canonical_;
    std::vector<runtime::simir::FusedStaticProjectedWrite> projected_writes_;
    std::vector<runtime::simir::FusedStaticProjectedWrite> projected_selected_;
    std::vector<std::uint8_t> projected_seen_;
    bool projected_ { };
    bool unexpected_callback_ { };
    const char* unexpected_callback_name_ { };
    const runtime::simir::ProcessCohortNativeContext* active_context_ { };
};

} // namespace

std::unique_ptr<runtime::simir::FusedStaticCohortExecutor>
make_fused_static_executor(
    compiler::LlvmJit& jit,
    const compiler::JitProcessHandle handle,
    const std::span<const runtime::simir::SignalId> actual_signals,
    const std::span<const std::uint32_t> canonical_widths,
    const std::span<const runtime::simir::SignalId> output_order,
    const std::span<const runtime::simir::ValueKind> canonical_kinds,
    const bool projected)
{
    return std::make_unique<LlvmFusedStaticExecutor>(
        jit, handle, actual_signals, canonical_widths, output_order,
        canonical_kinds, projected);
}

std::unique_ptr<runtime::simir::FusedMaskedRegionExecutor>
make_fused_masked_region_executor(
    compiler::LlvmJit& jit,
    const compiler::JitProcessHandle handle,
    const std::span<const runtime::simir::SignalId> actual_signals,
    const std::span<const std::uint32_t> canonical_widths,
    const std::span<const runtime::simir::SignalId> output_order,
    const std::span<const runtime::simir::ValueKind> canonical_kinds,
    const bool projected)
{
    return std::make_unique<LlvmFusedStaticExecutor>(
        jit, handle, actual_signals, canonical_widths, output_order,
        canonical_kinds, projected);
}

} // namespace fsim::app
