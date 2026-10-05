// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"

#include <cstdlib>
#include <numeric>

namespace fsim::app::application_detail {

#if defined(FSIM_HAS_LLVM)

namespace {

[[nodiscard]] std::size_t buffered_update_capacity(
    const runtime::simir::ProcessProgramView& process)
{
    return static_cast<std::size_t>(std::ranges::count_if(
        process.operations(),
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

[[nodiscard]] std::uint32_t remap_signal(
    const std::shared_ptr<const LlvmProcessExecutor::SignalRemap>& remap,
    const std::uint32_t dense_base,
    const std::vector<std::uint32_t>& dense,
    const std::uint32_t signal)
{
    if (!dense.empty()) {
        if (signal < dense_base) {
            return signal;
        }
        const auto offset = signal - dense_base;
        return offset < dense.size() ? dense[offset] : signal;
    }
    if (!remap) {
        return signal;
    }
    const auto found = std::ranges::lower_bound(
        *remap, signal, { },
        &LlvmProcessExecutor::SignalRemap::value_type::first);
    return found != remap->end() && found->first == signal
        ? found->second
        : signal;
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
    const std::optional<runtime::simir::ProcessId> generated_process,
    ExecutorHotCellPool* hot_cell_pool,
    const runtime::simir::Process* compiled_process,
    const compiler::JitProcessHandle required_direct_read_handle)
    : LlvmProcessExecutor(
          jit, handle, runtime::simir::ProcessProgramView { process },
          signal_widths, signal_value_kinds, signal_resolutions, signal_remap,
          generated_process.value_or(process.id), hot_cell_pool,
          runtime::simir::ProcessExecutorProgramBinding {
              process,
              compiled_process == nullptr ? process : *compiled_process,
              generated_process.value_or(process.id), signal_remap },
          required_direct_read_handle)
{
}

LlvmProcessExecutor::LlvmProcessExecutor(
    compiler::LlvmJit& jit,
    const compiler::JitProcessHandle handle,
    runtime::simir::ProcessProgramView process,
    std::span<const std::uint32_t> signal_widths,
    std::span<const runtime::simir::ValueKind> signal_value_kinds,
    std::span<const runtime::simir::ResolutionKind> signal_resolutions,
    std::shared_ptr<const SignalRemap> signal_remap,
    const runtime::simir::ProcessId generated_process,
    ExecutorHotCellPool* hot_cell_pool,
    runtime::simir::ProcessExecutorProgramBinding program_access_binding,
    const compiler::JitProcessHandle required_direct_read_handle)
    : jit_(jit)
    , binding_(jit.bind(handle))
    , process_(process)
    , operation_count_(jit_.operation_count(binding_))
    , signal_widths_(signal_widths)
    , signal_value_kinds_(signal_value_kinds)
    , signal_resolutions_(signal_resolutions)
    , signal_remap_(std::move(signal_remap))
    , program_access_binding_(std::move(program_access_binding))
    , generated_process_(generated_process)
    , layout_(jit_.frame_layout(binding_))
    , storage_(std::make_shared<FrameStorage>())
    , callback_state_(layout_.signal_callback_ids_are_actual)
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
    , stable_direct_update_suppression_allowed_(
          std::getenv("FSIM_DISABLE_STABLE_DIRECT_UPDATE_SUPPRESSION")
              == nullptr)
    , validated_static_sensitivity_(!process.static_sensitivity().empty())
    , has_container_registers_(!process.container_register_types().empty())
{
    const auto container_register_types
        = runtime::simir::process_layout_detail::ProcessLayoutAccess::view(
            process.container_register_types());
    if (operation_count_ != process_.operations().size()) {
        throw compiler::LlvmJitError(
            "compiled process operation count differs from its binding");
    }
    if (required_direct_read_handle) {
        required_direct_read_binding_
            = jit_.bind(required_direct_read_handle);
        required_direct_read_layout_
            = jit_.frame_layout(required_direct_read_binding_);
        if (required_direct_read_layout_.register_count
                != layout_.register_count
            || required_direct_read_layout_.register_word_count
                != layout_.register_word_count
            || required_direct_read_layout_.signal_callback_ids_are_actual
                != layout_.signal_callback_ids_are_actual
            || required_direct_read_layout_.signal_callback_operand_word_base
                != layout_.signal_callback_operand_word_base
            || required_direct_read_layout_.signal_callback_operands
                != layout_.signal_callback_operands
            || required_direct_read_layout_.string_register_count
                != layout_.string_register_count
            || required_direct_read_layout_.uses_logic9
                != layout_.uses_logic9
            || required_direct_read_layout_.tracks_register_initialization
                != layout_.tracks_register_initialization
            || required_direct_read_layout_.register_widths
                != layout_.register_widths
            || required_direct_read_layout_.register_word_offsets
                != layout_.register_word_offsets
            || required_direct_read_layout_.direct_read_signals
                != layout_.direct_read_signals
            || required_direct_read_layout_.direct_update_signals
                != layout_.direct_update_signals
            || required_direct_read_layout_.register_values_persistent
                != layout_.register_values_persistent) {
            throw compiler::LlvmJitError(
                "required-read entry has an incompatible process frame layout");
        }
    }
    register_aval_.resize(layout_.register_word_count);
    register_bval_.resize(layout_.register_word_count);
    if (layout_.uses_logic9) {
        register_logic9_plane2_.resize(layout_.register_word_count);
        register_logic9_plane3_.resize(layout_.register_word_count);
    }
    register_initialized_.resize(layout_.register_count);
    string_registers_.resize(layout_.string_register_count);
    container_registers_.reserve(container_register_types.size());
    for (const auto& type : container_register_types) {
        runtime::simir::ContainerValue value;
        value.type = type;
        container_registers_.push_back(
            std::make_shared<runtime::simir::ContainerValue>(std::move(value)));
    }
    container_register_shared_.assign(container_registers_.size(), 0U);
    container_object_aliases_.assign(
        container_register_types.size(), invalid_container_object);
    build_dense_signal_remap(
        signal_remap_, dense_signal_remap_base_, dense_signal_remap_);
    initialize_hot_cell(hot_cell_pool);
    initialize_direct_read_signals();
    initialize_code_coverage_hit_counters();
    initialize_direct_update_slots();
    initialize_buffered_logic9_updates();
    initialize_wide_logic4_write_scratch(process_);
    pending_update_words_.reserve(buffered_update_capacity(process));
    jit_.initialize_frame(
        binding_, frame_, register_aval_, register_bval_,
        register_initialized_, register_logic9_plane2_,
        register_logic9_plane3_);
    bind_signal_callback_operands(true);
}

LlvmProcessExecutor::LlvmProcessExecutor(
    compiler::LlvmJit& jit,
    const compiler::JitProcessBinding binding,
    runtime::simir::ProcessProgramView process,
    const std::span<const std::uint32_t> signal_widths,
    const std::span<const runtime::simir::ValueKind> signal_value_kinds,
    const std::span<const runtime::simir::ResolutionKind> signal_resolutions,
    std::shared_ptr<const SignalRemap> signal_remap,
    const runtime::simir::ProcessId generated_process,
    std::shared_ptr<FrameStorage> storage,
    const fsim_jit_frame_v2& parent_frame,
    const runtime::simir::InstructionIndex start_instruction,
    runtime::simir::ProcessExecutorProgramBinding program_access_binding)
    : jit_(jit)
    , binding_(binding)
    , process_(process)
    , operation_count_(jit_.operation_count(binding_))
    , signal_widths_(signal_widths)
    , signal_value_kinds_(signal_value_kinds)
    , signal_resolutions_(signal_resolutions)
    , signal_remap_(std::move(signal_remap))
    , program_access_binding_(std::move(program_access_binding))
    , generated_process_(generated_process)
    , layout_(jit_.frame_layout(binding_))
    , storage_(std::move(storage))
    , callback_state_(layout_.signal_callback_ids_are_actual)
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
    , validated_static_sensitivity_(!process.static_sensitivity().empty())
    , has_container_registers_(!process.container_register_types().empty())
{
    if (operation_count_ != process_.operations().size()) {
        throw compiler::LlvmJitError(
            "compiled process operation count differs from its binding");
    }
    build_dense_signal_remap(
        signal_remap_, dense_signal_remap_base_, dense_signal_remap_);
    bind_signal_callback_operands(false);
    initialize_hot_cell(nullptr);
    initialize_direct_read_signals();
    initialize_code_coverage_hit_counters();
    initialize_direct_update_slots();
    initialize_buffered_logic9_updates();
    initialize_wide_logic4_write_scratch(process);
    pending_update_words_.reserve(buffered_update_capacity(process));
    frame_.program_counter = start_instruction;
    frame_.state = FSIM_JIT_FRAME_STATE_READY_V2;
    frame_.last_instruction = FSIM_JIT_INVALID_INSTRUCTION_V2;
}

void LlvmProcessExecutor::initialize_code_coverage_hit_counters()
{
    code_coverage_hit_counters_.clear();
    code_coverage_hit_counters_.reserve(static_cast<std::size_t>(
        std::ranges::count_if(
            process_.operations(),
            [](const runtime::simir::Operation& operation) {
                return runtime::simir::operation_holds<
                    runtime::simir::CodeCoverageHit>(operation);
            })));
    for (std::size_t instruction = 0U;
         instruction < process_.operations().size(); ++instruction) {
        const auto* hit = runtime::simir::operation_get_if<
            runtime::simir::CodeCoverageHit>(
                &process_.operations()[instruction]);
        if (hit == nullptr) {
            continue;
        }
        code_coverage_hit_counters_.push_back(
            process_.operations().code_coverage_counter(
                instruction, hit->counter).value);
    }
}

void LlvmProcessExecutor::initialize_direct_read_signals()
{
    direct_read_signals_.clear();
    direct_read_signals_.reserve(layout_.direct_read_signals.size());
    for (const auto signal : layout_.direct_read_signals) {
        direct_read_signals_.push_back(remap_signal(
            signal_remap_, dense_signal_remap_base_, dense_signal_remap_,
            signal));
    }
    runtime_direct_read_signals_.assign(
        direct_read_signals_.size(),
        std::numeric_limits<std::uint32_t>::max());
}

void LlvmProcessExecutor::bind_signal_callback_operands(
    const bool initialize)
{
    const auto base = static_cast<std::size_t>(
        layout_.signal_callback_operand_word_base);
    if (base > layout_.register_word_count
        || register_aval_.size() < layout_.register_word_count
        || register_bval_.size() < layout_.register_word_count
        || (layout_.uses_logic9
            && (register_logic9_plane2_.size() < layout_.register_word_count
                || register_logic9_plane3_.size()
                    < layout_.register_word_count))
        || callback_state_.signal_callback_ids_are_actual
            != layout_.signal_callback_ids_are_actual) {
        throw compiler::LlvmJitError(
            "compiled signal-callback operand frame is incompatible");
    }

    const auto tail_size
        = static_cast<std::size_t>(layout_.register_word_count) - base;
    if (layout_.signal_callback_operands.size() != tail_size
        || (!layout_.signal_callback_ids_are_actual
            && (!layout_.signal_callback_operands.empty() || tail_size != 0U))) {
        throw compiler::LlvmJitError(
            "compiled signal-callback operand tail has an invalid extent");
    }

    for (std::size_t index = 0U;
         index < layout_.signal_callback_operands.size(); ++index) {
        const auto canonical = layout_.signal_callback_operands[index];
        const auto actual = remap_signal(
            signal_remap_, dense_signal_remap_base_, dense_signal_remap_,
            canonical);
        const auto word = base + index;
        if (actual >= signal_widths_.size()
            || signal_widths_[actual] == 0U) {
            throw compiler::LlvmJitError(
                "signal-callback operand maps outside the bound signal table");
        }
        if (initialize) {
            register_aval_[word] = actual;
        } else if (register_aval_[word] != actual) {
            throw compiler::LlvmJitError(
                "forked signal-callback operand tail differs from its binding");
        }
        if (register_bval_[word] != 0U
            || (layout_.uses_logic9
                && (register_logic9_plane2_[word] != 0U
                    || register_logic9_plane3_[word] != 0U))) {
            throw compiler::LlvmJitError(
                "signal-callback operand tail contains non-integer plane data");
        }
    }
    signal_callback_operands_bound_ = true;
    if (!signal_callback_operand_frame_shape_matches()) {
        throw compiler::LlvmJitError(
            "signal-callback operand tail does not match its frame binding");
    }
}

bool LlvmProcessExecutor::signal_callback_operand_frame_shape_matches()
    const noexcept
{
    const auto base = static_cast<std::size_t>(
        layout_.signal_callback_operand_word_base);
    return signal_callback_operands_bound_
        && base <= layout_.register_word_count
        && layout_.signal_callback_operands.size()
            == static_cast<std::size_t>(layout_.register_word_count) - base
        && (layout_.signal_callback_ids_are_actual
            || layout_.signal_callback_operands.empty())
        && callback_state_.signal_callback_ids_are_actual
            == layout_.signal_callback_ids_are_actual
        && register_aval_.size() == layout_.register_word_count
        && register_bval_.size() == layout_.register_word_count
        && register_initialized_.size() == layout_.register_count
        && register_logic9_plane2_.size()
            == (layout_.uses_logic9 ? layout_.register_word_count : 0U)
        && register_logic9_plane3_.size()
            == (layout_.uses_logic9 ? layout_.register_word_count : 0U)
        && frame_.layout_id_low == layout_.layout_id_low
        && frame_.layout_id_high == layout_.layout_id_high
        && frame_.register_count == layout_.register_count
        && frame_.register_aval == register_aval_.data()
        && frame_.register_bval == register_bval_.data()
        && frame_.register_initialized == register_initialized_.data()
        && frame_.register_logic9_plane2
            == (layout_.uses_logic9
                    ? register_logic9_plane2_.data() : nullptr)
        && frame_.register_logic9_plane3
            == (layout_.uses_logic9
                    ? register_logic9_plane3_.data() : nullptr);
}

void LlvmProcessExecutor::initialize_hot_cell(ExecutorHotCellPool* pool)
{
    if (pool != nullptr && layout_.direct_update_signals.size() == 1U) {
        const auto source = layout_.direct_update_signals.front();
        if (source < signal_widths_.size()) {
            const auto width = signal_widths_[source];
            const auto actual = remap_signal(
                signal_remap_, dense_signal_remap_base_,
                dense_signal_remap_, source);
            const auto kind = actual < signal_value_kinds_.size()
                ? signal_value_kinds_[actual]
                : runtime::simir::ValueKind::logic4;
            if (width != 0U && width <= 128U
                && kind == runtime::simir::ValueKind::logic4) {
                hot_cell_ = pool->acquire();
            }
        }
    }
}

void LlvmProcessExecutor::initialize_direct_update_slots()
{
    direct_update_signals_.clear();
    direct_update_slot_views_storage_.clear();
    buffered_logic9_update_views_.clear();
    direct_update_signals_.reserve(layout_.direct_update_signals.size());
    std::vector<std::uint32_t> widths;
    widths.reserve(layout_.direct_update_signals.size());
    for (const auto signal : layout_.direct_update_signals) {
        const auto actual = remap_signal(
            signal_remap_, dense_signal_remap_base_, dense_signal_remap_,
            signal);
        direct_update_signals_.push_back(actual);
        widths.push_back(signal_widths_[signal]);
    }
    if (hot_cell_) {
        direct_update_slots_ = { &hot_cell_->slot, 1U };
        direct_update_active_words_ = {
            &hot_cell_->active_word, 1U
        };
    } else {
        direct_update_slots_storage_.resize(direct_update_signals_.size());
        direct_update_active_words_storage_.assign(
            (direct_update_slots_storage_.size() + 63U) / 64U, UINT64_C(0));
        direct_update_slots_ = direct_update_slots_storage_;
        direct_update_active_words_ = direct_update_active_words_storage_;
    }
    std::size_t wide_word_count { };
    for (const auto width : widths) {
        if (width > 64U) {
            wide_word_count += (static_cast<std::size_t>(width) + 63U) / 64U;
        }
    }
    std::span<std::uint64_t> wide_aval;
    std::span<std::uint64_t> wide_bval;
    std::span<std::uint64_t> wide_mask;
    if (hot_cell_) {
        if (wide_word_count != 0U) {
            wide_aval = hot_cell_->wide_aval;
            wide_bval = hot_cell_->wide_bval;
            wide_mask = hot_cell_->wide_mask;
        }
    } else {
        direct_update_wide_aval_.resize(wide_word_count);
        direct_update_wide_bval_.resize(wide_word_count);
        direct_update_wide_mask_.resize(wide_word_count);
        wide_aval = direct_update_wide_aval_;
        wide_bval = direct_update_wide_bval_;
        wide_mask = direct_update_wide_mask_;
    }
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
        slot.wide_aval = wide_aval.data() + wide_offset;
        slot.wide_bval = wide_bval.data() + wide_offset;
        slot.wide_mask = wide_mask.data() + wide_offset;
        wide_offset += slot.word_count;
    }
    if (!hot_cell_) {
        direct_update_slot_views_storage_.reserve(direct_update_slots_.size());
    }
    buffered_logic9_update_views_.reserve(direct_update_slots_.size());
    for (std::size_t index = 0;
         index < direct_update_slots_.size(); ++index) {
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
        const runtime::simir::ProcessUpdateSlotView view {
            signal,
            slot.width,
            slot.word_count,
            &slot.active,
            wide ? slot.wide_aval : &slot.aval,
            wide ? slot.wide_bval : &slot.bval,
            wide ? slot.wide_mask : &slot.mask
        };
        if (hot_cell_) {
            hot_cell_->slot_view = view;
        } else {
            direct_update_slot_views_storage_.push_back(view);
        }
    }
    direct_update_slot_views_ = hot_cell_
        ? std::span { &hot_cell_->slot_view, 1U }
        : std::span { direct_update_slot_views_storage_ };
}

void LlvmProcessExecutor::initialize_wide_logic4_write_scratch(
    const runtime::simir::ProcessProgramView& registered_process)
{
    using namespace runtime::simir;

    std::map<std::uint32_t, std::size_t> scratch_count_by_width;
    const auto add_source = [&](const RegisterId source) {
        const auto index = static_cast<std::size_t>(source);
        if (index >= layout_.register_widths.size()) {
            return;
        }
        const auto width = layout_.register_widths[index];
        // P0 keeps Logic4 values through 128 bits inline, so only values
        // requiring the heap-backed plane helper need executor scratch.
        if (width > 128U) {
            ++scratch_count_by_width[width];
        }
    };
    const auto is_logic4_destination = [&](const SignalId signal) {
        return signal >= signal_value_kinds_.size()
            || signal_value_kinds_[signal] != ValueKind::logic9;
    };
    const auto add_write = [&](const SignalId signal,
                               const RegisterId source) {
        if (is_logic4_destination(signal)) {
            add_source(source);
        }
    };

    // The measured native callback path is a scheduled UPDATE. Reserve one
    // reusable image for each static source in the registered instance view;
    // remap congruence is verified by the executor binding setup.
    const auto& operations = registered_process.operations();
    for (std::size_t index = 0U; index < operations.size(); ++index) {
        const auto operation = operations.expanded(index);
        if (const auto* update = operation_get_if<WriteUpdate>(&operation)) {
            add_write(update->signal, update->source);
        } else if (const auto* update_slice =
                       operation_get_if<WriteUpdateSlice>(&operation)) {
            add_write(update_slice->signal, update_slice->source);
        } else if (const auto* update_dynamic =
                       operation_get_if<WriteUpdateDynamicSlice>(&operation)) {
            add_write(update_dynamic->signal, update_dynamic->source);
        } else if (const auto* update_part =
                       operation_get_if<WriteUpdateDynamicPartSlice>(&operation)) {
            add_write(update_part->signal, update_part->source);
        }
    }

    std::size_t total_scratch_count { };
    for (const auto& [width, count] : scratch_count_by_width) {
        static_cast<void>(width);
        total_scratch_count += count;
    }
    auto& scratch = callback_state_.wide_logic4_write_scratch;
    scratch.reserve(total_scratch_count);
    for (const auto& [width, count] : scratch_count_by_width) {
        for (std::size_t index = 0U; index < count; ++index) {
            scratch.emplace_back(width);
        }
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
    for (const auto& stored : process_.operations()) {
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
        // process_ already uses the candidate's signal IDs.
        const auto actual = signal;
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
    for (const auto& stored : process_.operations()) {
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

LlvmProcessExecutor::RegionCompletion::RegionCompletion(
    LlvmProcessExecutor& executor,
    const std::span<const runtime::simir::ProcessExecutor::RegionRegisterBinding>
        register_bindings,
    const std::span<const PackedLogic4> activation_registers,
    const runtime::simir::InstructionIndex wait_instruction,
    const runtime::simir::InstructionIndex jump_instruction)
    : executor_(&executor)
    , instance_generation_(executor.instance_generation_)
    , storage_(executor.storage_)
    , register_bindings_(register_bindings)
    , activation_registers_(activation_registers)
    , wait_instruction_(wait_instruction)
    , jump_instruction_(jump_instruction)
{
}

const void* LlvmProcessExecutor::RegionCompletion::storage_identity() const noexcept
{
    return storage_.get();
}

void LlvmProcessExecutor::RegionCompletion::commit() noexcept
{
    if (committed_ || executor_ == nullptr || !storage_
        || executor_->instance_generation_ != instance_generation_
        || executor_->storage_.get() != storage_.get()) {
        std::terminate();
    }

    auto& executor = *executor_;
    for (const auto& binding : register_bindings_) {
        if (!binding.defined
            || executor.layout_.register_values_persistent[
                binding.source_register] == 0U) {
            continue;
        }
        const auto source = static_cast<std::size_t>(binding.source_register);
        const auto& value = activation_registers_[binding.activation_register];
        const auto width = binding.width;
        const auto words = (static_cast<std::size_t>(width) + 63U) / 64U;
        const auto offset = executor.layout_.register_word_offsets[source];
        const auto kind = binding.value_kind;

        std::array<std::span<const std::uint64_t>, 4U> planes;
        if (kind == runtime::simir::ValueKind::logic9) {
            for (std::size_t plane = 0U; plane < planes.size(); ++plane) {
                planes[plane] = value.logic9_plane_words(plane);
            }
        } else {
            planes[0] = value.aval_words();
            planes[1] = value.bval_words();
        }

        for (std::size_t word = 0U; word < words; ++word) {
            auto aval = planes[0][word];
            auto bval = planes[1][word];
            auto plane2 = kind == runtime::simir::ValueKind::logic9
                ? planes[2][word] : UINT64_C(0);
            auto plane3 = kind == runtime::simir::ValueKind::logic9
                ? planes[3][word] : UINT64_C(0);
            if (word + 1U == words && (width % 64U) != 0U) {
                const auto mask = (UINT64_C(1) << (width % 64U)) - 1U;
                aval &= mask;
                bval &= mask;
                plane2 &= mask;
                plane3 &= mask;
            }
            const auto destination = offset + word;
            executor.register_aval_[destination] = aval;
            executor.register_bval_[destination] = bval;
            if (executor.layout_.uses_logic9) {
                executor.register_logic9_plane2_[destination] = plane2;
                executor.register_logic9_plane3_[destination] = plane3;
            }
        }
        if (executor.layout_.tracks_register_initialization) {
            executor.register_initialized_[source] = 1U;
        }
    }

    executor.frame_.program_counter = jump_instruction_;
    executor.frame_.state = FSIM_JIT_FRAME_STATE_READY_V2;
    executor.frame_.last_instruction = wait_instruction_;
    committed_ = true;
    executor_ = nullptr;
}

std::unique_ptr<runtime::simir::ProcessExecutor::PreparedRegionCompletion>
LlvmProcessExecutor::prepare_region_completion(
    const runtime::simir::ProcessId process,
    const runtime::simir::InstructionIndex wait_instruction,
    const runtime::simir::InstructionIndex jump_instruction,
    const std::span<const runtime::simir::ProcessExecutor::RegionRegisterBinding>
        register_bindings,
    const std::span<const PackedLogic4> activation_registers)
{
    using namespace runtime::simir;
    const auto register_value_kinds
        = process_layout_detail::ProcessLayoutAccess::view(process_.register_value_kinds());

    if (process != process_.id()
        || !program_access_binding_.valid()
        || !process_.matches_registered_binding(process, program_access_binding_)
        || !storage_ || callback_state_.failure
        || cohort_resume_mode_ != CohortResumeMode::normal
        || frame_.abi_version != FSIM_JIT_FRAME_ABI_VERSION_V2
        || frame_.struct_size < sizeof(fsim_jit_frame_v2)
        || frame_.layout_id_low != layout_.layout_id_low
        || frame_.layout_id_high != layout_.layout_id_high
        || frame_.register_count != layout_.register_count
        || frame_.state != FSIM_JIT_FRAME_STATE_READY_V2
        || frame_.program_counter != jump_instruction
        || frame_.last_instruction != wait_instruction
        || frame_.native_call_depth != 0U
        || frame_.native_call_reserved != 0U
        || std::ranges::any_of(frame_.native_return_stack,
            [](const std::uint32_t value) { return value != 0U; })
        || runtime_.abi_version != FSIM_JIT_RUNTIME_ABI_VERSION_V2
        || runtime_.struct_size < sizeof(fsim_jit_runtime_instance_v2)
        || runtime_.flags != 0U || runtime_.reserved != 0U
        || runtime_.direct_update_reserved != 0U
        || runtime_.direct_update_active_reserved != 0U
        || process_.operations().size() != operation_count_
        || process_.operations().size() < 2U
        || wait_instruction != process_.operations().size() - 2U
        || jump_instruction != process_.operations().size() - 1U
        || !operation_holds<WaitSensitivity>(
            process_.operations().expanded(wait_instruction))
        || !operation_holds<Jump>(
            process_.operations().expanded(jump_instruction))
        || operation_get<Jump>(
               process_.operations().expanded(jump_instruction)).target != 0U
        || process_.static_sensitivity().empty()
        || layout_.register_count != process_.register_count()
        || layout_.register_widths.size() != layout_.register_count
        || layout_.register_word_offsets.size() != layout_.register_count
        || layout_.register_values_persistent.size()
            != layout_.register_count
        || !signal_callback_operand_frame_shape_matches()
        || std::ranges::any_of(layout_.register_values_persistent,
            [](const std::uint8_t value) { return value > 1U; })
        || register_bindings.size() != layout_.register_count
        || (!register_value_kinds.empty()
            && register_value_kinds.size() != layout_.register_count)
        || layout_.register_word_count != register_aval_.size()
        || register_bval_.size() != layout_.register_word_count
        || register_initialized_.size() != layout_.register_count
        || frame_.register_aval != register_aval_.data()
        || frame_.register_bval != register_bval_.data()
        || frame_.register_initialized != register_initialized_.data()
        || layout_.uses_logic9
            != !register_logic9_plane2_.empty()
        || register_logic9_plane2_.size()
            != (layout_.uses_logic9 ? layout_.register_word_count : 0U)
        || register_logic9_plane3_.size()
            != (layout_.uses_logic9 ? layout_.register_word_count : 0U)
        || frame_.register_logic9_plane2
            != (layout_.uses_logic9
                    ? register_logic9_plane2_.data() : nullptr)
        || frame_.register_logic9_plane3
            != (layout_.uses_logic9
                    ? register_logic9_plane3_.data() : nullptr)
        || !string_registers_.empty() || !container_registers_.empty()
        || !active_container_object_aliases_.empty()
        || !pending_update_words_.empty() || has_buffered_update_words()
        || has_buffered_logic9_updates()) {
        return { };
    }

    std::uint64_t expected_word_offset { };
    RegisterId prior_activation_register { };
    bool has_prior_activation_register { };
    for (std::size_t index = 0U; index < register_bindings.size(); ++index) {
        const auto& binding = register_bindings[index];
        if (binding.source_register != index
            || binding.activation_register >= activation_registers.size()
            || (has_prior_activation_register
                && binding.activation_register <= prior_activation_register)) {
            return { };
        }
        has_prior_activation_register = true;
        prior_activation_register = binding.activation_register;

        const auto width = layout_.register_widths[index];
        const auto offset = layout_.register_word_offsets[index];
        const auto source_word_count = static_cast<std::uint64_t>(
            layout_.signal_callback_operand_word_base);
        if (offset != expected_word_offset
            || expected_word_offset > source_word_count) {
            return { };
        }
        const auto words = (static_cast<std::uint64_t>(width) + 63U) / 64U;
        if (words > source_word_count - expected_word_offset) {
            return { };
        }
        expected_word_offset += words;

        const auto kind = register_value_kinds.empty()
            ? ValueKind::logic4 : register_value_kinds[index];
        if (!binding.defined) {
            if (binding.width != 0U || binding.value_kind != kind) {
                return { };
            }
            continue;
        }
        if (width == 0U || binding.width != width
            || binding.value_kind != kind) {
            return { };
        }
        const auto& value = activation_registers[binding.activation_register];
        if (value.width() != width
            || value.is_logic9() != (kind == ValueKind::logic9)
            || (kind == ValueKind::logic9 && !layout_.uses_logic9)) {
            return { };
        }
        for (std::size_t plane = 0U;
             kind == ValueKind::logic9 && plane < 4U; ++plane) {
            const auto bits = value.logic9_plane_words(plane);
            if (bits.size() != words) {
                return { };
            }
        }
        if (kind == ValueKind::logic4
            && (value.aval_words().size() != words
                || value.bval_words().size() != words)) {
            return { };
        }
    }
    if (expected_word_offset
        != layout_.signal_callback_operand_word_base) {
        return { };
    }

    return std::make_unique<RegionCompletion>(
        *this, register_bindings, activation_registers,
        wait_instruction, jump_instruction);
}

bool LlvmProcessExecutor::region_kernel_completion_has_no_persistent_registers()
    const noexcept
{
    if (!program_access_binding_.valid()
        || !process_.matches_registered_binding(
            process_.id(), program_access_binding_)
        || layout_.register_count != process_.register_count()
        || layout_.register_values_persistent.size()
            != layout_.register_count
        || !signal_callback_operand_frame_shape_matches()
        || std::ranges::any_of(layout_.register_values_persistent,
            [](const std::uint8_t value) { return value != 0U; })) {
        return false;
    }
    return true;
}

bool LlvmProcessExecutor::validate_region_completion_native(
    const runtime::simir::ProcessId process,
    const runtime::simir::InstructionIndex wait_instruction,
    const runtime::simir::InstructionIndex jump_instruction,
    const std::span<const runtime::simir::ProcessExecutor::RegionRegisterBinding>
        register_bindings,
    const std::size_t activation_register_count,
    const bool require_all_registers_defined) const noexcept
{
    using namespace runtime::simir;
    const auto register_value_kinds
        = process_layout_detail::ProcessLayoutAccess::view(
            process_.register_value_kinds());

    if (process != process_.id() || !program_access_binding_.valid()
        || !process_.matches_registered_binding(
            process, program_access_binding_)
        || !storage_ || callback_state_.failure
        || cohort_resume_mode_ != CohortResumeMode::normal
        || frame_.abi_version != FSIM_JIT_FRAME_ABI_VERSION_V2
        || frame_.struct_size < sizeof(fsim_jit_frame_v2)
        || frame_.layout_id_low != layout_.layout_id_low
        || frame_.layout_id_high != layout_.layout_id_high
        || frame_.register_count != layout_.register_count
        || frame_.state != FSIM_JIT_FRAME_STATE_READY_V2
        || frame_.program_counter != jump_instruction
        || frame_.last_instruction != wait_instruction
        || frame_.native_call_depth != 0U
        || frame_.native_call_reserved != 0U
        || std::ranges::any_of(frame_.native_return_stack,
            [](const std::uint32_t value) { return value != 0U; })
        || runtime_.abi_version != FSIM_JIT_RUNTIME_ABI_VERSION_V2
        || runtime_.struct_size < sizeof(fsim_jit_runtime_instance_v2)
        || runtime_.flags != 0U || runtime_.reserved != 0U
        || runtime_.direct_update_reserved != 0U
        || runtime_.direct_update_active_reserved != 0U
        || process_.operations().size() != operation_count_
        || process_.operations().size() < 2U
        || wait_instruction != process_.operations().size() - 2U
        || jump_instruction != process_.operations().size() - 1U
        || !operation_holds<WaitSensitivity>(
            process_.operations().expanded(wait_instruction))
        || !operation_holds<Jump>(
            process_.operations().expanded(jump_instruction))
        || operation_get<Jump>(process_.operations().expanded(
               jump_instruction)).target != 0U
        || process_.static_sensitivity().empty()
        || layout_.register_count != process_.register_count()
        || layout_.register_widths.size() != layout_.register_count
        || layout_.register_word_offsets.size() != layout_.register_count
        || layout_.register_values_persistent.size()
            != layout_.register_count
        || std::ranges::any_of(layout_.register_values_persistent,
            [require_all_registers_defined](const std::uint8_t value) {
                return value > 1U
                    || (require_all_registers_defined && value != 0U);
            })
        || register_bindings.size() != layout_.register_count
        || (!register_value_kinds.empty()
            && register_value_kinds.size() != layout_.register_count)
        || layout_.register_word_count != register_aval_.size()
        || register_bval_.size() != layout_.register_word_count
        || register_initialized_.size() != layout_.register_count
        || frame_.register_aval != register_aval_.data()
        || frame_.register_bval != register_bval_.data()
        || frame_.register_initialized != register_initialized_.data()
        || layout_.uses_logic9 != !register_logic9_plane2_.empty()
        || register_logic9_plane2_.size()
            != (layout_.uses_logic9 ? layout_.register_word_count : 0U)
        || register_logic9_plane3_.size()
            != (layout_.uses_logic9 ? layout_.register_word_count : 0U)
        || frame_.register_logic9_plane2
            != (layout_.uses_logic9
                    ? register_logic9_plane2_.data() : nullptr)
        || frame_.register_logic9_plane3
            != (layout_.uses_logic9
                    ? register_logic9_plane3_.data() : nullptr)
        || !string_registers_.empty() || !container_registers_.empty()
        || !active_container_object_aliases_.empty()
        || !pending_update_words_.empty() || has_buffered_update_words()
        || has_buffered_logic9_updates()) {
        return false;
    }

    std::uint64_t expected_word_offset { };
    RegisterId prior_activation_register { };
    bool has_prior_activation_register { };
    for (std::size_t index = 0U; index < register_bindings.size(); ++index) {
        const auto& binding = register_bindings[index];
        if (binding.source_register != index
            || binding.activation_register >= activation_register_count
            || (has_prior_activation_register
                && binding.activation_register <= prior_activation_register)
            || (require_all_registers_defined && !binding.defined)) {
            return false;
        }
        prior_activation_register = binding.activation_register;
        has_prior_activation_register = true;
        const auto width = layout_.register_widths[index];
        const auto offset = layout_.register_word_offsets[index];
        const auto source_word_count = static_cast<std::uint64_t>(
            layout_.signal_callback_operand_word_base);
        if (offset != expected_word_offset
            || expected_word_offset > source_word_count) {
            return false;
        }
        const auto words = (static_cast<std::uint64_t>(width) + 63U) / 64U;
        if (words > source_word_count - expected_word_offset) {
            return false;
        }
        expected_word_offset += words;
        const auto kind = register_value_kinds.empty()
            ? ValueKind::logic4 : register_value_kinds[index];
        if (!binding.defined) {
            if (binding.width != 0U || binding.value_kind != kind) {
                return false;
            }
        } else if (width == 0U || binding.width != width
            || binding.value_kind != kind) {
            return false;
        }
    }
    return expected_word_offset
        == layout_.signal_callback_operand_word_base;
}

bool LlvmProcessExecutor::region_kernel_completion_is_parked_native(
    const runtime::simir::ProcessId process,
    const runtime::simir::InstructionIndex wait_instruction,
    const runtime::simir::InstructionIndex jump_instruction,
    const std::span<const runtime::simir::ProcessExecutor::RegionRegisterBinding>
        register_bindings,
    const std::size_t activation_register_count) const noexcept
{
    return !native_region_completion_prepared_
        && !native_region_completion_staged_
        && !native_region_completion_storage_
        && native_region_completion_bindings_.empty()
        && native_region_completion_registers_.empty()
        && native_region_completion_generation_ == 0U
        && validate_region_completion_native(process, wait_instruction,
            jump_instruction, register_bindings, activation_register_count,
            true);
}

bool LlvmProcessExecutor::prepare_region_completion_native(
    const runtime::simir::ProcessId process,
    const runtime::simir::InstructionIndex wait_instruction,
    const runtime::simir::InstructionIndex jump_instruction,
    const std::span<const runtime::simir::ProcessExecutor::RegionRegisterBinding>
        register_bindings,
    const std::size_t activation_register_count,
    const void** const storage_identity) noexcept
{
    if (storage_identity == nullptr || native_region_completion_prepared_
        || !validate_region_completion_native(process, wait_instruction,
            jump_instruction, register_bindings, activation_register_count,
            false)) {
        return false;
    }

    native_region_completion_storage_ = storage_;
    native_region_completion_bindings_ = register_bindings;
    native_region_completion_registers_ = { };
    native_region_wait_instruction_ = wait_instruction;
    native_region_jump_instruction_ = jump_instruction;
    native_region_completion_generation_ = instance_generation_;
    native_region_completion_prepared_ = true;
    native_region_completion_staged_ = false;
    *storage_identity = native_region_completion_storage_.get();
    return true;
}

bool LlvmProcessExecutor::stage_region_completion_native(
    const std::span<const PackedLogic4> activation_registers) noexcept
{
    using runtime::simir::ValueKind;
    if (!native_region_completion_prepared_
        || native_region_completion_staged_
        || native_region_completion_generation_ != instance_generation_
        || native_region_completion_storage_.get() != storage_.get()) {
        return false;
    }
    for (const auto& binding : native_region_completion_bindings_) {
        if (!binding.defined) {
            continue;
        }
        if (binding.activation_register >= activation_registers.size()) {
            return false;
        }
        const auto& value = activation_registers[binding.activation_register];
        if (value.width() != binding.width
            || value.is_logic9() != (binding.value_kind == ValueKind::logic9)
            || (binding.value_kind == ValueKind::logic9
                && !layout_.uses_logic9)) {
            return false;
        }
        const auto words = (static_cast<std::size_t>(binding.width) + 63U) / 64U;
        if (binding.value_kind == ValueKind::logic9) {
            for (std::size_t plane = 0U; plane < 4U; ++plane) {
                if (value.logic9_plane_words(plane).size() != words) {
                    return false;
                }
            }
        } else if (value.aval_words().size() != words
            || value.bval_words().size() != words) {
            return false;
        }
    }
    native_region_completion_registers_ = activation_registers;
    native_region_completion_staged_ = true;
    return true;
}

void LlvmProcessExecutor::commit_region_completion_native() noexcept
{
    using runtime::simir::ValueKind;
    if (!native_region_completion_staged_
        || native_region_completion_generation_ != instance_generation_
        || native_region_completion_storage_.get() != storage_.get()) {
        std::terminate();
    }
    for (const auto& binding : native_region_completion_bindings_) {
        if (!binding.defined
            || layout_.register_values_persistent[
                binding.source_register] == 0U) {
            continue;
        }
        const auto source = static_cast<std::size_t>(binding.source_register);
        const auto& value = native_region_completion_registers_[
            binding.activation_register];
        const auto words = (static_cast<std::size_t>(binding.width) + 63U) / 64U;
        const auto offset = layout_.register_word_offsets[source];
        std::array<std::span<const std::uint64_t>, 4U> planes;
        if (binding.value_kind == ValueKind::logic9) {
            for (std::size_t plane = 0U; plane < planes.size(); ++plane) {
                planes[plane] = value.logic9_plane_words(plane);
            }
        } else {
            planes[0] = value.aval_words();
            planes[1] = value.bval_words();
        }
        for (std::size_t word = 0U; word < words; ++word) {
            auto aval = planes[0][word];
            auto bval = planes[1][word];
            auto plane2 = binding.value_kind == ValueKind::logic9
                ? planes[2][word] : UINT64_C(0);
            auto plane3 = binding.value_kind == ValueKind::logic9
                ? planes[3][word] : UINT64_C(0);
            if (word + 1U == words && (binding.width % 64U) != 0U) {
                const auto mask
                    = (UINT64_C(1) << (binding.width % 64U)) - 1U;
                aval &= mask;
                bval &= mask;
                plane2 &= mask;
                plane3 &= mask;
            }
            const auto destination = offset + word;
            register_aval_[destination] = aval;
            register_bval_[destination] = bval;
            if (layout_.uses_logic9) {
                register_logic9_plane2_[destination] = plane2;
                register_logic9_plane3_[destination] = plane3;
            }
        }
        if (layout_.tracks_register_initialization) {
            register_initialized_[source] = 1U;
        }
    }
    frame_.program_counter = native_region_jump_instruction_;
    frame_.state = FSIM_JIT_FRAME_STATE_READY_V2;
    frame_.last_instruction = native_region_wait_instruction_;
    cancel_region_completion_native();
}

void LlvmProcessExecutor::cancel_region_completion_native() noexcept
{
    native_region_completion_storage_.reset();
    native_region_completion_bindings_ = { };
    native_region_completion_registers_ = { };
    native_region_wait_instruction_ = 0U;
    native_region_jump_instruction_ = 0U;
    native_region_completion_generation_ = 0U;
    native_region_completion_prepared_ = false;
    native_region_completion_staged_ = false;
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
            start_instruction, program_access_binding_ }
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
    frame_.state = FSIM_JIT_FRAME_STATE_READY_V2;
    frame_.last_instruction = FSIM_JIT_INVALID_INSTRUCTION_V2;
}

#endif

} // namespace fsim::app::application_detail
