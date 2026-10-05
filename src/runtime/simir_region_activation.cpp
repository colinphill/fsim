// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/simir_region_activation.hpp"

#include <algorithm>
#include <cassert>
#include <limits>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace fsim::runtime::simir {
namespace {

bool valid_value_kind(const ValueKind kind, const PackedLogic4& value)
{
    switch (kind) {
    case ValueKind::logic4:
        return !value.is_logic9();
    case ValueKind::logic9:
        return value.is_logic9();
    }
    return false;
}

PackedLogic4 ready_bit(const bool ready)
{
    return PackedLogic4::from_aval_bval(
        1U, ready ? UINT64_C(1) : UINT64_C(0), UINT64_C(0));
}

} // namespace

namespace {

std::vector<RegionKernelInternalSeed> make_initial_internal_seed(
    const RegionConeActivationKernel& kernel)
{
    std::vector<RegionKernelInternalSeed> seed;
    seed.reserve(kernel.internal_signals.size());
    for (const auto signal : kernel.internal_signals) {
        const auto output = std::ranges::find(kernel.outputs, signal,
            &RegionConeOutputBinding::signal);
        if (output == kernel.outputs.end()) {
            throw std::invalid_argument {
                "region activation internal signal has no output binding"
            };
        }
        PackedLogic4 value(output->width, Logic4::x);
        if (output->value_kind == ValueKind::logic9) {
            for (std::size_t bit = 0U; bit < value.width(); ++bit) {
                value.set_logic9(bit, Logic9::x);
            }
        }
        seed.push_back({ signal, output->owner, value, value, value });
    }
    return seed;
}

} // namespace

RegionKernelActivationState::RegionKernelActivationState(
    const RegionConeActivationKernel& kernel)
    : RegionKernelActivationState(kernel, make_initial_internal_seed(kernel))
{
}

RegionKernelActivationState::RegionKernelActivationState(
    const RegionConeActivationKernel& kernel,
    const std::span<const RegionKernelInternalSeed> internal_seed)
    : kernel_(&kernel)
{
    const auto register_value_kinds
        = process_layout_detail::ProcessLayoutAccess::view(
            kernel.program.register_value_kinds);
    if (register_value_kinds.size()
        != kernel.program.register_count) {
        throw std::invalid_argument {
            "region activation kernel has an incomplete register-kind map"
        };
    }
    if (kernel.program.scheduling_domain
            != ProcessSchedulingDomain::systemverilog
        && kernel.program.scheduling_domain
            != ProcessSchedulingDomain::generic) {
        throw std::invalid_argument {
            "region activation kernel has an unsupported scheduler domain"
        };
    }

    std::vector<bool> register_claimed(kernel.program.register_count, false);
    for (const auto& input : kernel.inputs) {
        const bool is_internal = std::ranges::find(kernel.internal_signals,
            input.signal) != kernel.internal_signals.end();
        if (input.value_register >= register_claimed.size()
            || register_claimed[input.value_register]
            || register_value_kinds[input.value_register]
                != input.value_kind
            || input.internal != is_internal || input.width == 0U) {
            throw std::invalid_argument {
                "region activation input register is duplicated or mistyped"
            };
        }
        register_claimed[input.value_register] = true;
        const auto output = std::ranges::find(kernel.outputs, input.signal,
            &RegionConeOutputBinding::signal);
        const auto output_signal_width = output == kernel.outputs.end()
            ? 0U
            : output->signal_width != 0U
                ? output->signal_width
                : output->offset == 0U ? output->width : 0U;
        const auto compared_output_width
            = kernel.program.scheduling_domain
                    == ProcessSchedulingDomain::systemverilog
            ? output_signal_width
            : output == kernel.outputs.end() ? 0U : output->width;
        if (output != kernel.outputs.end()
            && (compared_output_width != input.width
                || output->value_kind != input.value_kind)) {
            throw std::invalid_argument {
                "region activation input and output shapes disagree"
            };
        }
    }
    for (const auto& member : kernel.members) {
        if (member.readiness_register >= register_claimed.size()
            || register_claimed[member.readiness_register]
            || register_value_kinds[member.readiness_register]
                != ValueKind::logic4) {
            throw std::invalid_argument {
                "region activation readiness register is duplicated or mistyped"
            };
        }
        register_claimed[member.readiness_register] = true;
        if (member.branch_instruction >= kernel.program.operations.size()) {
            throw std::invalid_argument {
                "region activation member branch is outside the kernel"
            };
        }
        const auto branch_operation
            = kernel.program.operations.expanded(member.branch_instruction);
        const auto* branch = operation_get_if<Branch>(&branch_operation);
        if (member.begin >= member.end
            || member.end > kernel.program.operations.size()
            || member.branch_instruction >= member.end
            || member.begin != member.branch_instruction + 1U
            || branch == nullptr
            || branch->condition != member.readiness_register
            || branch->when_true != member.begin
            || branch->when_false != member.end) {
            throw std::invalid_argument {
                "region activation member instruction span is invalid"
            };
        }
    }

    if (internal_seed.size() != kernel.internal_signals.size()) {
        throw std::invalid_argument {
            "region activation internal seed does not match the kernel"
        };
    }
    internal_.reserve(kernel.internal_signals.size());
    for (const auto signal : kernel.internal_signals) {
        const auto binding = std::ranges::find(kernel.outputs, signal,
            &RegionConeOutputBinding::signal);
        const auto seed = std::ranges::find(internal_seed, signal,
            &RegionKernelInternalSeed::signal);
        if (binding == kernel.outputs.end() || seed == internal_seed.end()
            || seed->owner != binding->owner
            || std::ranges::count(kernel.internal_signals, signal) != 1) {
            throw std::invalid_argument {
                "region activation internal signal has no unique owner seed"
            };
        }
        require_value_shape(signal, binding->value_kind, seed->current);
        require_value_shape(signal, binding->value_kind, seed->previous);
        require_value_shape(signal, binding->value_kind, seed->raw_driver);
        internal_.push_back({ signal, seed->owner, binding->value_kind,
            seed->current, seed->previous, seed->raw_driver });
    }
    if (std::ranges::any_of(internal_seed, [&](const auto& seed) {
            return std::ranges::find(kernel.internal_signals, seed.signal)
                == kernel.internal_signals.end();
        })) {
        throw std::invalid_argument {
            "region activation seed contains an unrelated signal"
        };
    }

    std::vector<ProcessId> member_ids;
    member_ids.reserve(kernel.members.size());
    for (const auto& member : kernel.members) {
        member_ids.push_back(member.process);
    }
    std::ranges::sort(member_ids);
    if (std::ranges::adjacent_find(member_ids) != member_ids.end()) {
        throw std::invalid_argument {
            "region activation kernel has duplicate process members"
        };
    }
    std::optional<RegionUpdateKind> generic_output_update_kind;
    for (const auto& output : kernel.outputs) {
        const bool output_contract_matches
            = kernel.program.scheduling_domain
                    == ProcessSchedulingDomain::systemverilog
            ? output.update_kind == RegionUpdateKind::systemverilog_active
                && output.domain == SignalUpdateDomain::systemverilog_active
            : output.update_kind == RegionUpdateKind::vhdl_projected
                && output.domain == SignalUpdateDomain::generic
                && output.projected_mode == ProjectedDelayMode::inertial
                && output.projected_delay == 0U
                && output.projected_rejection == 0U;
        const bool generic_update_output
            = kernel.program.scheduling_domain
                    == ProcessSchedulingDomain::generic
            && output.update_kind == RegionUpdateKind::generic
            && output.domain == SignalUpdateDomain::generic
            && output.projected_mode == ProjectedDelayMode::inertial
            && output.projected_delay == 0U
            && output.projected_rejection == 0U;
        const bool internal_output
            = std::ranges::find(kernel.internal_signals, output.signal)
                != kernel.internal_signals.end();
        const auto output_signal_width = output.signal_width != 0U
            ? output.signal_width
            : output.offset == 0U ? output.width : 0U;
        const bool output_signal_shape_valid
            = kernel.program.scheduling_domain
                    != ProcessSchedulingDomain::systemverilog
            || (output_signal_width != 0U
                && output.offset <= output_signal_width
                && output.width <= output_signal_width - output.offset);
        const bool internal_output_shape_valid
            = !internal_output
            || (output.offset == 0U
                && output.width == output_signal_width);
        const bool partial_systemverilog_boundary_output
            = kernel.program.scheduling_domain
                    == ProcessSchedulingDomain::systemverilog
            && output_contract_matches && !internal_output
            && output.signal_width != 0U
            && (output.offset != 0U
                || output.width != output_signal_width);
        if (kernel.program.scheduling_domain
                == ProcessSchedulingDomain::generic
            && (output_contract_matches || generic_update_output)) {
            if (generic_output_update_kind
                && *generic_output_update_kind != output.update_kind) {
                throw std::invalid_argument {
                    "region activation kernel mixes generic update contracts"
                };
            }
            generic_output_update_kind = output.update_kind;
        }
        if (member_index(output.owner) >= kernel.members.size()
            || output.value_register >= kernel.program.register_count
            || register_claimed[output.value_register]
            || register_value_kinds[output.value_register]
                != output.value_kind
            || output.width == 0U
            || output.offset
                > std::numeric_limits<std::uint32_t>::max() - output.width
            || !output_signal_shape_valid
            || !internal_output_shape_valid
            || (output.offset != 0U && !generic_update_output
                && !partial_systemverilog_boundary_output)
            || output.kernel_instruction
                >= kernel.program.operations.size()
            || (!output_contract_matches && !generic_update_output)) {
            throw std::invalid_argument {
                "region activation output binding is invalid"
            };
        }
        const auto output_operation = kernel.program.operations.expanded(
            output.kernel_instruction);
        const auto* output_copy = operation_get_if<CopyRegister>(
            &output_operation);
        if (output_copy == nullptr
            || output_copy->destination != output.value_register) {
            throw std::invalid_argument {
                "region activation output is not a dedicated snapshot"
            };
        }
        register_claimed[output.value_register] = true;
        if (internal_output && std::ranges::count_if(kernel.outputs,
                [&](const auto& candidate) {
                    return candidate.signal == output.signal;
                }) != 1) {
            throw std::invalid_argument {
                "region activation internal output is not uniquely owned"
            };
        }
    }
    reserve_kernel_storage();
}

void RegionKernelActivationState::reserve_kernel_storage()
{
    active_image_.emplace();
    active_image_->scheduler_prefix.tasks.reserve(kernel_->members.size());
    active_image_->ready_processes.reserve(kernel_->members.size());
    active_image_->requests.reserve(kernel_->members.size());
    active_image_->active_member_indices.reserve(kernel_->members.size());
    active_image_->register_inputs.reserve(
        kernel_->inputs.size() + kernel_->members.size());
    active_members_.reserve(kernel_->members.size());
    pending_publications_.reserve(kernel_->outputs.size());
    active_publication_bindings_.reserve(kernel_->outputs.size());
    member_publication_offsets_.assign(kernel_->members.size() + 1U, 0U);
    for (const auto& output : kernel_->outputs) {
        ++member_publication_offsets_[member_index(output.owner) + 1U];
    }
    for (std::size_t member = 1U;
         member < member_publication_offsets_.size(); ++member) {
        member_publication_offsets_[member]
            += member_publication_offsets_[member - 1U];
    }
    member_publication_indices_.resize(kernel_->outputs.size());
    auto cursors = member_publication_offsets_;
    for (std::size_t output = 0U; output < kernel_->outputs.size(); ++output) {
        const auto member = member_index(kernel_->outputs[output].owner);
        member_publication_indices_[cursors[member]++] = output;
    }
    for (std::size_t member = 0U; member < kernel_->members.size(); ++member) {
        auto first = member_publication_indices_.begin()
            + static_cast<std::ptrdiff_t>(member_publication_offsets_[member]);
        auto last = member_publication_indices_.begin()
            + static_cast<std::ptrdiff_t>(member_publication_offsets_[member + 1U]);
        std::sort(first, last, [&](const std::size_t left,
                                   const std::size_t right) {
            return kernel_->outputs[left].source_instruction
                < kernel_->outputs[right].source_instruction;
        });
    }
}

void RegionKernelActivationState::reset_internal_state(
    const std::span<const RegionKernelInternalSeed> internal_seed)
{
    if (wave_active_) {
        throw std::logic_error {
            "region internal state reset during an active wave"
        };
    }

    if (internal_seed.size() != internal_.size()) {
        throw std::logic_error {
            "region internal state reset with a bad seed count"
        };
    }

    // Validate the complete replacement before changing any activation row.
    // Activation internals are never live A4 views: construction copies seeds,
    // and later publication paths copy or move PackedLogic4 values into this
    // private state. Those PackedLogic4 operations turn live A4 inputs into
    // immutable snapshots instead of retaining mutable backing.
    for (const auto& state : internal_) {
        const auto seed = std::ranges::find(internal_seed, state.signal,
            &RegionKernelInternalSeed::signal);
        if (seed == internal_seed.end() || seed->owner != state.owner) {
            throw std::invalid_argument {
                "region activation seed changed its internal owner"
            };
        }
        require_value_shape(state.signal, state.value_kind, seed->current);
        require_value_shape(state.signal, state.value_kind, seed->previous);
        require_value_shape(state.signal, state.value_kind, seed->raw_driver);
    }

    // `internal_` is constructed from PackedLogic4 values and every later
    // update moves values into it. The state is never a live A4 view. In
    // particular, copying the prepared A2 wide owning seeds shares their
    // existing WideStorage and cannot allocate or write through an A4 slot.
    // Keep the commit loop below free of shape/owner checks: every recoverable
    // validation failure has already returned with all rows unchanged.
    for (auto& state : internal_) {
        const auto seed = std::ranges::find(internal_seed, state.signal,
            &RegionKernelInternalSeed::signal);
        assert(seed != internal_seed.end() && seed->owner == state.owner);
        state.current = seed->current;
        state.previous = seed->previous;
        state.raw_driver = seed->raw_driver;
    }
}

bool RegionKernelActivationState::can_publish_internal_update(
    const SignalId signal,
    const ProcessId owner,
    const std::uint32_t width) const noexcept
{
    const auto state = std::ranges::find(internal_, signal,
        &RegionKernelInternalState::signal);
    return state != internal_.end() && state->owner == owner
        && state->value_kind == ValueKind::logic4
        && width != 0U
        && state->current.width() == width
        && state->previous.width() == width
        && state->raw_driver.width() == width
        && !state->current.is_logic9()
        && !state->previous.is_logic9()
        && !state->raw_driver.is_logic9();
}

void RegionKernelActivationState::publish_internal_update(
    const SignalId signal,
    const ProcessId owner,
    PackedLogic4&& current,
    PackedLogic4&& raw_driver,
    const bool changed) noexcept
{
    const auto found = std::ranges::find(internal_, signal,
        &RegionKernelInternalState::signal);
    assert(found != internal_.end() && found->owner == owner
        && can_publish_internal_update(signal, owner,
            static_cast<std::uint32_t>(current.width()))
        && current.width() == raw_driver.width()
        && !current.is_logic9() && !raw_driver.is_logic9());
    if (found == internal_.end() || found->owner != owner) {
        return;
    }
    if (changed) {
        found->previous = std::move(found->current);
    }
    found->current = std::move(current);
    found->raw_driver = std::move(raw_driver);
}

RegionKernelActivationImage RegionKernelActivationState::begin_wave(
    const RegionKernelSchedulerPrefix& scheduler_prefix,
    const std::span<const PackedLogic4> current_boundary_inputs,
    const std::span<const std::uint64_t> active_member_mask)
{
    const auto previous_generation = generation_;
    const auto previous_frontier_generation = last_frontier_generation_;
    const auto previous_frontier_extent = last_frontier_extent_;
    const auto previous_prefix_end = last_prefix_end_;
    try {
        return begin_wave_reusable(scheduler_prefix,
            current_boundary_inputs, active_member_mask);
    } catch (...) {
        // The by-value image copy can fail after the reusable wave committed.
        // Roll back only this newly committed wave; a pre-existing active wave
        // rejected by begin_wave_reusable has not advanced generation_.
        if (generation_ != previous_generation) {
            discard_wave();
            generation_ = previous_generation;
            last_frontier_generation_ = previous_frontier_generation;
            last_frontier_extent_ = previous_frontier_extent;
            last_prefix_end_ = previous_prefix_end;
        }
        throw;
    }
}

const RegionKernelActivationImage&
RegionKernelActivationState::begin_wave_reusable(
    const RegionKernelSchedulerPrefix& scheduler_prefix,
    const std::span<const PackedLogic4> current_boundary_inputs,
    const std::span<const std::uint64_t> active_member_mask)
{
    return begin_wave_reusable_impl(scheduler_prefix,
        current_boundary_inputs, active_member_mask, true);
}

const RegionKernelActivationImage&
RegionKernelActivationState::begin_direct_wave_reusable(
    const RegionKernelSchedulerPrefix& scheduler_prefix,
    const std::span<const PackedLogic4> current_boundary_inputs,
    const std::span<const std::uint64_t> active_member_mask)
{
    return begin_wave_reusable_impl(scheduler_prefix,
        current_boundary_inputs, active_member_mask, false);
}

const RegionKernelActivationImage&
RegionKernelActivationState::begin_wave_reusable_impl(
    const RegionKernelSchedulerPrefix& scheduler_prefix,
    const std::span<const PackedLogic4> current_boundary_inputs,
    const std::span<const std::uint64_t> active_member_mask,
    const bool include_input_values)
{
    if (wave_active_ || outputs_staged_ || !active_members_.empty()
        || !pending_publications_.empty()) {
        throw std::logic_error {
            "region activation wave began before prior publications completed"
        };
    }
    if (scheduler_prefix.tasks.empty()
        || scheduler_prefix.tasks.size() > kernel_->members.size()
        || scheduler_prefix.frontier_cursor >= scheduler_prefix.frontier_end
        || scheduler_prefix.tasks.size()
            > scheduler_prefix.frontier_end - scheduler_prefix.frontier_cursor) {
        throw std::invalid_argument {
            "region activation scheduler prefix has invalid bounds"
        };
    }
    if (scheduler_prefix.process_domain
            != kernel_->program.scheduling_domain
        || (scheduler_prefix.process_domain
                == ProcessSchedulingDomain::systemverilog
            && scheduler_prefix.phase != SchedulerPhase::active)
        || (scheduler_prefix.process_domain
                == ProcessSchedulingDomain::generic
            && scheduler_prefix.systemverilog_round != 0U)) {
        throw std::invalid_argument {
            "region activation prefix uses the wrong scheduler domain"
        };
    }
    if (last_frontier_generation_
        && (scheduler_prefix.frontier_generation < *last_frontier_generation_
            || (scheduler_prefix.frontier_generation
                    == *last_frontier_generation_
                && (scheduler_prefix.frontier_end != last_frontier_extent_
                    || scheduler_prefix.frontier_cursor < last_prefix_end_)))) {
        throw std::invalid_argument {
            "region activation scheduler prefix is stale or rewound"
        };
    }
    if (generation_ == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error { "region activation generation exhausted" };
    }
    if (!active_member_mask.empty()) {
        const auto expected_words = (kernel_->members.size() + 63U) / 64U;
        if (active_member_mask.size() != expected_words) {
            throw std::invalid_argument {
                "region activation readiness mask has an invalid size"
            };
        }
        const auto remainder = kernel_->members.size() % 64U;
        if (remainder != 0U
            && (active_member_mask.back()
                & (~UINT64_C(0) << remainder)) != 0U) {
            throw std::invalid_argument {
                "region activation readiness mask has bits outside the kernel"
            };
        }
    }

    try {
        auto& image = *active_image_;
        image.scheduler_prefix = scheduler_prefix;
        image.ready_processes.clear();
        image.requests.clear();
        image.active_member_indices.clear();
        image.register_inputs.clear();
        active_members_.clear();
        active_publication_bindings_.clear();
        std::optional<std::tuple<StableOrder, std::uint64_t>> previous_key;
        for (std::size_t index = 0U; index < scheduler_prefix.tasks.size();
             ++index) {
            const auto& task = scheduler_prefix.tasks[index];
            const auto& request = task.member;
            if (task.task_ordinal != scheduler_prefix.frontier_cursor + index) {
                throw std::invalid_argument {
                    "region activation scheduler tasks are not a contiguous prefix"
                };
            }
            if (request.trigger_mask == 0U
                || request.origin.process_domain
                    != scheduler_prefix.process_domain
                || request.origin.phase != SchedulerPhase::active
                || request.origin.time != scheduler_prefix.time
                || request.origin.delta != scheduler_prefix.delta
                || request.origin.phase != scheduler_prefix.phase
                || request.origin.systemverilog_round
                    != scheduler_prefix.systemverilog_round
                || (scheduler_prefix.process_domain
                        == ProcessSchedulingDomain::generic
                    && request.origin.systemverilog_round != 0U)) {
                throw std::invalid_argument {
                    "region activation request disagrees with its scheduler frontier"
                };
            }
            const auto key = std::tuple<StableOrder, std::uint64_t> {
                request.origin.stable_order, request.origin.sequence };
            if (previous_key && !(*previous_key < key)) {
                throw std::invalid_argument {
                    "region activation prefix is not in scheduler order"
                };
            }
            previous_key = key;
            const auto member = member_index(request.process);
            if (std::ranges::any_of(active_members_, [&](const auto& active) {
                    return active.member_index == member;
                })) {
                throw std::invalid_argument {
                    "region activation wave contains a process more than once"
                };
            }
            active_members_.push_back({ request, member });
            image.ready_processes.push_back(request.process);
            image.requests.push_back(request);
            image.active_member_indices.push_back(member);
        }
        std::ranges::sort(image.active_member_indices);

        const auto boundary_count = static_cast<std::size_t>(std::ranges::count_if(
            kernel_->inputs, [](const RegionConeKernelInput& input) {
                return !input.internal;
            }));
        if (current_boundary_inputs.size() != boundary_count) {
            throw std::invalid_argument {
                "region activation boundary snapshot count is invalid"
            };
        }

        image.register_inputs.reserve(
            kernel_->inputs.size() + kernel_->members.size());
        std::size_t boundary_index { };
        for (const auto& input : kernel_->inputs) {
            const auto& value = input.internal
                ? internal_state(input.signal).current
                : current_boundary_inputs[boundary_index++];
            require_value_shape(input.signal, input.value_kind, value);
            if (include_input_values) {
                image.register_inputs.push_back({ input.value_register, value });
            }
        }
        for (std::size_t member = 0U; member < kernel_->members.size(); ++member) {
            const bool prefix_active = std::ranges::binary_search(
                image.active_member_indices, member);
            const bool active = active_member_mask.empty()
                ? prefix_active
                : (active_member_mask[member / 64U]
                       & (UINT64_C(1) << (member % 64U))) != 0U;
            if (active != prefix_active) {
                throw std::invalid_argument {
                    "region activation readiness mask disagrees with its scheduler prefix"
                };
            }
            if (include_input_values) {
                image.register_inputs.push_back({
                    kernel_->members[member].readiness_register,
                    ready_bit(active) });
            }
        }
        std::ranges::sort(image.register_inputs, std::ranges::less { },
            &RegionKernelRegisterInput::register_id);

        image.generation = generation_ + 1U;
        expected_publication_count_ = 0U;
        for (const auto& active : active_members_) {
            const auto begin = member_publication_offsets_[active.member_index];
            const auto end = member_publication_offsets_[active.member_index + 1U];
            expected_publication_count_ += end - begin;
            for (auto position = begin; position < end; ++position) {
                active_publication_bindings_.push_back(&kernel_->outputs[
                    member_publication_indices_[position]]);
            }
        }
        wave_active_ = true;
        input_values_materialized_ = include_input_values;
        ++generation_;
        last_frontier_generation_ = scheduler_prefix.frontier_generation;
        last_frontier_extent_ = scheduler_prefix.frontier_end;
        last_prefix_end_ = scheduler_prefix.frontier_cursor
            + scheduler_prefix.tasks.size();
        return image;
    } catch (...) {
        // A reusable activation image is staged in-place. Restore its empty
        // state if validation or value copying fails before the wave starts.
        discard_wave();
        throw;
    }
}

void RegionKernelActivationState::materialize_wave_inputs(
    const std::span<const PackedLogic4> current_boundary_inputs)
{
    if (!wave_active_ || input_values_materialized_ || !active_image_) {
        throw std::logic_error {
            "region activation inputs materialized outside a direct wave"
        };
    }
    const auto boundary_count = static_cast<std::size_t>(std::ranges::count_if(
        kernel_->inputs, [](const RegionConeKernelInput& input) {
            return !input.internal;
        }));
    if (current_boundary_inputs.size() != boundary_count) {
        throw std::invalid_argument {
            "region activation boundary snapshot count is invalid"
        };
    }

    auto& image = *active_image_;
    auto& complete_inputs = image.register_inputs;
    complete_inputs.clear();
    try {
        complete_inputs.reserve(kernel_->inputs.size() + kernel_->members.size());
        for (std::size_t member = 0U; member < kernel_->members.size(); ++member) {
            const bool active = std::ranges::binary_search(
                image.active_member_indices, member);
            complete_inputs.push_back({
                kernel_->members[member].readiness_register,
                ready_bit(active) });
        }
        std::size_t boundary_index { };
        for (const auto& input : kernel_->inputs) {
            const auto& value = input.internal
                ? internal_state(input.signal).current
                : current_boundary_inputs[boundary_index++];
            require_value_shape(input.signal, input.value_kind, value);
            complete_inputs.push_back({ input.value_register, value });
        }
        std::ranges::sort(complete_inputs, std::ranges::less { },
            &RegionKernelRegisterInput::register_id);
        if (std::ranges::adjacent_find(complete_inputs,
                [](const RegionKernelRegisterInput& left,
                    const RegionKernelRegisterInput& right) {
                    return left.register_id == right.register_id;
                }) != complete_inputs.end()) {
            throw std::logic_error {
                "region activation direct input mapping has duplicate registers"
            };
        }
    } catch (...) {
        complete_inputs.clear();
        throw;
    }
    input_values_materialized_ = true;
}

void RegionKernelActivationState::stage_kernel_outputs(
    const RegionKernelActivationImage& image,
    const std::span<const PackedLogic4> registers)
{
    if (outputs_staged_ || !wave_active_ || active_image_->generation != image.generation
        || active_image_->scheduler_prefix.frontier_generation
            != image.scheduler_prefix.frontier_generation
        || active_members_.empty()
        || registers.size() < kernel_->program.register_count) {
        throw std::logic_error {
            "region activation output staging does not match its active wave"
        };
    }

    pending_publications_.clear();
    for (const auto& active : active_members_) {
        const auto begin = member_publication_offsets_[active.member_index];
        const auto end = member_publication_offsets_[active.member_index + 1U];
        for (auto position = begin; position < end; ++position) {
            const auto& binding = kernel_->outputs[
                member_publication_indices_[position]];
            const auto& value = registers[binding.value_register];
            if (value.width() != binding.width
                || !valid_value_kind(binding.value_kind, value)) {
                throw std::invalid_argument {
                    "region activation output snapshot has an invalid shape"
                };
            }
            pending_publications_.push_back({
                binding, value, active.request.origin });
        }
    }
    if (pending_publications_.size() != expected_publication_count_) {
        throw std::logic_error {
            "region activation publication count changed after preflight"
        };
    }
    outputs_staged_ = true;
    next_publication_ = 0U;
    publication_raw_started_ = false;
}

void RegionKernelActivationState::begin_raw_publication(
    const std::size_t index)
{
    if (!outputs_staged_ || index != next_publication_
        || index >= pending_publications_.size()
        || publication_raw_started_) {
        throw std::logic_error {
            "region raw-driver publications are out of scheduler order"
        };
    }
    const auto& pending = pending_publications_[index];
    if (std::ranges::find(kernel_->internal_signals, pending.binding.signal)
        != kernel_->internal_signals.end()) {
        auto& state = internal_.at(internal_index(pending.binding.signal));
        if (state.owner != pending.binding.owner) {
            throw std::logic_error {
                "region internal write changed its original owner"
            };
        }
        auto raw_driver = pending.value;
        state.raw_driver = std::move(raw_driver);
    }
    publication_raw_started_ = true;
}

void RegionKernelActivationState::publish_current(
    const std::size_t index, const PackedLogic4& resolved_value)
{
    if (!outputs_staged_ || index != next_publication_
        || index >= pending_publications_.size()
        || !publication_raw_started_) {
        throw std::logic_error {
            "region current publication was not preceded by its raw stage"
        };
    }
    const auto& pending = pending_publications_[index];
    if (resolved_value.width() != pending.binding.width
        || !valid_value_kind(pending.binding.value_kind, resolved_value)) {
        throw std::invalid_argument {
            "region activation resolved output has an invalid shape"
        };
    }
    if (std::ranges::find(kernel_->internal_signals, pending.binding.signal)
        != kernel_->internal_signals.end()) {
        auto& state = internal_.at(internal_index(pending.binding.signal));
        if (state.current != resolved_value) {
            auto previous = state.current;
            auto current = resolved_value;
            state.previous = std::move(previous);
            state.current = std::move(current);
        }
    }
    ++next_publication_;
    publication_raw_started_ = false;
}

void RegionKernelActivationState::complete_wave()
{
    if (!outputs_staged_ || publication_raw_started_
        || next_publication_ != pending_publications_.size()) {
        throw std::logic_error {
            "region activation completed before every captured publication"
        };
    }
    active_members_.clear();
    active_publication_bindings_.clear();
    pending_publications_.clear();
    active_image_->scheduler_prefix.tasks.clear();
    active_image_->ready_processes.clear();
    active_image_->requests.clear();
    active_image_->active_member_indices.clear();
    active_image_->register_inputs.clear();
    expected_publication_count_ = 0U;
    next_publication_ = 0U;
    outputs_staged_ = false;
    wave_active_ = false;
    input_values_materialized_ = false;
}

void RegionKernelActivationState::discard_wave() noexcept
{
    active_members_.clear();
    active_publication_bindings_.clear();
    pending_publications_.clear();
    if (active_image_) {
        active_image_->scheduler_prefix.tasks.clear();
        active_image_->ready_processes.clear();
        active_image_->requests.clear();
        active_image_->active_member_indices.clear();
        active_image_->register_inputs.clear();
    }
    expected_publication_count_ = 0U;
    next_publication_ = 0U;
    publication_raw_started_ = false;
    outputs_staged_ = false;
    wave_active_ = false;
    input_values_materialized_ = false;
}

const RegionKernelInternalState& RegionKernelActivationState::internal_state(
    const SignalId signal) const
{
    return internal_.at(internal_index(signal));
}

std::size_t RegionKernelActivationState::member_index(
    const ProcessId process) const
{
    const auto found = std::ranges::find(kernel_->members, process,
        &RegionConeKernelMember::process);
    if (found == kernel_->members.end()) {
        throw std::invalid_argument {
            "region activation references a nonmember process"
        };
    }
    return static_cast<std::size_t>(found - kernel_->members.begin());
}

std::size_t RegionKernelActivationState::internal_index(
    const SignalId signal) const
{
    const auto found = std::ranges::find(internal_, signal,
        &RegionKernelInternalState::signal);
    if (found == internal_.end()) {
        throw std::invalid_argument {
            "region activation references a noninternal signal"
        };
    }
    return static_cast<std::size_t>(found - internal_.begin());
}

void RegionKernelActivationState::require_value_shape(
    const SignalId signal,
    const ValueKind kind,
    const PackedLogic4& value) const
{
    const auto binding = std::ranges::find(kernel_->outputs, signal,
        &RegionConeOutputBinding::signal);
    std::size_t width { };
    if (binding != kernel_->outputs.end()) {
        width = binding->width;
    } else {
        const auto input = std::ranges::find(kernel_->inputs, signal,
            &RegionConeKernelInput::signal);
        if (input == kernel_->inputs.end()) {
            throw std::invalid_argument {
                "region activation value has no signal binding"
            };
        }
        width = input->width;
    }
    if (value.width() != width || !valid_value_kind(kind, value)) {
        throw std::invalid_argument {
            "region activation signal value has the wrong width or value kind"
        };
    }
}

} // namespace fsim::runtime::simir
