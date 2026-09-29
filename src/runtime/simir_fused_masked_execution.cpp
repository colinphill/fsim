// SPDX-License-Identifier: Apache-2.0
#include "simir_execution_context.hpp"
#include "simir_internal.hpp"

#include <algorithm>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <vector>

namespace fsim::runtime::simir {

void Interpreter::Impl::FusedMaskedGlobalFrontier::sift_up(
    std::size_t index) noexcept
{
    while (index != 0U) {
        const auto parent = (index - 1U) / 2U;
        if (!(heads[index].key < heads[parent].key)) {
            break;
        }
        std::swap(heads[index], heads[parent]);
        positions[heads[index].region] = index;
        positions[heads[parent].region] = parent;
        index = parent;
    }
}

void Interpreter::Impl::FusedMaskedGlobalFrontier::sift_down(
    std::size_t index) noexcept
{
    while (index < heads.size()) {
        const auto left = index * 2U + 1U;
        if (left >= heads.size()) {
            break;
        }
        const auto right = left + 1U;
        const auto child = right < heads.size()
                && heads[right].key < heads[left].key
            ? right : left;
        if (!(heads[child].key < heads[index].key)) {
            break;
        }
        std::swap(heads[index], heads[child]);
        positions[heads[index].region] = index;
        positions[heads[child].region] = child;
        index = child;
    }
}

void Interpreter::Impl::FusedMaskedGlobalFrontier::set_head(
    const std::size_t region, const SchedulerOrderKey key)
{
    const auto none = std::numeric_limits<std::size_t>::max();
    if (region >= positions.size()) {
        positions.resize(region + 1U, none);
    }
    auto index = positions[region];
    if (index == none) {
        index = heads.size();
        heads.push_back(Head { key, region });
        positions[region] = index;
    } else {
        heads[index].key = key;
    }
    sift_up(index);
    sift_down(positions[region]);
}

void Interpreter::Impl::FusedMaskedGlobalFrontier::erase_head(
    const std::size_t region) noexcept
{
    const auto none = std::numeric_limits<std::size_t>::max();
    if (region >= positions.size() || positions[region] == none) {
        return;
    }
    const auto index = positions[region];
    positions[region] = none;
    if (index + 1U == heads.size()) {
        heads.pop_back();
        return;
    }
    heads[index] = heads.back();
    heads.pop_back();
    positions[heads[index].region] = index;
    sift_up(index);
    sift_down(positions[heads[index].region]);
}

std::optional<SchedulerOrderKey>
Interpreter::Impl::FusedMaskedGlobalFrontier::next_other_key() const noexcept
{
    if (heads.size() < 2U) {
        return std::nullopt;
    }
    if (heads.size() == 2U || heads[1U].key < heads[2U].key) {
        return heads[1U].key;
    }
    return heads[2U].key;
}

void Interpreter::Impl::schedule_fused_masked_frontier(
    const std::size_t bucket_index,
    const bool current_phase)
{
    auto& bucket = fused_masked_global_frontiers.at(bucket_index);
    if (bucket.heads.empty()) {
        return;
    }
    const auto key = bucket.heads.front().key;
    if (bucket.callback && bucket.callback_key == key) {
        return;
    }
    const auto callback = [this, bucket_index,
                           time = bucket.time,
                           delta = bucket.delta](Scheduler&) {
        execute_fused_masked_frontier(bucket_index, time, delta);
    };
    auto handle = current_phase
        ? scheduler.schedule_reserved_current_cancelable(
            SchedulerPhase::active, key, callback)
        : scheduler.schedule_reserved_next_delta_cancelable(
            SchedulerPhase::active, key, callback);
    if (bucket.callback) {
        scheduler.cancel(*bucket.callback);
    }
    bucket.callback = std::move(handle);
    bucket.callback_key = key;
    if (fused_masked_counters_enabled) {
        ++fused_masked_counts.global_frontier_queue_entries;
    }
}

bool Interpreter::Impl::queue_fused_masked_member(const ProcessId id)
{
    const auto none = std::numeric_limits<std::size_t>::max();
    if (id >= fused_masked_region_by_process.size()) {
        return false;
    }
    const auto region_id = fused_masked_region_by_process[id];
    if (region_id == none) {
        return false;
    }
    auto& region = fused_masked_regions[region_id];
    if (!region.certified || !region.executor) {
        return false;
    }
    if (scheduler.delta() == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error { "masked region delta overflow" };
    }
    const auto target_delta = scheduler.delta() + 1U;
    const auto bucket_index = static_cast<std::size_t>(target_delta & 1U);
    auto& bucket = region.ready_buckets[bucket_index];
    if (!bucket.ready.empty()
        && (bucket.time != scheduler.now()
            || bucket.delta != target_delta)) {
        throw std::logic_error {
            "masked region has an unconsumed delta bucket"
        };
    }
    bucket.time = scheduler.now();
    bucket.delta = target_delta;
    const auto key = scheduler.reserve_order_key(id);
    auto& global = fused_masked_global_frontiers[bucket_index];
    if (!global.heads.empty()
        && (global.time != scheduler.now()
            || global.delta != target_delta)) {
        throw std::logic_error {
            "masked global frontier has an unconsumed delta bucket"
        };
    }
    global.time = scheduler.now();
    global.delta = target_delta;
    const auto previous_head = bucket.ready.empty()
        ? std::nullopt
        : std::optional<SchedulerOrderKey> { bucket.ready.front().key };
    bucket.ready.push_back(FusedMaskedRegionPlan::Ready { id, key });
    std::ranges::sort(bucket.ready,
        [](const auto& left, const auto& right) {
            return left.key < right.key;
        });
    try {
        if (!previous_head || bucket.ready.front().key != *previous_head) {
            global.set_head(region_id, bucket.ready.front().key);
        }
        schedule_fused_masked_frontier(bucket_index, false);
    } catch (...) {
        const auto found = std::ranges::find(
            bucket.ready, id, &FusedMaskedRegionPlan::Ready::process);
        if (found != bucket.ready.end()) {
            bucket.ready.erase(found);
        }
        if (bucket.ready.empty()) {
            global.erase_head(region_id);
        } else {
            global.set_head(region_id, bucket.ready.front().key);
        }
        throw;
    }
    processes[id].queued = true;
    if (fused_masked_counters_enabled) {
        ++fused_masked_counts.virtual_tasks;
    }
    return true;
}

void Interpreter::Impl::execute_fused_masked_frontier(
    const std::size_t bucket_index,
    const SimulationTick time, const std::uint64_t delta)
{
    auto& global = fused_masked_global_frontiers.at(bucket_index);
    if (global.time != time || global.delta != delta
        || scheduler.now() != time || scheduler.delta() != delta) {
        throw std::logic_error { "masked global frontier crossed a delta" };
    }
    global.callback.reset();
    global.callback_key.reset();
    if (global.heads.empty()) {
        return;
    }
    if (fused_masked_counters_enabled) {
        ++fused_masked_counts.frontier_calls;
        ++fused_masked_counts.global_frontier_callbacks;
    }

    const auto continue_ready = [&] {
        if (!global.heads.empty()) {
            schedule_fused_masked_frontier(bucket_index, true);
        }
    };
    try {
        bool first_dispatch = true;
        while (!global.heads.empty()) {
            if (!first_dispatch && scheduler.stop_requested()) {
                break;
            }
            const auto physical = scheduler.next_current_order_key();
            const auto head = global.heads.front();
            if (physical && !(head.key < *physical)) {
                break;
            }
            auto barrier = global.next_other_key();
            if (physical && (!barrier || *physical < *barrier)) {
                barrier = physical;
            }
            const auto revision = scheduler.current_phase_revision();
            const auto stopping = scheduler.stop_requested();
            try {
                execute_fused_masked_region(head.region, bucket_index,
                    time, delta, barrier);
            } catch (...) {
                auto& ready = fused_masked_regions[head.region]
                    .ready_buckets[bucket_index].ready;
                if (ready.empty()) {
                    global.erase_head(head.region);
                } else {
                    global.set_head(head.region, ready.front().key);
                }
                throw;
            }
            auto& ready = fused_masked_regions[head.region]
                .ready_buckets[bucket_index].ready;
            if (ready.empty()) {
                global.erase_head(head.region);
            } else {
                global.set_head(head.region, ready.front().key);
            }
            first_dispatch = false;
            if (stopping || scheduler.stop_requested()
                || scheduler.current_phase_revision() != revision) {
                break;
            }
        }
    } catch (...) {
        continue_ready();
        throw;
    }
    continue_ready();
}

void Interpreter::Impl::execute_fused_masked_region(
    const std::size_t region_id, const std::size_t bucket_index,
    const SimulationTick time, const std::uint64_t delta,
    const std::optional<SchedulerOrderKey> barrier)
{
    auto& region = fused_masked_regions.at(region_id);
    auto& bucket = region.ready_buckets.at(bucket_index);
    if (bucket.time != time || bucket.delta != delta
        || scheduler.now() != time || scheduler.delta() != delta) {
        throw std::logic_error { "masked region frontier crossed a delta" };
    }
    if (bucket.ready.empty()) {
        return;
    }
    std::size_t prefix { };
    while (prefix < bucket.ready.size()
        && (!barrier || bucket.ready[prefix].key < *barrier)) {
        ++prefix;
    }
    if (prefix == 0U) {
        throw std::logic_error {
            "masked global frontier selected a blocked region"
        };
    }
    // The scheduler already checked stop before popping this callback. If a
    // concurrent request arrived between that check and this point, consume
    // the first original task exactly as an ordinary callback would.
    if (scheduler.stop_requested()) {
        prefix = 1U;
    }
    const auto retire = [&](const std::size_t count) {
        bucket.ready.erase(bucket.ready.begin(),
            bucket.ready.begin() + static_cast<std::ptrdiff_t>(count));
    };

    bool admissible = region.certified && region.executor
        && !process_profile_enabled && !update_profile_enabled
        && !execution_point_hook
        && (!native_signal_observation_any_hook
            || !native_signal_observation_any_hook());
    std::ranges::fill(region.activation_words, UINT64_C(0));
    std::ranges::fill(region.selected_write_mask, UINT64_C(0));
    for (std::size_t index = 0U; index < prefix && admissible; ++index) {
        const auto id = bucket.ready[index].process;
        const auto offset = fused_masked_offset_by_process[id];
        const auto& member = region.members[offset];
        const auto& state = processes[id];
        if (!state.queued || !state.waiting_on_static
            || state.status != ProcessStatus::waiting
            || state.suspended || state.halted
            || state.pc != member.resume_instruction
            || state.has_callable_frame_push) {
            admissible = false;
            break;
        }
        region.activation_words[offset / 64U]
            |= UINT64_C(1) << (offset % 64U);
        for (std::size_t word = 0U;
             word < region.selected_write_mask.size(); ++word) {
            region.selected_write_mask[word]
                |= member.mandatory_write_mask[word];
        }
    }
    if (region.candidate.projected) {
        for (const auto signal : region.candidate.outputs) {
            admissible &= !signal_transaction_observed[signal]
                && !forced_values[signal]
                && !forced_driver_values[signal]
                && !external_driver_values[signal];
        }
    } else {
        const auto signal = region.candidate.outputs.front();
        admissible &= owned_driver_active(signal)
            && !signal_transaction_observed[signal];
    }

    if (admissible) {
        ExecutionContext context { *this, bucket.ready.front().process };
        const ProcessCohortNativeContext native_context {
            this,
            context.direct_signal_aval(),
            context.direct_signal_bval(),
            context.signal_writer_revision(),
            context.supports_direct_word_updates(),
            context.execution_points_enabled(),
            context.direct_wide_signal_aval(),
            context.direct_wide_signal_bval(),
            context.direct_wide_signal_offsets(),
            context.direct_signal_logic9_plane0(),
            context.direct_signal_logic9_plane1(),
            context.direct_signal_logic9_plane2(),
            context.direct_signal_logic9_plane3(),
            context.direct_wide_signal_logic9_plane2(),
            context.direct_wide_signal_logic9_plane3(),
        };
        admissible = native_context.supports_direct_word_updates
            && !native_context.execution_points_enabled;
        if (admissible) {
            bool native_completed { };
            // A projected owner can stage before a later owner throws.
            // Never replay that accepted prefix, but leave untouched owners
            // queued at their original reserved keys.
            auto retiring_prefix = prefix;
            try {
                const auto completion = region.executor->resume(
                    native_context, region.activation_words);
                if (completion) {
                    auto outcome = OwnedDriverStage::unchanged;
                    bool normal_member_selected { };
                    std::size_t selected_terminal_members { };
                    for (std::size_t index = 0U; index < prefix; ++index) {
                        selected_terminal_members += std::ranges::find(
                            region.candidate.terminal_members,
                            bucket.ready[index].process)
                            != region.candidate.terminal_members.end();
                    }
                    if (region.candidate.projected) {
                        if (!completion->aggregate_slots.empty()
                            || completion->projected_writes.size() != prefix) {
                            throw std::logic_error {
                                "masked region returned an incomplete projected set"
                            };
                        }
                        for (std::size_t index = 0U; index < prefix; ++index) {
                            const auto id = bucket.ready[index].process;
                            const auto offset = fused_masked_offset_by_process[id];
                            const auto signal = region.candidate.outputs[offset];
                            const auto& write = completion->projected_writes[index];
                            if (write.signal != signal
                                || write.value.width()
                                    != signals[signal].initial_value.width()
                                || write.value.is_logic9()
                                    != (signals[signal].value_kind
                                        == ValueKind::logic9)) {
                                throw std::logic_error {
                                    "masked region returned an invalid projected value"
                                };
                            }
                        }
                        for (std::size_t index = 0U; index < prefix; ++index) {
                            const auto id = bucket.ready[index].process;
                            const auto& write = completion->projected_writes[index];
                            retiring_prefix = index + 1U;
                            schedule_projected(id, write.signal, write.value,
                                std::nullopt, 0U, 0U,
                                ProjectedDelayMode::inertial);
                        }
                    } else {
                        if (!completion->projected_writes.empty()
                            || completion->aggregate_slots.size()
                                != region.candidate.outputs.size()) {
                            throw std::logic_error {
                                "masked region returned an incomplete owned slot"
                            };
                        }
                        const auto& slot = completion->aggregate_slots.front();
                        const bool owned_selected = std::ranges::any_of(
                            region.selected_write_mask,
                            [](const std::uint64_t word) {
                                return word != 0U;
                            });
                        const auto inactive = [](const ProcessUpdateSlotView& view,
                                                  const SignalId signal,
                                                  const std::size_t width) {
                            if (view.signal != signal || view.width != width
                                || view.word_count != (width + 63U) / 64U
                                || view.active == nullptr || *view.active != 0U
                                || view.mask == nullptr) {
                                return false;
                            }
                            return std::all_of(view.mask,
                                view.mask + view.word_count,
                                [](const std::uint64_t word) {
                                    return word == 0U;
                                });
                        };
                        if (owned_selected
                            ? !valid_fused_masked_owned_slot(
                                region_id, slot, region.activation_words,
                                region.selected_write_mask)
                            : !inactive(slot, region.candidate.outputs.front(),
                                signals[region.candidate.outputs.front()]
                                    .initial_value.width())) {
                            throw std::logic_error {
                                "masked region returned an invalid owned mask"
                            };
                        }
                        std::optional<PackedLogic4> normal_value;
                        if (region.normal_output_owner) {
                            const auto normal_signal
                                = *region.candidate.normal_single_writer_output;
                            const auto& normal_slot
                                = completion->aggregate_slots.back();
                            normal_member_selected = std::ranges::any_of(
                                bucket.ready.begin(),
                                bucket.ready.begin()
                                    + static_cast<std::ptrdiff_t>(prefix),
                                [&](const auto& ready) {
                                    return ready.process
                                        == *region.normal_output_owner;
                                });
                            const auto width
                                = signals[normal_signal].initial_value.width();
                            if (normal_slot.signal != normal_signal
                                || normal_slot.width != width
                                || normal_slot.word_count != (width + 63U) / 64U
                                || normal_slot.active == nullptr
                                || normal_slot.aval == nullptr
                                || normal_slot.bval == nullptr
                                || normal_slot.mask == nullptr
                                || signal_transaction_observed[normal_signal]
                                || forced_values[normal_signal]
                                || forced_driver_values[normal_signal]
                                || external_driver_values[normal_signal]
                                || signal_writer_counts[normal_signal] != 1U
                                || stable_single_writer_processes[normal_signal]
                                    != *region.normal_output_owner
                                || (signals[normal_signal].resolution
                                        == ResolutionKind::sv_wire
                                    && (driver_values[normal_signal].size() != 1U
                                        || driver_values[normal_signal].find(
                                            *region.normal_output_owner)
                                            == nullptr))) {
                                throw std::logic_error {
                                    "masked region returned an invalid normal output"
                                };
                            }
                            for (std::size_t word = 0U;
                                 word < normal_slot.word_count; ++word) {
                                const auto size = std::min<std::size_t>(
                                    64U, width - word * 64U);
                                const auto full = size == 64U
                                    ? std::numeric_limits<std::uint64_t>::max()
                                    : (UINT64_C(1) << size) - UINT64_C(1);
                                if (normal_slot.mask[word]
                                    != (normal_member_selected ? full : 0U)) {
                                    throw std::logic_error {
                                        "masked normal output mask is incomplete"
                                    };
                                }
                            }
                            if ((*normal_slot.active != 0U)
                                != normal_member_selected) {
                                throw std::logic_error {
                                    "masked normal output activation changed"
                                };
                            }
                            if (normal_member_selected) {
                                normal_value = PackedLogic4::from_word_planes(
                                    width,
                                    { normal_slot.aval,
                                        normal_slot.word_count },
                                    { normal_slot.bval,
                                        normal_slot.word_count });
                            }
                        }
                        if (driver_update_scratch.size() < signals.size()) {
                            driver_update_scratch.resize(signals.size());
                            resolved_update_marked.resize(signals.size());
                        }
                        const auto stage_owned = [&](auto& write_mask,
                                                     const auto& activation) {
                            if (std::ranges::none_of(write_mask,
                                    [](const std::uint64_t word) {
                                        return word != 0U;
                                    })) {
                                return;
                            }
                            auto selected_slot = slot;
                            auto active = std::uint32_t { 1U };
                            selected_slot.mask = write_mask.data();
                            selected_slot.active = &active;
                            const auto staged = stage_fused_masked_owned_slot(
                                region_id, selected_slot, activation,
                                write_mask);
                            if (staged == OwnedDriverStage::unsupported) {
                                throw std::logic_error {
                                    "masked region lost its owned certificate"
                                };
                            }
                            if (staged == OwnedDriverStage::changed) {
                                outcome = staged;
                                schedule_update_commit();
                            }
                        };
                        const auto stage_normal = [&] {
                            if (normal_member_selected) {
                                stage_update(*region.normal_output_owner,
                                    *region.candidate.normal_single_writer_output,
                                    std::move(*normal_value));
                            }
                        };
                        if (!normal_member_selected) {
                            stage_owned(region.selected_write_mask,
                                region.activation_words);
                        } else {
                            const auto terminal = std::ranges::find_if(
                                bucket.ready.begin(),
                                bucket.ready.begin()
                                    + static_cast<std::ptrdiff_t>(prefix),
                                [&](const auto& ready) {
                                    return ready.process
                                        == *region.normal_output_owner;
                                });
                            const auto terminal_index = static_cast<std::size_t>(
                                terminal - bucket.ready.begin());
                            auto& before_mask
                                = region.before_terminal_write_mask;
                            auto& after_mask
                                = region.after_terminal_write_mask;
                            auto& before_activation
                                = region.before_terminal_activation_words;
                            auto& after_activation
                                = region.after_terminal_activation_words;
                            std::ranges::fill(before_mask, UINT64_C(0));
                            std::ranges::fill(after_mask, UINT64_C(0));
                            std::ranges::fill(before_activation, UINT64_C(0));
                            std::ranges::fill(after_activation, UINT64_C(0));
                            for (std::size_t index = 0U;
                                 index < prefix; ++index) {
                                if (index == terminal_index) {
                                    continue;
                                }
                                const auto id = bucket.ready[index].process;
                                const auto offset
                                    = fused_masked_offset_by_process[id];
                                auto& mask = index < terminal_index
                                    ? before_mask : after_mask;
                                auto& active_members = index < terminal_index
                                    ? before_activation : after_activation;
                                active_members[offset / 64U]
                                    |= UINT64_C(1) << (offset % 64U);
                                for (std::size_t word = 0U;
                                     word < mask.size(); ++word) {
                                    mask[word]
                                        |= region.members[offset]
                                            .mandatory_write_mask[word];
                                }
                            }
                            // All returned slots were validated above. Stage
                            // in original member order so an exception only
                            // retires owners whose stage was attempted.
                            retiring_prefix = terminal_index;
                            stage_owned(before_mask, before_activation);
                            retiring_prefix = terminal_index + 1U;
                            stage_normal();
                            retiring_prefix = prefix;
                            stage_owned(after_mask, after_activation);
                        }
                    }
                    for (std::size_t index = 0U;
                         index < prefix; ++index) {
                        const auto id = bucket.ready[index].process;
                        const auto offset = fused_masked_offset_by_process[id];
                        auto& state = processes[id];
                        state.queued = false;
                        state.pc = region.members[offset].resume_instruction;
                        clear_wait_timeout(state);
                        state.status = ProcessStatus::waiting;
                        state.waiting_on_static = true;
                        state.static_trigger_mask = 0U;
                    }
                    retire(prefix);
                    if (fused_masked_counters_enabled) {
                        ++fused_masked_counts.masked_calls;
                        fused_masked_counts.represented_members += prefix;
                        fused_masked_counts.terminal_activations
                            += selected_terminal_members;
                        fused_masked_counts.terminal_joint_activations
                            += prefix > selected_terminal_members
                                ? selected_terminal_members : 0U;
                        if (!region.candidate.projected) {
                            fused_masked_counts.owner_stage_calls_avoided
                                += prefix - (normal_member_selected ? 1U : 0U);
                            fused_masked_counts.aggregate_signals_staged
                                += outcome == OwnedDriverStage::changed;
                        }
                    }
                    native_completed = true;
                }
            } catch (...) {
                for (std::size_t index = 0U;
                     index < retiring_prefix; ++index) {
                    auto& state = processes[bucket.ready[index].process];
                    state.queued = false;
                    state.static_trigger_mask = 0U;
                }
                retire(retiring_prefix);
                throw;
            }
            if (native_completed) {
                return;
            }
        }
    }

    // A kernel decline is no-mutation. Keep every original queued/static
    // state intact while offering the same full-key prefix to the retained
    // prepared native route. Unsupported shapes fall through one task at a
    // time to the original interpreter/executor path.
    auto payloads = std::vector<std::uint64_t> { };
    try {
        payloads.reserve(prefix);
        for (std::size_t index = 0U; index < prefix; ++index) {
            payloads.push_back(pure_wave_singleton_payload
                | static_cast<std::uint64_t>(bucket.ready[index].process));
        }
    } catch (...) {
        // The global frontier still represents every untouched member.
        throw;
    }
    std::size_t offered_tasks { };
    std::size_t prepared_completed { };
    try {
        if (const auto completed = try_execute_pure_wave(
                payloads, offered_tasks)) {
            prepared_completed = *completed;
        }
    } catch (...) {
        const auto retired = std::min(prefix,
            std::max<std::size_t>(1U, offered_tasks));
        for (std::size_t index = 0U; index < retired; ++index) {
            processes[bucket.ready[index].process].queued = false;
        }
        retire(retired);
        throw;
    }
    if (prepared_completed != 0U) {
        retire(prepared_completed);
        if (fused_masked_counters_enabled) {
            fused_masked_counts.prepared_fallback_tasks
                += prepared_completed;
        }
        return;
    }
    const auto id = bucket.ready.front().process;
    retire(1U);
    auto& state = processes[id];
    state.queued = false;
    state.waiting_on_static = false;
    remove_dynamic_wait(state);
    if (fused_masked_counters_enabled) {
        ++fused_masked_counts.ordinary_fallback_tasks;
    }
    try {
        execute(id);
    } catch (...) {
        throw;
    }
}

} // namespace fsim::runtime::simir
