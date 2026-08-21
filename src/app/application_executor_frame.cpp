// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"

#include <numeric>

namespace fsim::app::application_detail {

#if defined(FSIM_HAS_LLVM)

namespace {

[[nodiscard]] std::size_t buffered_update_capacity(
    const runtime::simir::Process& process)
{
    return static_cast<std::size_t>(std::ranges::count_if(
        process.operations,
        [](const runtime::simir::Operation& operation) {
            return runtime::simir::operation_holds<
                       runtime::simir::WriteUpdate>(operation)
                || runtime::simir::operation_holds<
                       runtime::simir::WriteUpdateSlice>(operation);
        }));
}

void build_dense_signal_remap(
    const std::shared_ptr<const LlvmProcessExecutor::SignalRemap>& remap,
    std::uint32_t& base,
    std::vector<std::uint32_t>& dense)
{
    if (!remap || remap->empty()) {
        return;
    }
    const auto first = remap->front().first;
    const auto last = remap->back().first;
    const auto range = static_cast<std::uint64_t>(last) - first + 1U;
    constexpr std::uint64_t maximum_dense_entries = 64U;
    if (range > maximum_dense_entries
        || range > remap->size() * 8U) {
        return;
    }
    base = first;
    dense.resize(static_cast<std::size_t>(range));
    std::iota(dense.begin(), dense.end(), first);
    for (const auto& [source, target] : *remap) {
        dense[source - first] = target;
    }
}

} // namespace

LlvmProcessExecutor::LlvmProcessExecutor(
    compiler::LlvmJit& jit,
    const compiler::JitProcessHandle handle,
    const runtime::simir::Process& process,
    std::span<const std::uint32_t> signal_widths,
    std::span<const runtime::simir::ValueKind> signal_value_kinds,
    std::span<const runtime::simir::ResolutionKind> signal_resolutions,
    std::shared_ptr<const SignalRemap> signal_remap,
    const std::optional<runtime::simir::ProcessId> generated_process)
    : jit_(jit)
    , binding_(jit.bind(handle))
    , process_(process)
    , signal_widths_(signal_widths)
    , signal_value_kinds_(signal_value_kinds)
    , signal_resolutions_(signal_resolutions)
    , signal_remap_(std::move(signal_remap))
    , generated_process_(generated_process.value_or(process.id))
    , layout_(jit_.frame_layout(binding_))
    , storage_(std::make_shared<FrameStorage>())
    , register_aval_(storage_->register_aval)
    , register_bval_(storage_->register_bval)
    , register_logic9_plane2_(storage_->register_logic9_plane2)
    , register_logic9_plane3_(storage_->register_logic9_plane3)
    , register_initialized_(storage_->register_initialized)
    , string_registers_(storage_->string_registers)
    , container_registers_(storage_->container_registers)
    , container_register_shared_(storage_->container_register_shared)
    , container_object_aliases_(storage_->container_object_aliases)
    , active_container_object_aliases_(
          storage_->active_container_object_aliases)
{
    register_aval_.resize(layout_.register_word_count);
    register_bval_.resize(layout_.register_word_count);
    if (layout_.uses_logic9) {
        register_logic9_plane2_.resize(layout_.register_word_count);
        register_logic9_plane3_.resize(layout_.register_word_count);
    }
    register_initialized_.resize(layout_.register_count);
    string_registers_.resize(layout_.string_register_count);
    container_registers_.reserve(process.container_register_types.size());
    for (const auto& type : process.container_register_types) {
        runtime::simir::ContainerValue value;
        value.type = type;
        container_registers_.push_back(
            std::make_shared<runtime::simir::ContainerValue>(std::move(value)));
    }
    container_register_shared_.assign(container_registers_.size(), 0U);
    container_object_aliases_.assign(
        process.container_register_types.size(), invalid_container_object);
    build_dense_signal_remap(
        signal_remap_, dense_signal_remap_base_, dense_signal_remap_);
    initialize_direct_read_signals();
    initialize_direct_update_slots();
    initialize_buffered_logic9_updates();
    pending_update_words_.reserve(buffered_update_capacity(process));
    jit_.initialize_frame(
        binding_, frame_, register_aval_, register_bval_,
        register_initialized_, register_logic9_plane2_,
        register_logic9_plane3_);
}

LlvmProcessExecutor::LlvmProcessExecutor(
    compiler::LlvmJit& jit,
    const compiler::JitProcessBinding binding,
    const runtime::simir::Process& process,
    const std::span<const std::uint32_t> signal_widths,
    const std::span<const runtime::simir::ValueKind> signal_value_kinds,
    const std::span<const runtime::simir::ResolutionKind> signal_resolutions,
    std::shared_ptr<const SignalRemap> signal_remap,
    const runtime::simir::ProcessId generated_process,
    std::shared_ptr<FrameStorage> storage,
    const fsim_jit_frame_v1& parent_frame,
    const runtime::simir::InstructionIndex start_instruction)
    : jit_(jit)
    , binding_(binding)
    , process_(process)
    , signal_widths_(signal_widths)
    , signal_value_kinds_(signal_value_kinds)
    , signal_resolutions_(signal_resolutions)
    , signal_remap_(std::move(signal_remap))
    , generated_process_(generated_process)
    , layout_(jit_.frame_layout(binding_))
    , storage_(std::move(storage))
    , frame_(parent_frame)
    , register_aval_(storage_->register_aval)
    , register_bval_(storage_->register_bval)
    , register_logic9_plane2_(storage_->register_logic9_plane2)
    , register_logic9_plane3_(storage_->register_logic9_plane3)
    , register_initialized_(storage_->register_initialized)
    , string_registers_(storage_->string_registers)
    , container_registers_(storage_->container_registers)
    , container_register_shared_(storage_->container_register_shared)
    , container_object_aliases_(storage_->container_object_aliases)
    , active_container_object_aliases_(
          storage_->active_container_object_aliases)
{
    build_dense_signal_remap(
        signal_remap_, dense_signal_remap_base_, dense_signal_remap_);
    initialize_direct_read_signals();
    initialize_direct_update_slots();
    initialize_buffered_logic9_updates();
    pending_update_words_.reserve(buffered_update_capacity(process));
    frame_.program_counter = start_instruction;
    frame_.state = FSIM_JIT_FRAME_STATE_READY;
    frame_.last_instruction = FSIM_JIT_INVALID_INSTRUCTION;
}

void LlvmProcessExecutor::initialize_direct_read_signals()
{
    direct_read_signals_.clear();
    direct_read_signals_.reserve(layout_.direct_read_signals.size());
    for (const auto signal : layout_.direct_read_signals) {
        auto actual = signal;
        if (!dense_signal_remap_.empty()
            && signal >= dense_signal_remap_base_) {
            const auto offset = signal - dense_signal_remap_base_;
            if (offset < dense_signal_remap_.size()) {
                actual = dense_signal_remap_[offset];
            }
        } else if (dense_signal_remap_.empty() && signal_remap_) {
            const auto found = std::ranges::lower_bound(
                *signal_remap_, signal, { }, &SignalRemap::value_type::first);
            if (found != signal_remap_->end() && found->first == signal) {
                actual = found->second;
            }
        }
        direct_read_signals_.push_back(actual);
    }
}

void LlvmProcessExecutor::initialize_direct_update_slots()
{
    direct_update_signals_.clear();
    direct_update_slot_views_.clear();
    buffered_logic9_update_views_.clear();
    direct_update_signals_.reserve(layout_.direct_update_signals.size());
    std::vector<std::uint32_t> widths;
    widths.reserve(layout_.direct_update_signals.size());
    for (const auto signal : layout_.direct_update_signals) {
        auto actual = signal;
        if (!dense_signal_remap_.empty()
            && signal >= dense_signal_remap_base_) {
            const auto offset = signal - dense_signal_remap_base_;
            if (offset < dense_signal_remap_.size()) {
                actual = dense_signal_remap_[offset];
            }
        } else if (dense_signal_remap_.empty() && signal_remap_) {
            const auto found = std::ranges::lower_bound(
                *signal_remap_, signal, { }, &SignalRemap::value_type::first);
            if (found != signal_remap_->end() && found->first == signal) {
                actual = found->second;
            }
        }
        direct_update_signals_.push_back(actual);
        widths.push_back(signal_widths_[signal]);
    }
    direct_update_slots_.resize(direct_update_signals_.size());
    direct_update_active_words_.assign(
        (direct_update_slots_.size() + 63U) / 64U, UINT64_C(0));
    std::size_t wide_word_count { };
    for (const auto width : widths) {
        if (width > 64U) {
            wide_word_count += (static_cast<std::size_t>(width) + 63U) / 64U;
        }
    }
    direct_update_wide_aval_.resize(wide_word_count);
    direct_update_wide_bval_.resize(wide_word_count);
    direct_update_wide_mask_.resize(wide_word_count);
    std::size_t wide_offset { };
    for (std::size_t index = 0; index < widths.size(); ++index) {
        auto& slot = direct_update_slots_[index];
        const auto width = widths[index];
        slot.width = width;
        slot.word_count = static_cast<std::uint32_t>(
            (static_cast<std::size_t>(width) + 63U) / 64U);
        if (width <= 64U) {
            continue;
        }
        slot.wide_aval = direct_update_wide_aval_.data() + wide_offset;
        slot.wide_bval = direct_update_wide_bval_.data() + wide_offset;
        slot.wide_mask = direct_update_wide_mask_.data() + wide_offset;
        wide_offset += slot.word_count;
    }
    direct_update_slot_views_.reserve(direct_update_slots_.size());
    buffered_logic9_update_views_.reserve(direct_update_slots_.size());
    for (std::size_t index = 0; index < direct_update_slots_.size(); ++index) {
        auto& slot = direct_update_slots_[index];
        const auto signal = direct_update_signals_[index];
        const auto kind = signal < signal_value_kinds_.size()
            ? signal_value_kinds_[signal]
            : runtime::simir::ValueKind::logic4;
        if (kind == runtime::simir::ValueKind::logic9) {
            buffered_logic9_update_views_.push_back({
                signal, slot.width, &slot.aval, &slot.mask
            });
            continue;
        }
        const bool wide = slot.width > 64U;
        direct_update_slot_views_.push_back({
            signal,
            slot.width,
            slot.word_count,
            &slot.active,
            wide ? slot.wide_aval : &slot.aval,
            wide ? slot.wide_bval : &slot.bval,
            wide ? slot.wide_mask : &slot.mask
        });
    }
}

void LlvmProcessExecutor::initialize_buffered_logic9_updates()
{
    if (std::getenv("FSIM_DISABLE_BUFFERED_LOGIC9_UPDATES") != nullptr) {
        return;
    }
    std::vector<runtime::simir::SignalId> signals;
    std::vector<runtime::simir::SignalId> projected_candidates;
    std::vector<runtime::simir::SignalId> projected_unsafe;
    const auto add_unique = [](auto& collection, const auto signal) {
        if (std::ranges::find(collection, signal) == collection.end()) {
            collection.push_back(signal);
        }
    };
    for (const auto& stored : process_.operations) {
        if (const auto* projected = runtime::simir::operation_get_if<
                runtime::simir::WriteProjected>(&stored)) {
            const bool simple = projected->delay == 0U
                && projected->rejection == 0U
                && projected->mode
                    == runtime::simir::ProjectedDelayMode::inertial;
            add_unique(
                simple ? projected_candidates : projected_unsafe,
                projected->signal);
        } else if (const auto* projected_slice = runtime::simir::operation_get_if<
                runtime::simir::WriteProjectedSlice>(&stored)) {
            const bool simple = projected_slice->delay == 0U
                && projected_slice->rejection == 0U
                && projected_slice->mode
                    == runtime::simir::ProjectedDelayMode::inertial;
            add_unique(
                simple ? projected_candidates : projected_unsafe,
                projected_slice->signal);
        } else if (const auto* waveform = runtime::simir::operation_get_if<
                runtime::simir::WriteProjectedWaveform>(&stored)) {
            add_unique(projected_unsafe, waveform->signal);
        } else if (const auto* waveform_slice = runtime::simir::operation_get_if<
                runtime::simir::WriteProjectedWaveformSlice>(&stored)) {
            add_unique(projected_unsafe, waveform_slice->signal);
        } else if (const auto* dynamic = runtime::simir::operation_get_if<
                runtime::simir::WriteProjectedDynamicSlice>(&stored)) {
            add_unique(projected_unsafe, dynamic->signal);
        } else if (const auto* dynamic_waveform = runtime::simir::operation_get_if<
                runtime::simir::WriteProjectedWaveformDynamicSlice>(&stored)) {
            add_unique(projected_unsafe, dynamic_waveform->signal);
        }
    }
    std::erase_if(projected_candidates, [&](const auto signal) {
        return std::ranges::find(projected_unsafe, signal)
            != projected_unsafe.end();
    });
    std::ranges::sort(projected_candidates);
    const auto add = [&](const runtime::simir::SignalId signal) {
        auto actual = signal;
        if (!dense_signal_remap_.empty()
            && signal >= dense_signal_remap_base_) {
            const auto offset = signal - dense_signal_remap_base_;
            if (offset < dense_signal_remap_.size()) {
                actual = dense_signal_remap_[offset];
            }
        } else if (dense_signal_remap_.empty() && signal_remap_) {
            const auto found = std::ranges::lower_bound(
                *signal_remap_, signal, { }, &SignalRemap::value_type::first);
            if (found != signal_remap_->end() && found->first == signal) {
                actual = found->second;
            }
        }
        if (actual >= signal_widths_.size()
            || actual >= signal_value_kinds_.size()
            || signal_value_kinds_[actual]
                != runtime::simir::ValueKind::logic9
            || signal_widths_[actual] == 0U
            || signal_widths_[actual] > 64U) {
            return;
        }
        if (std::ranges::find(direct_update_signals_, actual)
            != direct_update_signals_.end()) {
            return;
        }
        signals.push_back(actual);
    };
    for (const auto& stored : process_.operations) {
        if (const auto* operation
            = runtime::simir::operation_get_if<
                runtime::simir::WriteUpdate>(&stored)) {
            add(operation->signal);
        } else if (const auto* slice
            = runtime::simir::operation_get_if<
                runtime::simir::WriteUpdateSlice>(&stored)) {
            add(slice->signal);
        } else if (const auto* dynamic_slice
            = runtime::simir::operation_get_if<
                runtime::simir::WriteUpdateDynamicSlice>(&stored)) {
            add(dynamic_slice->signal);
        } else if (const auto* dynamic_part
            = runtime::simir::operation_get_if<
                runtime::simir::WriteUpdateDynamicPartSlice>(&stored)) {
            add(dynamic_part->signal);
        } else if (const auto* projected = runtime::simir::operation_get_if<
                runtime::simir::WriteProjected>(&stored)) {
            if (std::ranges::binary_search(
                    projected_candidates, projected->signal)) {
                add(projected->signal);
            }
        } else if (const auto* projected_slice = runtime::simir::operation_get_if<
                runtime::simir::WriteProjectedSlice>(&stored)) {
            if (std::ranges::binary_search(
                    projected_candidates, projected_slice->signal)) {
                add(projected_slice->signal);
            }
        }
    }
    std::ranges::sort(signals);
    const auto unique = std::ranges::unique(signals);
    signals.erase(unique.begin(), unique.end());
    buffered_logic9_updates_.reserve(signals.size());
    for (const auto signal : signals) {
        buffered_logic9_updates_.push_back({
            signal, signal_widths_[signal], { }, 0U
        });
    }
    buffered_logic9_update_views_.reserve(
        buffered_logic9_update_views_.size()
        + buffered_logic9_updates_.size());
    for (auto& slot : buffered_logic9_updates_) {
        buffered_logic9_update_views_.push_back({
            slot.signal, slot.width, slot.planes.data(), &slot.mask
        });
    }
}

[[nodiscard]] std::unique_ptr<runtime::simir::ProcessExecutor>
LlvmProcessExecutor::fork_clone(
    const runtime::simir::InstructionIndex start_instruction)
{
    auto clone = std::unique_ptr<LlvmProcessExecutor> {
        new LlvmProcessExecutor {
            jit_, binding_, process_, signal_widths_, signal_value_kinds_,
            signal_resolutions_,
            signal_remap_, generated_process_, storage_, frame_,
            start_instruction }
    };
    // A fork child can begin before a preceding nonblocking update from its
    // parent commits. Its private update-slot shadow is therefore not a stable
    // image of the shared static writer, even though both executors retain the
    // same elaborated process identity.
    clone->stable_direct_update_suppression_allowed_ = false;
    return clone;
}

void LlvmProcessExecutor::redirect(
    const runtime::simir::InstructionIndex instruction)
{
    frame_.program_counter = instruction;
    frame_.state = FSIM_JIT_FRAME_STATE_READY;
    frame_.last_instruction = FSIM_JIT_INVALID_INSTRUCTION;
}

#endif

} // namespace fsim::app::application_detail
