// SPDX-License-Identifier: Apache-2.0

#include "simir_internal.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <exception>
#include <limits>
#include <new>
#include <ranges>

namespace fsim::runtime::simir {

namespace {

struct SchedulerTaskReferencePayload {
    Scheduler::Task* task { };
};

void dispatch_scheduler_task_reference(
    Scheduler& scheduler,
    const SchedulerTaskReferencePayload& payload)
{
    if (payload.task == nullptr) {
        throw std::logic_error {
            "scheduler task reference is empty"
        };
    }
    (*payload.task)(scheduler);
}

template <typename Visitor>
void for_each_active_update_slot(
    const ProcessUpdateSlotBatch& batch,
    Visitor&& visitor)
{
    if (batch.active_words.empty()) {
        for (const auto& slot : batch.slots) {
            visitor(slot);
        }
        return;
    }
    for (std::size_t word_index = 0;
         word_index < batch.active_words.size(); ++word_index) {
        auto active = batch.active_words[word_index];
        while (active != 0U) {
            const auto bit = static_cast<std::size_t>(
                std::countr_zero(active));
            const auto slot_index = word_index * 64U + bit;
            if (slot_index < batch.slots.size()) {
                visitor(batch.slots[slot_index]);
            }
            active &= active - UINT64_C(1);
        }
    }
}

} // namespace

void Interpreter::Impl::block_native_logic9_update_before_generic(
    const SignalId signal)
{
    if (signal >= signals.size()
        || signal >= direct_single_driver_logic9_word_scratch.size()
        || signals[signal].value_kind != ValueKind::logic9
        || signals[signal].resolution != ResolutionKind::std_logic
        || signals[signal].initial_value.width() == 0U
        || signals[signal].initial_value.width() > 64U) {
        return;
    }
    auto& staged = direct_single_driver_logic9_word_scratch[signal];
    using Stage = DirectSingleDriverLogic9WordUpdate;
    if (staged.active == Stage::generic_blocked) {
        return;
    }
    if (staged.active == Stage::native) {
        // Native writes preceding this generic update must enter the same
        // ordered pending queue. Preserve only their written bits.
        std::uint32_t bit { };
        while (bit < staged.width) {
            while (bit < staged.width
                && ((staged.mask >> bit) & UINT64_C(1)) == 0U) {
                ++bit;
            }
            if (bit == staged.width) {
                break;
            }
            const auto offset = bit;
            while (bit < staged.width
                && ((staged.mask >> bit) & UINT64_C(1)) != 0U) {
                ++bit;
            }
            const auto width = bit - offset;
            const auto mask = width == 64U
                ? std::numeric_limits<std::uint64_t>::max()
                : (UINT64_C(1) << width) - UINT64_C(1);
            const auto value_index = pending_update_values.size();
            pending_update_values.push_back(
                PackedLogic4::from_logic9_word({
                    width,
                    { (staged.planes[0] >> offset) & mask,
                        (staged.planes[1] >> offset) & mask,
                        (staged.planes[2] >> offset) & mask,
                        (staged.planes[3] >> offset) & mask }
                }));
            pending_updates.push_back(PendingUpdate {
                signal,
                staged.process,
                width == staged.width
                    ? std::nullopt
                    : std::optional<std::size_t> { offset },
                { },
                value_index });
        }
    } else {
        if (native_logic9_word_update_count
            >= native_logic9_word_update_signals.size()) {
            throw std::logic_error {
                "native Logic9 update phase exceeded its signal capacity"
            };
        }
        native_logic9_word_update_signals[
            native_logic9_word_update_count++] = signal;
    }
    staged.active = Stage::generic_blocked;
    staged.mask = 0U;
}

bool Interpreter::Impl::can_stage_disjoint_owner_group(
    const SignalId signal,
    const std::vector<PendingDriverCommit>& staged)
{
    if (staged.size() < 2U
        || signal >= resolved_update_marked.size()
        || !resolved_update_marked[signal]
        || signal >= signals.size()
        || signal >= signal_container_aggregate_aliases.size()
        || signal_container_aggregate_aliases[signal]
        || (signal < signal_container_element_aliases.size()
            && signal_container_element_aliases[signal])
        || (signal < signal_container_aliases.size()
            && !signal_container_aliases[signal].empty())
        || (signals[signal].value_kind != ValueKind::logic4
            && signals[signal].value_kind != ValueKind::logic9)
        || !can_try_wide_disjoint_signal_commit(signal)) {
        return false;
    }

    const auto& descriptor = signals[signal];
    for (std::size_t index = 0U; index < staged.size(); ++index) {
        const auto& update = staged[index];
        if (!update.driver || update.owned_composite
            || update.value.width() != descriptor.initial_value.width()
            || update.value.is_logic9()
                != (descriptor.value_kind == ValueKind::logic9)
            || !can_try_wide_disjoint_owner_commit(*update.driver, signal)) {
            return false;
        }
        for (std::size_t earlier = 0U; earlier < index; ++earlier) {
            if (staged[earlier].driver == update.driver) {
                return false;
            }
        }
    }

    std::size_t matched_updates { };
    bool compatible_records = true;
    driver_values[signal].for_each_in_process_order(
        [&](const DriverRecord& record) {
            if (record.value.width() != descriptor.initial_value.width()
                || record.value.is_logic9()
                    != (descriptor.value_kind == ValueKind::logic9)) {
                compatible_records = false;
                return;
            }
            if (std::ranges::find(staged,
                    std::optional<ProcessId> { record.process },
                    &PendingDriverCommit::driver)
                != staged.end()) {
                ++matched_updates;
            }
        });
    return compatible_records && matched_updates == staged.size();
}

bool Interpreter::Impl::disjoint_owner_group_scratch_ready(
    const SignalId signal,
    const std::size_t owner_count) const noexcept
{
    if (signal >= disjoint_owner_group_scratch_by_signal.size()
        || signal >= signals.size()) {
        return false;
    }
    const auto& scratch = disjoint_owner_group_scratch_by_signal[signal];
    const bool is_logic9
        = signals[signal].value_kind == ValueKind::logic9;
    return scratch.prepared && scratch.mutations.size() >= owner_count
        && scratch.resolved.width() == signals[signal].initial_value.width()
        && scratch.resolved.is_logic9() == is_logic9;
}

bool Interpreter::Impl::prepare_disjoint_owner_group_scratch(
    const SignalId signal,
    const std::size_t owner_capacity) noexcept
{
    if (owner_capacity < 2U || signal >= signals.size()
        || signal >= disjoint_owner_group_scratch_by_signal.size()
        || (signals[signal].value_kind != ValueKind::logic4
            && signals[signal].value_kind != ValueKind::logic9)
        || !can_try_wide_disjoint_signal_commit(signal)) {
        return false;
    }

    auto& scratch = disjoint_owner_group_scratch_by_signal[signal];
    scratch.prepared = false;
    auto* const state = region_authoritative_state_for_signal(signal);
    if (state == nullptr) {
        return false;
    }
    try {
        const auto word_count
            = state->values().layout().signal(signal).word_count;
        if (scratch.mutations.size() < owner_capacity) {
            scratch.mutations.resize(owner_capacity);
        }
        for (std::size_t index = 0U; index < owner_capacity; ++index) {
            auto& mutation = scratch.mutations[index];
            if (mutation.words.capacity() < word_count) {
                mutation.words.reserve(word_count);
            }
        }
        const auto width = signals[signal].initial_value.width();
        const bool is_logic9
            = signals[signal].value_kind == ValueKind::logic9;
        if (scratch.resolved.width() != width
            || scratch.resolved.is_logic9() != is_logic9) {
            scratch.resolved = is_logic9
                ? PackedLogic4::from_logic9_msb_string(
                    std::string(width, 'X'))
                : PackedLogic4 { width, Logic4::x };
        }
        scratch.prepared = true;
        return true;
    } catch (const std::bad_alloc&) {
        // Keep all staged owners intact. The Update callback will take the
        // checked DriverTable path when this preallocation is unavailable.
        return false;
    }
}

void Interpreter::Impl::schedule_update_commit()
{
    // Prepare fixed-topology queue scratch before handing the commit to the
    // scheduler. In particular, optional native publication preflight must
    // not encounter first-use generic scratch allocation in its callback.
    // Check each vector independently so an allocation failure between
    // resizes leaves a subsequent preparation able to finish safely.
    if (unresolved_update_scratch.size() < signals.size()) {
        unresolved_update_scratch.resize(signals.size());
    }
    if (unresolved_update_owner_provenance.size() < signals.size()) {
        unresolved_update_owner_provenance.resize(signals.size());
    }
    unresolved_update_owner_provenance_signals.reserve(signals.size());
    if (driver_update_scratch.size() < signals.size()) {
        driver_update_scratch.resize(signals.size());
    }
    if (resolved_update_marked.size() < signals.size()) {
        resolved_update_marked.resize(signals.size());
    }
    if (direct_single_driver_commit_marked.size() < signals.size()) {
        direct_single_driver_commit_marked.resize(signals.size());
    }
    const auto update_commit_word_count
        = signals.size() / 64U + (signals.size() % 64U != 0U ? 1U : 0U);
    if (update_commit_words.size() < update_commit_word_count) {
        update_commit_words.resize(update_commit_word_count);
    }
    bool has_disjoint_owner_group_candidate { };
    for (const auto signal : driver_update_signals) {
        if (signal < driver_update_scratch.size()
            && can_stage_disjoint_owner_group(
                signal, driver_update_scratch[signal])) {
            has_disjoint_owner_group_candidate = true;
            break;
        }
    }
    for (std::size_t left = 0U;
         !has_disjoint_owner_group_candidate
             && left < pending_updates.size();
         ++left) {
        const auto& first = pending_updates[left];
        if (!first.driver_present() || first.signal >= signals.size()
            || !can_try_wide_disjoint_signal_commit(first.signal)) {
            continue;
        }
        for (std::size_t right = left + 1U;
             right < pending_updates.size(); ++right) {
            const auto& second = pending_updates[right];
            if (second.signal == first.signal
                && second.driver_present()
                && second.driver != first.driver) {
                has_disjoint_owner_group_candidate = true;
                break;
            }
        }
    }
    if (has_disjoint_owner_group_candidate) {
        try {
            if (disjoint_owner_group_scratch_by_signal.size()
                < signals.size()) {
                disjoint_owner_group_scratch_by_signal.resize(
                    signals.size());
            }
        } catch (const std::bad_alloc&) {
            // Preserve the ordinary checked path if the per-signal scratch
            // index cannot be created before the Update ticket is queued.
        }
    }
    if (disjoint_owner_group_scratch_by_signal.size() >= signals.size()) {
        for (const auto signal : driver_update_signals) {
            if (signal < driver_update_scratch.size()
                && can_stage_disjoint_owner_group(
                    signal, driver_update_scratch[signal])) {
                (void)prepare_disjoint_owner_group_scratch(
                    signal, driver_values[signal].size());
            }
        }
        for (std::size_t left = 0U; left < pending_updates.size(); ++left) {
            const auto& first = pending_updates[left];
            if (!first.driver_present() || first.signal >= signals.size()
                || !can_try_wide_disjoint_signal_commit(first.signal)) {
                continue;
            }
            bool earlier_same_signal { };
            for (std::size_t earlier = 0U; earlier < left; ++earlier) {
                earlier_same_signal = earlier_same_signal
                    || (pending_updates[earlier].signal == first.signal
                        && pending_updates[earlier].driver_present());
            }
            if (earlier_same_signal) {
                continue;
            }
            for (std::size_t right = left + 1U;
                 right < pending_updates.size(); ++right) {
                const auto& second = pending_updates[right];
                if (second.signal != first.signal
                    || !second.driver_present()
                    || second.driver == first.driver) {
                    continue;
                }
                (void)prepare_disjoint_owner_group_scratch(
                    first.signal, driver_values[first.signal].size());
                break;
            }
        }
    }
    if (native_update_profile_enabled) {
        ++native_update_profile_schedule_requests;
    }
    if (update_commit_scheduled) {
        if (native_update_profile_enabled) {
            ++native_update_profile_schedule_coalesced;
        }
        return;
    }
    // A generic Update ticket may borrow this member's address. Keep the
    // callable stable after its first initialization.
    if (!update_commit_callback) {
        update_commit_callback = [this](Scheduler&) {
            const auto clear_provenance = [this](void*) noexcept {
                clear_unresolved_update_owner_provenance();
            };
            const std::unique_ptr<void, decltype(clear_provenance)>
                clear_provenance_on_exit(this, clear_provenance);
            if (native_update_profile_enabled) {
                ++native_update_profile_commits;
            }
            if (update_profile_enabled) {
                ++update_profile_commits;
            }
            const auto stage_checked_owner_updates = [this](
                const SignalId signal,
                std::vector<PendingDriverCommit>& staged,
                const bool retain_staged_values) {
                if (signal
                    < region_authoritative_component_by_signal.size()) {
                    const auto component
                        = region_authoritative_component_by_signal[signal];
                    if (component
                        != std::numeric_limits<std::size_t>::max()) {
                        demote_region_authoritative_slots(component, false);
                    }
                }
                for (auto& update : staged) {
                    if (update.driver) {
                        set_driver(*update.driver, signal,
                            retain_staged_values
                                ? PackedLogic4 { update.value }
                                : std::move(update.value));
                    }
                }
                if (!retain_staged_values) {
                    staged.clear();
                }
            };
            // Flush only components whose signals can be touched by this
            // Update transaction, and do so before moving staged values or
            // changing raw driver state. Several fast paths below bypass
            // commit_driver and write A4 directly.
            if (region_forwarding_role_journal_nonempty_components != 0U) {
                const auto flush_signal = [this](const SignalId signal) {
                    require_region_forwarding_role_journal_flushed_for_signal(
                        signal);
                };
                for (std::uint32_t index = 0U;
                     index < native_logic9_word_update_count; ++index) {
                    flush_signal(native_logic9_word_update_signals[index]);
                }
                for (std::uint32_t index = 0U;
                     index < native_word_update_count; ++index) {
                    flush_signal(native_word_update_signals[index]);
                }
                for (const auto signal : direct_single_driver_update_signals) {
                    flush_signal(signal);
                }
                for (const auto signal : driver_update_signals) {
                    flush_signal(signal);
                }
                for (const auto signal : unresolved_update_signals) {
                    flush_signal(signal);
                }
                for (const auto signal : resolved_update_signals) {
                    flush_signal(signal);
                }
                for (const auto& pending : pending_updates) {
                    flush_signal(pending.signal);
                    if (pending.signal
                            < signal_container_aggregate_aliases.size()
                        && signal_container_aggregate_aliases[
                            pending.signal]) {
                        const auto object = *signal_container_aggregate_aliases[
                            pending.signal];
                        if (object
                            < container_element_signal_aliases.size()) {
                            for (const auto& alias
                                 : container_element_signal_aliases[object]) {
                                if (alias) {
                                    flush_signal(alias->signal);
                                }
                            }
                        }
                    }
                    if (has_bidirectional_switches
                        && pending.driver_present()
                        && switch_process(pending.driver)) {
                        const auto& connection
                            = processes.program_view(pending.driver);
                        if (connection.switch_source()) {
                            flush_signal(*connection.switch_source());
                        }
                        if (connection.switch_target()) {
                            flush_signal(*connection.switch_target());
                        }
                    }
                }
                if (has_bidirectional_switches) {
                    require_all_region_forwarding_role_journals_flushed();
                }
            }
            // Drain the old native Logic9 phase before processing the ordered
            // generic queue. A generic ingress already moved prior writes
            // into that queue and blocked later native writes for its signal.
            const bool hook_free_publication
                = native_word_publication_phase_eligible();
            const auto logic9_count
                = std::exchange(native_logic9_word_update_count, 0U);
            using Logic9Stage = DirectSingleDriverLogic9WordUpdate;
            for (std::uint32_t index = 0U; index < logic9_count; ++index) {
                const auto signal
                    = native_logic9_word_update_signals[index];
                auto& staged
                    = direct_single_driver_logic9_word_scratch[signal];
                if (staged.active == Logic9Stage::generic_blocked) {
                    staged.active = Logic9Stage::unlisted;
                    staged.mask = 0U;
                    continue;
                }
                if (staged.active != Logic9Stage::native) {
                    continue;
                }
                const auto process = staged.process;
                const auto origin = capture_signal_change_origin(process);
                const bool can_publish = hook_free_publication
                    && can_publish_native_logic9_word_prevalidated(
                        signal, process);
                if (!can_publish) {
                    block_native_logic9_update_before_generic(signal);
                    staged.active = Logic9Stage::unlisted;
                    staged.mask = 0U;
                    continue;
                }
                auto value = Logic9Word {
                    staged.width,
                    { direct_signal_logic9_plane0[signal],
                        direct_signal_logic9_plane1[signal],
                        direct_signal_logic9_plane2[signal],
                        direct_signal_logic9_plane3[signal] }
                };
                for (std::size_t plane = 0U;
                    plane < value.planes.size(); ++plane) {
                    value.planes[plane]
                        = (value.planes[plane] & ~staged.mask)
                        | (staged.planes[plane] & staged.mask);
                }
                staged.active = Logic9Stage::unlisted;
                staged.mask = 0U;
                publish_native_logic9_word(signal, value, origin);
            }
            if (has_bidirectional_switches) {
                for (const auto& update : pending_updates) {
                    if (update.driver_present()
                        && switch_process(update.driver)) {
                        const auto& connection
                            = processes.program_view(update.driver);
                        if (connection.switch_source()) {
                            mark_switch_network_dirty(
                                *connection.switch_source());
                        }
                        if (connection.switch_target()) {
                            mark_switch_network_dirty(
                                *connection.switch_target());
                        }
                    } else {
                        mark_switch_network_dirty(
                            update.signal, true);
                    }
                }
                for (const auto component : dirty_switch_components) {
                    if (switch_components[component]
                            .has_non_switch_update) {
                        reset_switch_drivers(component);
                    }
                }
                std::erase_if(
                    pending_updates, [&](const PendingUpdate& update) {
                        if (!update.driver_present()
                            || !switch_process(update.driver)) {
                            return false;
                        }
                        const auto& connection
                            = processes.program_view(update.driver);
                        if (!connection.switch_source()) {
                            return false;
                        }
                        const auto component
                            = switch_component_by_signal.at(
                                *connection.switch_source());
                        return component != no_switch_component
                            && switch_components[component]
                                .has_non_switch_update;
                    });
            }

            // Coalesce updates to one eligible physical container family in
            // the same packed-value space as its original aggregate target.
            // Leaf-only updates keep their independent target publications.
            struct ContainerAliasOwnerSelection {
                std::optional<ProcessId> process;
                std::vector<std::uint8_t> leaves;
            };
            struct ContainerAliasQueueCandidate {
                ContainerObjectId object { };
                SignalId proxy { };
                bool has_proxy_write { };
                bool driver_update_enqueued { };
                bool eligible { };
                std::vector<ContainerAliasOwnerSelection> owners;
                std::vector<std::uint8_t> selected_union;
            };
            std::vector<ContainerAliasQueueCandidate>
                container_alias_candidates;
            const auto ensure_container_alias_owner =
                [&](ContainerAliasQueueCandidate& candidate,
                    const std::optional<ProcessId> process)
                -> ContainerAliasOwnerSelection& {
                auto owner = std::ranges::find(
                    candidate.owners,
                    process,
                    &ContainerAliasOwnerSelection::process);
                if (owner != candidate.owners.end()) {
                    return *owner;
                }
                ContainerAliasOwnerSelection selection;
                selection.process = process;
                selection.leaves.resize(
                    container_element_signal_aliases.at(
                        candidate.object).size());
                candidate.owners.push_back(std::move(selection));
                return candidate.owners.back();
            };
            for (const auto& pending : pending_updates) {
                if (pending.signal
                        >= signal_container_aggregate_aliases.size()
                    || !signal_container_aggregate_aliases[pending.signal]) {
                    continue;
                }
                const auto object
                    = *signal_container_aggregate_aliases[pending.signal];

                auto candidate = std::ranges::find(
                    container_alias_candidates,
                    object,
                    &ContainerAliasQueueCandidate::object);
                if (candidate == container_alias_candidates.end()) {
                    const auto& aggregate_alias
                        = container_aggregate_signal_aliases.at(object);
                    if (!aggregate_alias) {
                        continue;
                    }
                    ContainerAliasQueueCandidate new_candidate;
                    new_candidate.object = object;
                    new_candidate.proxy = aggregate_alias->signal;
                    new_candidate.selected_union.resize(
                        container_element_signal_aliases.at(object).size());
                    container_alias_candidates.push_back(
                        std::move(new_candidate));
                    candidate = std::prev(
                        container_alias_candidates.end());
                }
                candidate->has_proxy_write = true;
                (void)ensure_container_alias_owner(
                    *candidate, pending.driver_present()
                        ? std::optional<ProcessId> { pending.driver }
                        : std::nullopt);
            }
            for (const auto& pending : pending_updates) {
                if (pending.signal
                        >= signal_container_element_aliases.size()
                    || !signal_container_element_aliases[pending.signal]) {
                    continue;
                }
                const auto object
                    = signal_container_element_aliases[pending.signal]->first;
                const auto candidate = std::ranges::find(
                    container_alias_candidates,
                    object,
                    &ContainerAliasQueueCandidate::object);
                if (candidate != container_alias_candidates.end()) {
                    (void)ensure_container_alias_owner(
                        *candidate, pending.driver_present()
                            ? std::optional<ProcessId> { pending.driver }
                            : std::nullopt);
                }
            }
            std::size_t eligible_container_alias_candidates { };
            for (auto& candidate : container_alias_candidates) {
                const auto proxy = candidate.proxy;
                candidate.eligible = candidate.has_proxy_write
                    && !candidate.owners.empty()
                    && std::ranges::all_of(
                        candidate.owners,
                        [&](const ContainerAliasOwnerSelection& owner) {
                            return !owner.process
                                || !get_process(*owner.process)
                                        .program().switch_source();
                        })
                    && can_stage_container_alias_deposit(candidate.object)
                    && !owned_driver_active(proxy)
                    && driver_values.at(proxy).empty()
                    && !external_driver_values.at(proxy)
                    && !forced_driver_values.at(proxy)
                    && !forced_values.at(proxy)
                    && !signals.at(proxy).has_implicit_driver
                    && !signals.at(proxy).has_charge_strength
                    && std::ranges::all_of(
                        container_element_signal_aliases.at(
                            candidate.object),
                        [&](const auto& alias) {
                            if (!alias) {
                                return false;
                            }
                            const auto leaf = alias->signal;
                            return !owned_driver_active(leaf)
                                && !signals.at(leaf).has_implicit_driver
                                && !signals.at(leaf).has_charge_strength;
                        });
                if (candidate.eligible) {
                    ++eligible_container_alias_candidates;
                }
            }
            struct QueuedContainerAliasDriverFamily {
                SignalId proxy { };
                ContainerObjectId object { };
                SignalChangeOrigin origin;
                std::vector<std::uint8_t> selected_leaves;
                std::vector<PackedLogic4> resolved_values;
            };
            std::vector<QueuedContainerAliasDriverFamily>
                queued_container_alias_drivers;
            queued_container_alias_drivers.reserve(
                eligible_container_alias_candidates);

            const auto mark_resolved = [&](const SignalId signal) {
                if (!resolved_update_marked[signal]) {
                    resolved_update_marked[signal] = true;
                    resolved_update_signals.push_back(signal);
                }
            };
            const auto materialize = [](PendingUpdate& pending) {
                return PackedLogic4::from_aval_bval(
                    pending.word.width,
                    pending.word.aval,
                    pending.word.bval);
            };
            const auto pending_value = [&](PendingUpdate& pending) {
                return pending.packed_value_present()
                    ? std::move(pending_update_values.at(
                          pending.packed_value))
                    : materialize(pending);
            };
            const auto container_family_driver_projection =
                [&](const ContainerAliasQueueCandidate& candidate,
                    const std::optional<ProcessId> process) {
                    const auto& object
                        = get_container_object(candidate.object);
                    const auto& aliases
                        = container_element_signal_aliases.at(
                            candidate.object);
                    const auto element_width
                        = object.initial_value.type.element_width;
                    auto projection = PackedLogic4 {
                        get_signal(candidate.proxy).initial_value.width(),
                        Logic4::z
                    };
                    for (std::size_t ordinal = 0U;
                        ordinal < aliases.size(); ++ordinal) {
                        const auto leaf = aliases[ordinal]->signal;
                        const auto offset
                            = (aliases.size() - ordinal - 1U)
                            * element_width;
                        if (!process) {
                            const auto& external
                                = external_driver_values.at(leaf);
                            if (external) {
                                projection.insert_bits(*external, offset);
                            } else {
                                projection.insert_bits(
                                    initial_driver_value(leaf), offset);
                            }
                            continue;
                        }
                        if (owned_driver_active(leaf)) {
                            if (*process < owned_driver_spans.size()
                                && owned_driver_spans[*process].signal
                                    == leaf) {
                                const auto value
                                    = owned_driver_value(*process, leaf);
                                projection.insert_bits(
                                    value, offset);
                            }
                            continue;
                        }
                        const auto* record
                            = driver_values.at(leaf).find(*process);
                        if (record == nullptr) {
                            continue;
                        }
                        projection.insert_bits(
                            record->value, offset);
                    }
                    return projection;
                };
            const auto select_container_proxy_bits =
                [&](ContainerAliasQueueCandidate& candidate,
                    ContainerAliasOwnerSelection& owner,
                    const std::size_t offset,
                    const std::size_t width) {
                    const auto& aliases
                        = container_element_signal_aliases.at(
                            candidate.object);
                    const auto element_width
                        = get_container_object(candidate.object)
                              .initial_value.type.element_width;
                    const auto range_end = offset + width;
                    for (std::size_t ordinal = 0U;
                        ordinal < aliases.size(); ++ordinal) {
                        const auto lane_begin
                            = (aliases.size() - ordinal - 1U)
                            * element_width;
                        const auto lane_end = lane_begin + element_width;
                        if (offset < lane_end && lane_begin < range_end) {
                            owner.leaves[ordinal] = 1U;
                            candidate.selected_union[ordinal] = 1U;
                        }
                    }
                };
            for (auto& pending : pending_updates) {
                if (update_profile_enabled) {
                    update_profile_bits += pending.packed_value_present()
                        ? pending_update_values.at(pending.packed_value).width()
                        : pending.word.width;
                    if (pending.offset_present()) {
                        ++update_profile_slices;
                    } else {
                        ++update_profile_whole;
                    }
                    if (get_signal(pending.signal).resolution
                        == ResolutionKind::none) {
                        ++update_profile_unresolved;
                    } else {
                        ++update_profile_resolved;
                        const auto& values = driver_values[pending.signal];
                        const auto& signal = get_signal(pending.signal);
                        if (signal.resolution == ResolutionKind::sv_wire
                            && values.size() == 1U
                            && !external_driver_values[pending.signal]
                            && !signal.has_implicit_driver
                            && !signal.has_charge_strength) {
                            ++update_profile_resolved_single_driver;
                        }
                    }
                }
                ContainerAliasQueueCandidate* family_candidate { };
                std::optional<std::size_t> family_leaf_ordinal;
                if (pending.signal
                        < signal_container_aggregate_aliases.size()
                    && signal_container_aggregate_aliases[pending.signal]) {
                    const auto object
                        = *signal_container_aggregate_aliases[pending.signal];
                    const auto candidate = std::ranges::find(
                        container_alias_candidates,
                        object,
                        &ContainerAliasQueueCandidate::object);
                    if (candidate != container_alias_candidates.end()) {
                        family_candidate = &*candidate;
                    }
                } else if (pending.signal
                        < signal_container_element_aliases.size()
                    && signal_container_element_aliases[pending.signal]) {
                    const auto [object, ordinal]
                        = *signal_container_element_aliases[pending.signal];
                    const auto candidate = std::ranges::find(
                        container_alias_candidates,
                        object,
                        &ContainerAliasQueueCandidate::object);
                    if (candidate != container_alias_candidates.end()) {
                        family_candidate = &*candidate;
                        family_leaf_ordinal = ordinal;
                    }
                }
                if (family_candidate != nullptr
                    && family_candidate->eligible) {
                    auto& candidate = *family_candidate;
                    const auto process = pending.driver_present()
                        ? std::optional<ProcessId> { pending.driver }
                        : std::nullopt;
                    auto owner = std::ranges::find(
                        candidate.owners,
                        process,
                        &ContainerAliasOwnerSelection::process);
                    if (owner == candidate.owners.end()) {
                        throw std::logic_error {
                            "eligible container update has no owner mask"
                        };
                    }

                    auto& staged = driver_update_scratch[candidate.proxy];
                    auto pending_driver = std::ranges::find(
                        staged,
                        std::optional<ProcessId> { process },
                        &PendingDriverCommit::driver);
                    if (pending_driver == staged.end()) {
                        if (!candidate.driver_update_enqueued) {
                            driver_update_signals.push_back(candidate.proxy);
                            candidate.driver_update_enqueued = true;
                        }
                        staged.push_back(PendingDriverCommit {
                            process,
                            container_family_driver_projection(
                                candidate, process) });
                        pending_driver = std::prev(staged.end());
                    }

                    auto value = pending_value(pending);
                    if (family_leaf_ordinal) {
                        const auto& aliases
                            = container_element_signal_aliases.at(
                                candidate.object);
                        const auto element_width
                            = get_container_object(candidate.object)
                                  .initial_value.type.element_width;
                        const auto lane_begin
                            = (aliases.size() - *family_leaf_ordinal - 1U)
                            * element_width;
                        const auto local_offset
                            = pending.offset_present()
                            ? pending.offset
                            : 0U;
                        pending_driver->value.insert_bits(
                            value, lane_begin + local_offset);
                        owner->leaves[*family_leaf_ordinal] = 1U;
                        candidate.selected_union[*family_leaf_ordinal] = 1U;
                    } else if (pending.offset_present()) {
                        pending_driver->value.insert_bits(
                            value, pending.offset);
                        select_container_proxy_bits(
                            candidate, *owner, pending.offset, value.width());
                    } else {
                        if (pending_driver->value.width() != value.width()) {
                            throw std::logic_error {
                                "aggregate alias update width changed while queued"
                            };
                        }
                        pending_driver->value = std::move(value);
                        std::ranges::fill(owner->leaves, 1U);
                        std::ranges::fill(candidate.selected_union, 1U);
                    }
                    mark_resolved(candidate.proxy);
                    continue;
                }
                const auto driver = pending.driver_present()
                    ? std::optional<ProcessId> { pending.driver }
                    : std::nullopt;
                if (has_bidirectional_switches
                    && driver && switch_process(*driver)) {
                    const auto& connection = processes.program_view(*driver);
                    if (connection.switch_source()) {
                        mark_resolved(*connection.switch_source());
                    }
                    if (connection.switch_target()) {
                        mark_resolved(*connection.switch_target());
                    }
                    continue;
                }
                if (stage_owned_driver_pending(pending)) {
                    mark_resolved(pending.signal);
                    continue;
                }
                PackedLogic4* destination { };
                if (get_signal(pending.signal).resolution
                    == ResolutionKind::none) {
                    note_unresolved_update_owner(
                        pending.signal, driver, !pending.offset_present());
                    auto& staged
                        = unresolved_update_scratch[pending.signal];
                    if (!staged) {
                        unresolved_update_signals.push_back(pending.signal);
                        if (!pending.offset_present()) {
                            staged.emplace(pending_value(pending));
                            continue;
                        }
                        staged.emplace(driven_values[pending.signal]);
                    }
                    destination = &*staged;
                } else {
                    const auto* direct_record
                        = direct_single_driver_record(pending.signal);
                    const bool switch_endpoint
                        = has_bidirectional_switches
                        && pending.signal
                            < switch_endpoint_adjacency.size()
                        && !switch_endpoint_adjacency[pending.signal].empty();
                    const bool direct_single_driver
                        = !switch_endpoint && driver
                        && direct_record != nullptr
                        && direct_record->value.width() <= 64U
                        && direct_record->process == *driver
                        && !external_driver_values[pending.signal]
                        && !forced_driver_values[pending.signal];
                    if (direct_single_driver) {
                        auto& staged
                            = unresolved_update_scratch[pending.signal];
                        if (!staged) {
                            direct_single_driver_update_signals.push_back(
                                pending.signal);
                            if (!pending.offset_present()) {
                                staged.emplace(pending_value(pending));
                                continue;
                            }
                            staged.emplace(direct_record->value);
                        }
                        destination = &*staged;
                    } else {
                        auto& staged
                            = driver_update_scratch[pending.signal];
                        auto found = std::ranges::find(
                            staged, driver,
                            &PendingDriverCommit::driver);
                        if (found == staged.end()) {
                            if (staged.empty()) {
                                driver_update_signals.push_back(
                                    pending.signal);
                            }
                            if (!pending.offset_present()) {
                                staged.push_back(PendingDriverCommit {
                                    driver, pending_value(pending) });
                                mark_resolved(pending.signal);
                                continue;
                            }
                            auto initial = [&]() {
                                if (driver
                                    && can_try_wide_disjoint_owner_commit(
                                        *driver, pending.signal)) {
                                    const auto* const state
                                        = region_authoritative_state_for_signal(
                                            pending.signal);
                                    if (state == nullptr) {
                                        throw std::logic_error {
                                            "bound disjoint owner lost its A4 state"
                                        };
                                    }
                                    // Materialize a private merge base. The
                                    // public DriverRecord is a bound facade;
                                    // copying it would create a read pin and
                                    // force a replacement allocation at the
                                    // subsequent owner publication.
                                    return state->values().owner_value(
                                        pending.signal, *driver);
                                }
                                return driver
                                    ? PackedLogic4 {
                                          driver_slot(*driver, pending.signal)
                                      }
                                    : PackedLogic4 {
                                          external_driver_slot(pending.signal)
                                      };
                            }();
                            staged.push_back(PendingDriverCommit {
                                driver,
                                std::move(initial) });
                            found = std::prev(staged.end());
                        }
                        destination = &found->value;
                        mark_resolved(pending.signal);
                    }
                }
                if (pending.offset_present()) {
                    if (!pending.packed_value_present()) {
                        destination->insert_word(
                            pending.word, pending.offset);
                    } else {
                        destination->insert_bits(
                            pending_update_values.at(
                                pending.packed_value),
                            pending.offset);
                    }
                } else {
                    *destination = pending_value(pending);
                }
            }
            pending_updates.clear();
            pending_update_values.clear();
            update_commit_scheduled = false;
            const bool native_publication_phase
                = native_word_publication_phase_eligible();

            const auto commit_direct_word = [&](const SignalId signal) {
                auto& staged = direct_single_driver_word_scratch[signal];
                const auto value = Logic4Word {
                    staged.width, staged.aval, staged.bval
                };
                const auto process = staged.process;
                const auto origin = capture_signal_change_origin(process);
                staged.active = 0U;
                const auto& route = direct_single_driver_routes[signal];
                if (!route.active || route.process != process
                    || direct_single_driver_record(signal) == nullptr
                    || external_driver_values[signal]
                    || forced_driver_values[signal]) {
                    set_driver(
                        process,
                        signal,
                        PackedLogic4::from_aval_bval(
                            value.width, value.aval, value.bval));
                    mark_resolved(signal);
                    return;
                }
                if (native_phase_profile_enabled) {
                    ++native_phase_profile_attempts;
                }
                const bool can_publish = native_publication_phase
                    ? can_publish_native_word_prevalidated(signal, process)
                    : can_publish_native_word(signal, process);
                if (can_publish) {
                    if (native_phase_profile_enabled) {
                        ++native_phase_profile_published;
                    }
                    publish_native_word(signal, value, origin);
                    return;
                }
                materialize_direct_signal(signal);
                auto* record = direct_single_driver_record(signal);
                if (record == nullptr) {
                    set_driver(
                        process,
                        signal,
                        PackedLogic4::from_aval_bval(
                            value.width, value.aval, value.bval));
                    mark_resolved(signal);
                    return;
                }
                const bool driver_changed
                    = !record->value.matches_word(value, 0U);
                if (driver_changed) {
                    prepare_region_authoritative_write(signal);
                    record->value.assign_word(value);
                    mirror_region_owner(signal, process, record->value);
                    if (driver_change_hook) {
                        driver_change_hook(process, signal, scheduler.now());
                        publish_aggregate_leaf_driver_change(
                            process, signal);
                    }
                }
                if (resolved_update_marked[signal]) {
                    return;
                }
                const bool stored_changed
                    = !driven_values[signal].matches_word(value, 0U);
                if (stored_changed) {
                    prepare_region_authoritative_write(signal);
                    driven_values[signal].assign_word(value);
                    mirror_region_stored(signal);
                    if (stored_signal_change_hook) {
                        stored_signal_change_hook(signal, scheduler.now());
                    }
                }
                publish_normalized_word(signal, value, true, origin);
                if (stored_changed) {
                    publish_container_signal_aliases(signal);
                }
            };
            for (std::uint32_t index = 0U;
                index < native_word_update_count; ++index) {
                commit_direct_word(native_word_update_signals[index]);
            }
            if (native_update_profile_enabled) {
                native_update_profile_commit_word_signals
                    += native_word_update_count;
            }
            native_word_update_count = 0U;

            for (const auto signal : direct_single_driver_update_signals) {
                auto& staged = unresolved_update_scratch[signal];
                auto& values = driver_values[signal];
                auto* record = values.sole();
                if (!staged || record == nullptr) {
                    throw std::logic_error {
                        "direct single-driver update lost its staged value"
                    };
                }
                const auto process = record->process;
                if (record->value != *staged) {
                    prepare_region_authoritative_write(signal);
                    record->value = *staged;
                    mirror_region_owner(signal, process, record->value);
                    if (driver_change_hook) {
                        driver_change_hook(process, signal, scheduler.now());
                        publish_aggregate_leaf_driver_change(
                            process, signal);
                    }
                }
                if (resolved_update_marked[signal]) {
                    // Another governed update for the same resolved net was
                    // staged in this update phase. Its ordinary resolution
                    // pass must observe the newly published native driver.
                    staged.reset();
                } else {
                    unresolved_update_signals.push_back(signal);
                    direct_single_driver_commit_marked[signal] = true;
                }
            }
            direct_single_driver_update_signals.clear();

            for (const auto signal : driver_update_signals) {
                auto& staged = driver_update_scratch[signal];
                const auto candidate = std::ranges::find(
                    container_alias_candidates,
                    signal,
                    &ContainerAliasQueueCandidate::proxy);
                if (candidate != container_alias_candidates.end()
                    && candidate->eligible && !staged.empty()
                    && std::ranges::all_of(
                        staged,
                        [](const PendingDriverCommit& update) {
                            return !update.owned_composite;
                        })
                    && !owned_driver_active(signal)
                    && driver_values.at(signal).empty()
                    && !external_driver_values.at(signal)
                    && !forced_driver_values.at(signal)
                    && !forced_values.at(signal)
                    && !signals.at(signal).has_implicit_driver
                    && !signals.at(signal).has_charge_strength
                    && can_stage_container_alias_deposit(
                        candidate->object)) {
                    std::vector<std::pair<
                        SignalId, std::unique_ptr<PackedLogic4>>>
                        prepared_external_values;
                    const auto external_update = std::ranges::find(
                        staged,
                        std::optional<ProcessId> { },
                        &PendingDriverCommit::driver);
                    if (external_update != staged.end()) {
                        const auto external_owner = std::ranges::find(
                            candidate->owners,
                            std::optional<ProcessId> { },
                            &ContainerAliasOwnerSelection::process);
                        if (external_owner == candidate->owners.end()) {
                            throw std::logic_error {
                                "external aggregate update lost its leaf mask"
                            };
                        }
                        const auto& aliases
                            = container_element_signal_aliases.at(
                                candidate->object);
                        prepared_external_values.reserve(aliases.size());
                        auto aggregate = normalize_signal_value(
                            signal, external_update->value);
                        auto replacement
                            = get_container_object(candidate->object)
                                  .initial_value;
                        unpack_container_signal_value(
                            replacement, aggregate);
                        for (std::size_t ordinal = 0U;
                            ordinal < aliases.size(); ++ordinal) {
                            if (external_owner->leaves[ordinal] == 0U) {
                                continue;
                            }
                            const auto leaf = aliases[ordinal]->signal;
                            materialize_direct_signal(leaf);
                            auto value = normalize_signal_value(
                                leaf, replacement.elements[ordinal]);
                            prepared_external_values.emplace_back(
                                leaf,
                                std::make_unique<PackedLogic4>(
                                    std::move(value)));
                        }
                    }
                    queued_container_alias_drivers.emplace_back();
                    auto& queued
                        = queued_container_alias_drivers.back();
                    queued.proxy = signal;
                    queued.object = candidate->object;
                    queued.origin = { };
                    queued.selected_leaves = candidate->selected_union;
                    for (auto& update : staged) {
                        const auto owner = std::ranges::find(
                            candidate->owners,
                            update.driver,
                            &ContainerAliasOwnerSelection::process);
                        if (owner == candidate->owners.end()) {
                            throw std::logic_error {
                                "eligible aggregate update lost its leaf mask"
                            };
                        }
                        if (!update.driver) {
                            const bool adds_external_owner
                                = std::ranges::any_of(
                                    prepared_external_values,
                                    [this](const auto& item) {
                                        return !external_driver_values.at(
                                            item.first);
                                    });
                            if (adds_external_owner) {
                                note_region_graph_policy_change();
                            }
                            for (auto& [leaf, value] :
                                prepared_external_values) {
                                external_driver_values.at(leaf)
                                    = std::move(value);
                            }
                            for (const auto& [leaf, value] :
                                prepared_external_values) {
                                (void)value;
                                refresh_direct_single_driver_route(leaf);
                            }
                            continue;
                        }

                        auto aggregate = normalize_signal_value(
                            signal, std::move(update.value));
                        auto replacement
                            = get_container_object(candidate->object)
                                  .initial_value;
                        unpack_container_signal_value(
                            replacement, aggregate);
                        auto prepared
                            = prepare_container_alias_driver_family(
                                candidate->object,
                                *update.driver,
                                replacement.elements,
                                queued.origin,
                                owner->leaves);
                        begin_container_alias_driver_family(prepared);
                        install_container_alias_driver_family(prepared);
                        notify_container_alias_driver_family_raw(prepared);
                        finish_container_alias_driver_family_raw(prepared);
                        if (prepared.raw_observer_failure) {
                            std::rethrow_exception(
                                prepared.raw_observer_failure);
                        }
                    }
                    staged.clear();
                    continue;
                }
                if (owned_driver_active(signal)) {
                    commit_owned_driver(signal);
                    staged.clear();
                    continue;
                }
                if (disjoint_owner_group_scratch_ready(
                        signal, staged.size())
                    && can_stage_disjoint_owner_group(signal, staged)) {
                    // Keep original DriverRecord slots and staged values
                    // intact until this signal reaches its existing ordered
                    // commit_row below.
                    continue;
                }
                for (auto& update : staged) {
                    if (update.driver) {
                        const auto owner = *update.driver;
                        const bool generic_projected_owner
                            = staged.size() == 1U
                            && update.value.width() > 64U
                            && processes.scheduling_origin(owner).process_domain
                                == ProcessSchedulingDomain::generic
                            && region_graph
                            && owner < region_graph->processes().size()
                            && region_graph->processes()[owner].update_kind
                                == RegionUpdateKind::vhdl_projected
                            && can_try_wide_single_owner_commit(
                                owner, signal);
                        const bool disjoint_wide_owner
                            = can_try_wide_disjoint_owner_commit(
                                *update.driver, signal);
                        set_driver(
                            owner,
                            signal,
                            std::move(update.value),
                            generic_projected_owner || disjoint_wide_owner);
                    } else {
                        external_driver_slot(signal)
                            = std::move(update.value);
                        refresh_direct_single_driver_route(signal);
                    }
                }
                staged.clear();
            }
            driver_update_signals.clear();

            update_commit_scratch.clear();
            update_commit_scratch.reserve(
                unresolved_update_signals.size()
                + resolved_update_signals.size());
            for (auto& word : update_commit_words) {
                word.dirty_mask = 0U;
                word.head = std::numeric_limits<std::size_t>::max();
            }
            const auto append_commit_row = [this](
                                               const SignalId signal,
                                               PackedLogic4 value,
                                               const bool disjoint_owner_group
                                                   = false) {
                if (signal >= signals.size()) {
                    throw std::logic_error {
                        "update commit row exceeds the signal table"
                    };
                }
                const auto word_index = signal / 64U;
                const auto bit = static_cast<unsigned>(signal % 64U);
                if (word_index >= update_commit_words.size()) {
                    throw std::logic_error {
                        "update commit word scratch was not prepared"
                    };
                }
                auto& word = update_commit_words[word_index];
                const auto bit_mask = UINT64_C(1) << bit;
                if ((word.dirty_mask & bit_mask) != 0U) {
                    throw std::logic_error {
                        "update commit signal was staged more than once"
                    };
                }
                const auto row_index = update_commit_scratch.size();
                update_commit_scratch.push_back({ signal,
                    std::move(value), word.head, disjoint_owner_group });
                word.head = row_index;
                word.dirty_mask |= bit_mask;
            };
            for (const auto signal : unresolved_update_signals) {
                auto& staged = unresolved_update_scratch[signal];
                append_commit_row(signal, std::move(*staged));
                staged.reset();
            }
            unresolved_update_signals.clear();
            for (const auto signal : resolved_update_signals) {
                const auto queued_family = std::ranges::find(
                    queued_container_alias_drivers,
                    signal,
                    &QueuedContainerAliasDriverFamily::proxy);
                if (queued_family
                    == queued_container_alias_drivers.end()) {
                    auto& staged = driver_update_scratch[signal];
                    if (disjoint_owner_group_scratch_ready(
                            signal, staged.size())
                        && can_stage_disjoint_owner_group(signal, staged)) {
                        auto& resolved
                            = disjoint_owner_group_scratch_by_signal[
                                signal].resolved;
                        if (signals[signal].value_kind == ValueKind::logic9) {
                            if (!resolved.is_logic9()
                                || !driven_values[signal].is_logic9()) {
                                throw std::logic_error {
                                    "Logic9 owner group scratch has the wrong value kind"
                                };
                            }
                            for (std::size_t bit = 0U;
                                bit < resolved.width(); ++bit) {
                                auto bit_value
                                    = driven_values[signal].get_logic9(bit);
                                bool has_source { };
                                bool incompatible_source { };
                                driver_values[signal]
                                    .for_each_in_process_order(
                                        [&](const DriverRecord& record) {
                                            if (record.scalar_regions
                                                && std::ranges::none_of(
                                                    *record.scalar_regions,
                                                    [bit](const Process::DriverRegion& region) {
                                                        return bit >= region.offset
                                                            && bit - region.offset
                                                                < region.width;
                                                    })) {
                                                return;
                                            }
                                            const auto update
                                                = std::ranges::find(
                                                    staged,
                                                    std::optional<ProcessId> {
                                                        record.process
                                                    },
                                                    &PendingDriverCommit::driver);
                                            const auto& owner_value
                                                = update == staged.end()
                                                ? record.value : update->value;
                                            if (!owner_value.is_logic9()
                                                || owner_value.width()
                                                    != resolved.width()) {
                                                incompatible_source = true;
                                                return;
                                            }
                                            const auto source
                                                = owner_value.get_logic9(bit);
                                            bit_value = has_source
                                                ? runtime::resolve(
                                                      bit_value, source)
                                                : source;
                                            has_source = true;
                                        });
                                if (incompatible_source) {
                                    throw std::logic_error {
                                        "Logic9 owner group contains an incompatible driver"
                                    };
                                }
                                resolved.set_logic9(bit, bit_value);
                            }
                        } else {
                            const auto encode_logic4 = [](const Logic4 value) {
                                switch (value) {
                                case Logic4::zero:
                                    return Logic4Word { 1U, 0U, 0U };
                                case Logic4::one:
                                    return Logic4Word { 1U, 1U, 0U };
                                case Logic4::x:
                                    return Logic4Word { 1U, 1U, 1U };
                                case Logic4::z:
                                    return Logic4Word { 1U, 0U, 1U };
                                }
                                return Logic4Word { 1U, 1U, 1U };
                            };
                            const auto decode_logic4 = [](const Logic4Word& word) {
                                if (word.bval != 0U) {
                                    return word.aval != 0U
                                        ? Logic4::x : Logic4::z;
                                }
                                return word.aval != 0U
                                    ? Logic4::one : Logic4::zero;
                            };
                            for (std::size_t bit = 0U;
                                bit < resolved.width(); ++bit) {
                                Logic4ResolutionAccumulator accumulator { 1U };
                                driver_values[signal]
                                    .for_each_in_process_order(
                                        [&](const DriverRecord& record) {
                                            const auto update = std::ranges::find(
                                                staged,
                                                std::optional<ProcessId> {
                                                    record.process
                                                },
                                                &PendingDriverCommit::driver);
                                            const auto& owner_value
                                                = update == staged.end()
                                                ? record.value : update->value;
                                            accumulator.add(encode_logic4(
                                                owner_value.get(bit)));
                                        });
                                resolved.set(bit,
                                    decode_logic4(accumulator.result()));
                            }
                        }
                        append_commit_row(signal, PackedLogic4 { }, true);
                    } else {
                        append_commit_row(
                            signal, resolved_driver_value(signal));
                    }
                } else {
                    const auto& aliases
                        = container_element_signal_aliases.at(
                            queued_family->object);
                    queued_family->resolved_values.reserve(aliases.size());
                    for (std::size_t ordinal = 0U;
                        ordinal < aliases.size(); ++ordinal) {
                        const auto leaf = aliases[ordinal]->signal;
                        queued_family->resolved_values.push_back(
                            queued_family->selected_leaves[ordinal] != 0U
                                ? resolved_driver_value(leaf)
                                : driven_values.at(leaf));
                    }
                }
                resolved_update_marked[signal] = false;
            }
            resolved_update_signals.clear();
            std::sort(
                queued_container_alias_drivers.begin(),
                queued_container_alias_drivers.end(),
                [](const auto& lhs, const auto& rhs) {
                    return lhs.proxy < rhs.proxy;
                });
            if (native_update_profile_enabled) {
                native_update_profile_commit_value_signals
                    += update_commit_scratch.size()
                    + queued_container_alias_drivers.size();
            }
            std::size_t queued_family_index { };
            const auto commit_disjoint_owner_group = [this,
                &stage_checked_owner_updates](
                const SignalId signal,
                std::vector<PendingDriverCommit>& staged) {
                const auto component
                    = signal
                            < region_authoritative_component_by_signal.size()
                    ? region_authoritative_component_by_signal[signal]
                    : std::numeric_limits<std::size_t>::max();
                auto* const state
                    = region_authoritative_state_for_signal(signal);
                if (signal >= disjoint_owner_group_scratch_by_signal.size()) {
                    stage_checked_owner_updates(signal, staged, false);
                    commit_resolved(
                        signal, resolved_driver_value(signal), { });
                    return;
                }
                auto& scratch
                    = disjoint_owner_group_scratch_by_signal[signal];
                auto& mutations = scratch.mutations;
                const auto mutation_count = staged.size();
                const bool scratch_ready
                    = disjoint_owner_group_scratch_ready(
                        signal, mutation_count);
                const auto mutation_rows = scratch_ready
                    ? std::span<AuthoritativeSignalPlanes::PreparedMutation> {
                        mutations.data(), mutation_count }
                    : std::span<AuthoritativeSignalPlanes::PreparedMutation> { };
                bool prepared { };
                if (state != nullptr && scratch_ready
                    && can_try_wide_disjoint_signal_commit(signal)
                    && staged.size() >= 2U) {
                    try {
                        std::size_t prepared_count { };
                        for (std::size_t index = 0U;
                             index < staged.size(); ++index) {
                            const auto& update = staged[index];
                            auto& mutation = mutation_rows[index];
                            if (!update.driver
                                || update.owned_composite
                                || mutation.words.capacity()
                                    < state->values().layout()
                                        .signal(signal).word_count
                                || !can_try_wide_disjoint_owner_commit(
                                    *update.driver, signal)) {
                                break;
                            }
                            state->values().prepare_owner_group_change_into(
                                mutation, signal, *update.driver,
                                update.value, scratch.resolved,
                                scratch.resolved);
                            ++prepared_count;
                        }
                        prepared = prepared_count == mutation_count
                            && state->values()
                                .begin_prepared_owner_group_publication(
                                    mutation_rows);
                    } catch (const std::bad_alloc&) {
                        prepared = false;
                    } catch (const std::invalid_argument&) {
                        prepared = false;
                    }
                }

                if (!prepared) {
                    if (!mutation_rows.empty() && state != nullptr) {
                        state->values()
                            .cancel_prepared_owner_group_publication(
                                mutation_rows);
                    }
                    // Preserve the staged values until the checked signal
                    // publication succeeds. The group result was computed
                    // before raw owner mutation, so do not resolve again
                    // after the checked owner writes.
                    stage_checked_owner_updates(signal, staged, true);
                    auto resolved = std::move(scratch.resolved);
                    commit_resolved(signal, std::move(resolved), { });
                    staged.clear();
                    return;
                }

                const bool current_changed
                    = mutation_rows.front().any_current_changed;
                try {
                    note_signal_transaction(signal, true, { }, true);
                } catch (...) {
                    state->values()
                        .cancel_prepared_owner_group_publication(
                            mutation_rows);
                    if (component
                        != std::numeric_limits<std::size_t>::max()) {
                        demote_region_authoritative_slots(
                            component, false);
                    }
                    throw;
                }
                if (!state->values().publish_prepared_owner_group(
                        mutation_rows)) {
                    if (component
                        != std::numeric_limits<std::size_t>::max()) {
                        demote_region_authoritative_slots(component, false);
                    }
                    throw std::logic_error {
                        "preflighted A4 owner group failed to publish"
                    };
                }
                staged.clear();
                if (current_changed) {
                    refresh_direct_signal_planes(signal);
                    publish_value_change(signal, true, { });
                }
            };
            const auto commit_row = [&](UpdateCommitScratchRow& row) {
                const auto signal = row.signal;
                auto& value = row.value;
                while (queued_family_index
                        < queued_container_alias_drivers.size()
                    && queued_container_alias_drivers[
                           queued_family_index].proxy < signal) {
                    auto& queued
                        = queued_container_alias_drivers[
                            queued_family_index++];
                    publish_container_alias_family(
                        queued.object,
                        queued.resolved_values,
                        queued.origin,
                        { },
                        queued.selected_leaves);
                }
                if (queued_family_index
                        < queued_container_alias_drivers.size()
                    && queued_container_alias_drivers[
                           queued_family_index].proxy == signal) {
                    throw std::logic_error {
                        "container alias driver family duplicated its "
                        "value-publication signal"
                    };
                }
                // Generic commits can take the wide A4 writer fast paths
                // below without passing through commit_driver. Materialize
                // any applied forwarding role row for this signal first.
                require_region_forwarding_role_journal_flushed_for_signal(
                    signal);
                if (row.disjoint_owner_group) {
                    commit_disjoint_owner_group(
                        signal, driver_update_scratch[signal]);
                    return;
                }
                if (direct_single_driver_commit_marked[signal]) {
                    direct_single_driver_commit_marked[signal] = false;
                    commit_direct_single_driver(signal, std::move(value));
                } else if (get_signal(signal).resolution
                    == ResolutionKind::none) {
                    const auto& provenance
                        = unresolved_update_owner_provenance[signal];
                    if (provenance.touched && provenance.has_process
                        && !provenance.ambiguous
                        && try_commit_wide_unresolved_owner_alias(
                            provenance.process, signal, value, { })) {
                        return;
                    }
                    commit(signal, std::move(value));
                } else if (try_commit_wide_disjoint_value(
                               signal, value, { })) {
                    return;
                } else {
                    const auto* const record
                        = direct_single_driver_record(signal);
                    if (record != nullptr
                        && record->value.width() > 64U
                        && try_commit_wide_single_owner(
                            record->process, signal, value, { }, true)) {
                        return;
                    }
                    commit_resolved(signal, std::move(value));
                }
            };
            std::array<std::size_t, 64U> row_index_by_bit { };
            constexpr auto no_row = std::numeric_limits<std::size_t>::max();
            for (std::size_t word_index = 0U;
                 word_index < update_commit_words.size(); ++word_index) {
                const auto& word = update_commit_words[word_index];
                if (word.dirty_mask == 0U) {
                    if (word.head != no_row) {
                        throw std::logic_error {
                            "empty update commit word has a linked row"
                        };
                    }
                    continue;
                }
                row_index_by_bit.fill(no_row);
                auto row_index = word.head;
                std::size_t linked_rows { };
                while (row_index != no_row) {
                    if (row_index >= update_commit_scratch.size()
                        || ++linked_rows > update_commit_scratch.size()) {
                        throw std::logic_error {
                            "update commit word has an invalid row link"
                        };
                    }
                    const auto& row = update_commit_scratch[row_index];
                    const auto row_word = row.signal / 64U;
                    const auto bit = static_cast<unsigned>(
                        row.signal % 64U);
                    const auto bit_mask = UINT64_C(1) << bit;
                    if (row_word != word_index
                        || (word.dirty_mask & bit_mask) == 0U
                        || row_index_by_bit[bit] != no_row) {
                        throw std::logic_error {
                            "update commit word does not match its rows"
                        };
                    }
                    row_index_by_bit[bit] = row_index;
                    row_index = row.next_in_word;
                }
                if (linked_rows
                    != static_cast<std::size_t>(
                        std::popcount(word.dirty_mask))) {
                    throw std::logic_error {
                        "update commit word mask does not match its rows"
                    };
                }
                auto dirty = word.dirty_mask;
                while (dirty != 0U) {
                    const auto bit = static_cast<std::size_t>(
                        std::countr_zero(dirty));
                    const auto slot = row_index_by_bit[bit];
                    if (slot == no_row) {
                        throw std::logic_error {
                            "update commit word mask has no value row"
                        };
                    }
                    commit_row(update_commit_scratch[slot]);
                    dirty &= dirty - UINT64_C(1);
                }
            }
            while (queued_family_index
                < queued_container_alias_drivers.size()) {
                auto& queued = queued_container_alias_drivers[
                    queued_family_index++];
                publish_container_alias_family(
                    queued.object,
                    queued.resolved_values,
                    queued.origin,
                    { },
                    queued.selected_leaves);
            }
        };
    }

    const auto frontier = scheduler.current_generic_batch_frontier();
    if (frontier && frontier->generation != 0U) {
        try {
            auto reservation
                = scheduler.reserve_internal_generic_update_batch_from_frontier(
                    frontier->generation, 1U);
            if (reservation) {
                const std::array<StableOrder, 1U> orders {
                    std::numeric_limits<StableOrder>::max()
                };
                const std::array<detail::SchedulerTaskDescriptor, 1U> tasks {
                    detail::make_scheduler_task_descriptor<
                        SchedulerTaskReferencePayload,
                        &dispatch_scheduler_task_reference>(
                        { &update_commit_callback })
                };
                if (reservation.commit(orders, tasks)) {
                    if (fused_static_counters_enabled) {
                        ++fused_static_counts.generic_update_commit_tickets;
                    }
                    update_commit_scheduled = true;
                    return;
                }
            }
        } catch (const std::bad_alloc&) {
            // This is an optional queue preflight. If it declines before
            // enqueue, retain the ordinary callback path.
        }
    }

    scheduler.schedule(SchedulerPhase::update,
        std::numeric_limits<StableOrder>::max(), update_commit_callback);
    update_commit_scheduled = true;
}

void Interpreter::Impl::note_unresolved_update_owner(
    const SignalId signal,
    const std::optional<ProcessId> process,
    const bool whole_update)
{
    if (signal >= unresolved_update_owner_provenance.size()) {
        throw std::logic_error {
            "unresolved owner provenance was not preallocated"
        };
    }
    auto& provenance = unresolved_update_owner_provenance[signal];
    if (!provenance.touched) {
        if (unresolved_update_owner_provenance_signals.size()
            >= unresolved_update_owner_provenance_signals.capacity()) {
            throw std::logic_error {
                "unresolved owner provenance was not preallocated"
            };
        }
        provenance.touched = true;
        provenance.process = process.value_or(ProcessId { });
        provenance.has_process = process.has_value();
        provenance.ambiguous = !whole_update || !process.has_value();
        unresolved_update_owner_provenance_signals.push_back(signal);
        return;
    }
    if (!whole_update || !process || !provenance.has_process
        || provenance.process != *process) {
        // Once provenance becomes mixed or incomplete in this Update phase,
        // a later matching owner cannot make it precise again.
        provenance.ambiguous = true;
    }
}

void Interpreter::Impl::clear_unresolved_update_owner_provenance() noexcept
{
    for (const auto signal : unresolved_update_owner_provenance_signals) {
        if (signal < unresolved_update_owner_provenance.size()) {
            unresolved_update_owner_provenance[signal] = { };
        }
    }
    unresolved_update_owner_provenance_signals.clear();
}

SignalChangeOrigin Interpreter::Impl::capture_signal_change_origin(
    const ProcessId process,
    const SignalUpdateDomain update_domain) const
{
    const auto process_origin = processes.scheduling_origin(process);
    const auto process_domain = process_origin.process_domain;
    const auto phase = process_origin.phase;
    if (update_domain != SignalUpdateDomain::generic
        && process_domain != ProcessSchedulingDomain::systemverilog) {
        throw std::logic_error {
            "a tagged SystemVerilog update requires a SystemVerilog process"
        };
    }
    if (update_domain == SignalUpdateDomain::systemverilog_active) {
        if (phase == SchedulerPhase::observed
            || phase == SchedulerPhase::postponed) {
            throw std::logic_error {
                "SystemVerilog writes from Observed or Postponed are not "
                "supported"
            };
        }
        return { ProcessSchedulingDomain::systemverilog,
            phase };
    }
    if (update_domain == SignalUpdateDomain::systemverilog_nba) {
        if (phase == SchedulerPhase::active) {
            return { ProcessSchedulingDomain::systemverilog,
                SchedulerPhase::update };
        }
        if (phase == SchedulerPhase::reactive) {
            return { ProcessSchedulingDomain::systemverilog,
                SchedulerPhase::re_update };
        }
        throw std::logic_error {
            "SystemVerilog nonblocking writes from Observed or Postponed "
            "are not supported"
        };
    }
    return { process_domain,
        scheduler.current_phase().value_or(phase) };
}

void Interpreter::Impl::schedule_systemverilog_update(
    const ProcessId process,
    const SignalId signal,
    PackedLogic4 value,
    const std::optional<std::size_t> offset,
    const SignalUpdateDomain update_domain,
    const SimulationTick delay)
{
    if (update_domain == SignalUpdateDomain::generic) {
        throw std::invalid_argument {
            "a SystemVerilog update requires an explicit scheduling domain"
        };
    }
    const auto origin = capture_signal_change_origin(process, update_domain);
    if (route_module_path_update(
            process, signal, value, offset, nullptr, delay, origin)) {
        return;
    }
    if (delay
        > std::numeric_limits<SimulationTick>::max() - scheduler.now()) {
        throw std::overflow_error(
            "simulation time overflow while scheduling event");
    }
    const auto time = scheduler.now() + delay;
    const auto token = reserve_systemverilog_update(
        process, signal, std::move(value), offset, origin);
    if (systemverilog_local_wave_enabled && !offset && delay == 0U
        && update_domain == SignalUpdateDomain::systemverilog_active
        && origin.process_domain == ProcessSchedulingDomain::systemverilog
        && origin.phase == SchedulerPhase::active
        && signal < signals.size()
        && signal < region_authoritative_component_by_signal.size()) {
        const auto component
            = region_authoritative_component_by_signal[signal];
        if (component < region_local_wave_state_by_component.size()
            && component < region_activation_programs.size()
            && region_local_wave_state_by_component[component]
            && region_local_wave_state_by_component[component]->generation
                == region_runtime_generation
            && region_activation_programs[component]) {
            const auto& kernel
                = region_activation_programs[component]->activation_kernel;
            const auto output = std::ranges::find(kernel.outputs, signal,
                &RegionConeOutputBinding::signal);
            if (output != kernel.outputs.end() && output->owner == process
                && output->offset == 0U
                && output->width == signals[signal].initial_value.width()
                && std::ranges::find(kernel.internal_signals, signal)
                    != kernel.internal_signals.end()) {
                systemverilog_update_slots[token.slot]
                    .invalidate_local_wave_bank_on_commit = true;
            }
        }
    }
    try {
        const auto descriptor
            = detail::make_scheduler_task_descriptor<
                SystemVerilogUpdateToken,
                &Interpreter::Impl::dispatch_systemverilog_update>(token);
        scheduler.schedule_internal_systemverilog_at(
            time, origin.phase, process, descriptor);
    } catch (...) {
        release_systemverilog_update(token);
        throw;
    }
}

void Interpreter::Impl::prepare_systemverilog_update_pool()
{
    std::size_t capacity { };
    const auto count_if_tagged = [&capacity](const auto* const operation) {
        if (operation == nullptr
            || operation->domain == SignalUpdateDomain::generic) {
            return;
        }
        if (capacity == no_systemverilog_update_slot) {
            throw std::length_error {
                "too many tagged SystemVerilog update operations"
            };
        }
        ++capacity;
    };
    const auto count_tagged_startup_write = [&capacity] {
        if (capacity == no_systemverilog_update_slot) {
            throw std::length_error {
                "too many tagged SystemVerilog update operations"
            };
        }
        ++capacity;
    };

    // Static kernel members never execute on the scheduler (the kernel
    // stages its few host updates through the growable pool).
    const bool kernel = static_cast<bool>(static_kernel);
    for (ProcessId id = 0U; id < processes.size(); ++id) {
        if (kernel && id < fusion_dormant_process.size()
            && fusion_dormant_process[id] != 0U) {
            continue;
        }
        if (const auto* const compact = processes.compact_constant(id);
            compact != nullptr && compact->startup_write_bank != nullptr) {
            if (compact->startup_write_bank->update_domain
                != SignalUpdateDomain::generic) {
                count_tagged_startup_write();
            }
            continue;
        }
        const auto program = processes.program_view(id);
        for (const auto& operation : program.operations()) {
            count_if_tagged(operation_get_if<WriteUpdate>(&operation));
            count_if_tagged(operation_get_if<WriteAfter>(&operation));
            count_if_tagged(operation_get_if<WriteUpdateSlice>(&operation));
            count_if_tagged(operation_get_if<WriteAfterSlice>(&operation));
            count_if_tagged(
                operation_get_if<WriteUpdateDynamicSlice>(&operation));
            count_if_tagged(
                operation_get_if<WriteAfterDynamicSlice>(&operation));
            count_if_tagged(
                operation_get_if<WriteUpdateDynamicPartSlice>(&operation));
            count_if_tagged(
                operation_get_if<WriteAfterDynamicPartSlice>(&operation));
        }
    }

    systemverilog_update_slots.resize(
        std::max(capacity, systemverilog_update_slots.size()));
    systemverilog_update_free_head = no_systemverilog_update_slot;
    for (std::size_t index = 0U;
         index < systemverilog_update_slots.size();
         ++index) {
        auto& slot = systemverilog_update_slots[index];
        slot.value = PackedLogic4 { };
        slot.region_internal_output = false;
        slot.invalidate_local_wave_bank_on_commit = false;
        slot.region_local_component = no_systemverilog_update_slot;
        slot.region_local_generation = 0U;
        slot.occupied = false;
        slot.reserved = false;
        slot.next_free = no_systemverilog_update_slot;
        if (slot.generation
            >= std::numeric_limits<std::uint64_t>::max() - 1U) {
            slot.retired = true;
        }
        if (slot.retired) {
            continue;
        }
        ++slot.generation;
        slot.next_free = systemverilog_update_free_head;
        systemverilog_update_free_head = index;
    }
}

Interpreter::Impl::SystemVerilogUpdateToken
Interpreter::Impl::reserve_systemverilog_update(
    const ProcessId process,
    const SignalId signal,
    PackedLogic4 value,
    const std::optional<std::size_t> offset,
    const SignalChangeOrigin origin)
{
    std::size_t index { };
    if (systemverilog_update_free_head
        != no_systemverilog_update_slot) {
        index = systemverilog_update_free_head;
        auto& slot = systemverilog_update_slots[index];
        systemverilog_update_free_head = slot.next_free;
    } else {
        if (systemverilog_update_slots.size()
            == no_systemverilog_update_slot) {
            throw std::length_error {
                "too many pending SystemVerilog updates"
            };
        }
        index = systemverilog_update_slots.size();
        systemverilog_update_slots.emplace_back();
    }

    auto& slot = systemverilog_update_slots[index];
    slot.process = process;
    slot.signal = signal;
    slot.value = std::move(value);
    slot.offset = offset;
    slot.origin = origin;
    slot.region_internal_output = false;
    slot.invalidate_local_wave_bank_on_commit = false;
    slot.prepared_output_batch.reset();
    slot.prepared_output_slot = no_systemverilog_update_slot;
    slot.prepared_output_dispatch_ordinal = no_systemverilog_update_slot;
    slot.region_local_component = no_systemverilog_update_slot;
    slot.region_local_generation = 0U;
    slot.next_free = no_systemverilog_update_slot;
    slot.occupied = true;
    slot.reserved = false;
    return { this, index, slot.generation };
}

void Interpreter::Impl::reserve_systemverilog_update_slots(
    const std::span<std::size_t> reserved_slots)
{
    std::size_t reserved_count { };
    try {
        for (; reserved_count < reserved_slots.size(); ++reserved_count) {
            std::size_t index { };
            if (systemverilog_update_free_head
                != no_systemverilog_update_slot) {
                index = systemverilog_update_free_head;
                auto& slot = systemverilog_update_slots[index];
                systemverilog_update_free_head = slot.next_free;
                slot.next_free = no_systemverilog_update_slot;
                slot.region_internal_output = false;
                slot.invalidate_local_wave_bank_on_commit = false;
                slot.prepared_output_batch.reset();
                slot.prepared_output_slot = no_systemverilog_update_slot;
                slot.prepared_output_dispatch_ordinal
                    = no_systemverilog_update_slot;
                slot.region_local_component = no_systemverilog_update_slot;
                slot.region_local_generation = 0U;
                slot.reserved = true;
            } else {
                if (systemverilog_update_slots.size()
                    == no_systemverilog_update_slot) {
                    throw std::length_error {
                        "too many reserved SystemVerilog updates"
                    };
                }
                index = systemverilog_update_slots.size();
                systemverilog_update_slots.emplace_back();
                systemverilog_update_slots[index].region_internal_output = false;
                systemverilog_update_slots[index]
                    .invalidate_local_wave_bank_on_commit = false;
                systemverilog_update_slots[index]
                    .prepared_output_batch.reset();
                systemverilog_update_slots[index].prepared_output_slot
                    = no_systemverilog_update_slot;
                systemverilog_update_slots[index]
                    .prepared_output_dispatch_ordinal
                    = no_systemverilog_update_slot;
                systemverilog_update_slots[index].region_local_component
                    = no_systemverilog_update_slot;
                systemverilog_update_slots[index].region_local_generation = 0U;
                systemverilog_update_slots[index].reserved = true;
            }
            reserved_slots[reserved_count] = index;
        }
    } catch (...) {
        cancel_systemverilog_update_slots(
            reserved_slots.first(reserved_count));
        throw;
    }
}

void Interpreter::Impl::cancel_systemverilog_update_slots(
    const std::span<const std::size_t> reserved_slots) noexcept
{
    for (const auto index : reserved_slots) {
        if (index >= systemverilog_update_slots.size()) {
            continue;
        }
        auto& slot = systemverilog_update_slots[index];
        if (!slot.reserved || slot.occupied) {
            continue;
        }
        slot.reserved = false;
        slot.next_free = systemverilog_update_free_head;
        systemverilog_update_free_head = index;
    }
}

Interpreter::Impl::SystemVerilogUpdateToken
Interpreter::Impl::commit_reserved_systemverilog_update(
    const std::size_t index,
    const ProcessId process,
    const SignalId signal,
    PackedLogic4 value,
    const SignalChangeOrigin origin,
    const std::optional<std::size_t> offset,
    const bool region_internal_output,
    const std::size_t region_local_component,
    const std::uint64_t region_local_generation) noexcept
{
    if (index >= systemverilog_update_slots.size()) {
        std::terminate();
    }
    auto& slot = systemverilog_update_slots[index];
    if (!slot.reserved || slot.occupied || slot.retired) {
        std::terminate();
    }
    slot.process = process;
    slot.signal = signal;
    slot.value = std::move(value);
    slot.offset = offset;
    slot.origin = origin;
    slot.prepared_output_batch.reset();
    slot.prepared_output_slot = no_systemverilog_update_slot;
    slot.prepared_output_dispatch_ordinal = no_systemverilog_update_slot;
    slot.region_internal_output = region_internal_output;
    slot.invalidate_local_wave_bank_on_commit = false;
    slot.region_local_component = region_local_component;
    slot.region_local_generation = region_local_generation;
    slot.next_free = no_systemverilog_update_slot;
    slot.reserved = false;
    slot.occupied = true;
    return { this, index, slot.generation };
}

void Interpreter::Impl::release_systemverilog_update(
    const SystemVerilogUpdateToken& token) noexcept
{
    if (token.owner != this
        || token.slot >= systemverilog_update_slots.size()) {
        return;
    }
    auto& slot = systemverilog_update_slots[token.slot];
    if (!slot.occupied || slot.generation != token.generation) {
        return;
    }
    if (slot.prepared_output_batch) {
        slot.prepared_output_batch->retire_ticket(
            slot.prepared_output_slot, true);
        slot.prepared_output_batch->retire_dispatch_member(
            slot.prepared_output_dispatch_ordinal, true);
        slot.prepared_output_batch.reset();
    }
    slot.prepared_output_slot = no_systemverilog_update_slot;
    slot.prepared_output_dispatch_ordinal = no_systemverilog_update_slot;
    slot.value = PackedLogic4 { };
    slot.process = ProcessId { };
    slot.signal = SignalId { };
    slot.offset.reset();
    slot.origin = { };
    slot.region_internal_output = false;
    slot.invalidate_local_wave_bank_on_commit = false;
    slot.region_local_component = no_systemverilog_update_slot;
    slot.region_local_generation = 0U;
    slot.occupied = false;
    slot.reserved = false;
    slot.next_free = no_systemverilog_update_slot;
    if (slot.generation
        >= std::numeric_limits<std::uint64_t>::max() - 1U) {
        slot.retired = true;
        return;
    }
    ++slot.generation;
    slot.next_free = systemverilog_update_free_head;
    systemverilog_update_free_head = token.slot;
}

void Interpreter::Impl::clear_systemverilog_update_pool() noexcept
{
    systemverilog_update_free_head = no_systemverilog_update_slot;
    for (std::size_t index = 0U;
         index < systemverilog_update_slots.size();
         ++index) {
        auto& slot = systemverilog_update_slots[index];
        if (slot.prepared_output_batch) {
            slot.prepared_output_batch->retire_ticket(
                slot.prepared_output_slot, true);
            slot.prepared_output_batch->retire_dispatch_member(
                slot.prepared_output_dispatch_ordinal, true);
            slot.prepared_output_batch.reset();
        }
        slot.prepared_output_slot = no_systemverilog_update_slot;
        slot.prepared_output_dispatch_ordinal
            = no_systemverilog_update_slot;
        slot.value = PackedLogic4 { };
        slot.process = ProcessId { };
        slot.signal = SignalId { };
        slot.offset.reset();
        slot.origin = { };
        slot.region_internal_output = false;
        slot.invalidate_local_wave_bank_on_commit = false;
        slot.region_local_component = no_systemverilog_update_slot;
        slot.region_local_generation = 0U;
        slot.occupied = false;
        slot.reserved = false;
        slot.next_free = no_systemverilog_update_slot;
        if (slot.generation
            >= std::numeric_limits<std::uint64_t>::max() - 1U) {
            slot.retired = true;
        }
        if (slot.retired) {
            continue;
        }
        ++slot.generation;
        slot.next_free = systemverilog_update_free_head;
        systemverilog_update_free_head = index;
    }
}

void Interpreter::Impl::dispatch_systemverilog_update(
    Scheduler&,
    const SystemVerilogUpdateToken& token)
{
    if (token.owner == nullptr
        || token.slot >= token.owner->systemverilog_update_slots.size()) {
        return;
    }
    auto& owner = *token.owner;
    auto& slot = owner.systemverilog_update_slots[token.slot];
    if (!slot.occupied || slot.generation != token.generation) {
        return;
    }

    // Preserve the original slot until a required role-journal flush has
    // completed. Scheduler callbacks are removed before invocation, so a
    // failure here is a fatal run error rather than a retryable task.
    owner.require_region_forwarding_role_journal_flushed_for_signal(
        slot.signal);
    const auto process = slot.process;
    const auto signal = slot.signal;
    auto value = std::move(slot.value);
    const auto offset = slot.offset;
    const auto origin = slot.origin;
    const bool invalidate_local_wave_bank
        = slot.invalidate_local_wave_bank_on_commit;
    auto prepared_batch = std::move(slot.prepared_output_batch);
    const auto prepared_slot = slot.prepared_output_slot;
    const auto dispatch_ordinal = slot.prepared_output_dispatch_ordinal;
    slot.prepared_output_slot = no_systemverilog_update_slot;
    slot.prepared_output_dispatch_ordinal
        = no_systemverilog_update_slot;
    owner.release_systemverilog_update(token);
    struct RetirePreparedGroupMember {
        std::shared_ptr<RegionPreparedOutputBatchState>& batch;
        std::size_t slot;
        std::size_t ordinal;
        bool cancel { true };

        ~RetirePreparedGroupMember()
        {
            if (batch) {
                batch->retire_ticket(slot, cancel);
                batch->retire_dispatch_member(ordinal, cancel);
            }
        }
    } retire { prepared_batch, prepared_slot, dispatch_ordinal };
    if (invalidate_local_wave_bank
        && owner.systemverilog_wave_profile_enabled) {
        ++owner.systemverilog_wave_profile_a2_ordinary_internal_updates;
    }
    if (invalidate_local_wave_bank) {
        owner.invalidate_region_local_wave_signal(signal);
    }
    const auto profile_start = owner.commit_signal_profile_enabled
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point { };
    if (offset) {
        owner.commit_driver_slice(
            process, signal, std::move(value), *offset, origin, false);
    } else {
        owner.commit_driver(process, signal, std::move(value), origin, false);
    }
    if (owner.commit_signal_profile_enabled) {
        const auto elapsed = std::chrono::duration_cast<
            std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - profile_start);
        auto& rows = owner.commit_signal_profile_rows;
        if (signal >= rows.size()) {
            rows.resize(static_cast<std::size_t>(signal) + 1U);
        }
        auto& row = rows[signal];
        ++row.calls;
        row.slice_calls += offset ? 1U : 0U;
        row.nanoseconds += static_cast<std::uint64_t>(elapsed.count());
    }
    retire.cancel = false;
}

void Interpreter::Impl::dispatch_region_internal_update(
    Scheduler&,
    const SystemVerilogUpdateToken& token)
{
    if (token.owner == nullptr
        || token.slot >= token.owner->systemverilog_update_slots.size()) {
        return;
    }
    auto& owner = *token.owner;
    auto& slot = owner.systemverilog_update_slots[token.slot];
    if (!slot.occupied || slot.generation != token.generation) {
        return;
    }

    const auto process = slot.process;
    const auto signal = slot.signal;
    const auto component = slot.region_local_component;
    const auto generation = slot.region_local_generation;
    const auto origin = slot.origin;
    const bool internal_output = slot.region_internal_output;
    bool publication_committed { };
    bool role_deferred { };

    struct PreparedDispatch {
        std::shared_ptr<RegionPreparedOutputBatchState> batch;
        std::size_t slot { no_systemverilog_update_slot };
        std::size_t ordinal { no_systemverilog_update_slot };
    };
    const auto detach_and_release = [&]() {
        PreparedDispatch dispatch {
            std::move(slot.prepared_output_batch),
            slot.prepared_output_slot,
            slot.prepared_output_dispatch_ordinal,
        };
        slot.prepared_output_slot = no_systemverilog_update_slot;
        slot.prepared_output_dispatch_ordinal
            = no_systemverilog_update_slot;
        owner.release_systemverilog_update(token);
        return dispatch;
    };
    struct RetirePreparedDispatch {
        PreparedDispatch& dispatch;
        bool cancel { true };
        ~RetirePreparedDispatch()
        {
            if (dispatch.batch) {
                dispatch.batch->retire_ticket(dispatch.slot, cancel);
                dispatch.batch->retire_dispatch_member(
                    dispatch.ordinal, cancel);
            }
        }
    };

    if (slot.prepared_output_batch
        && slot.prepared_output_slot != no_systemverilog_update_slot
        && internal_output) {
        // Prepared native publications do not use the private role overlay.
        // Flush before touching their original token or slot value.
        owner.require_region_forwarding_role_journal_flushed_for_signal(
            signal);
        auto& prepared_batch = *slot.prepared_output_batch;
        PreparedOutputTicketResult result;
        try {
            result = owner.try_publish_region_prepared_output(
                prepared_batch, slot.prepared_output_slot, process, signal,
                slot.value, origin, publication_committed);
        } catch (...) {
            if (publication_committed) {
                auto dispatch = detach_and_release();
                RetirePreparedDispatch retire { dispatch, false };
            }
            throw;
        }
        if (result != PreparedOutputTicketResult::declined) {
            auto dispatch = detach_and_release();
            RetirePreparedDispatch retire { dispatch, false };
            if (owner.systemverilog_wave_profile_enabled) {
                ++owner.systemverilog_wave_profile_a2_local_update_dispatches;
            }
            return;
        }
    }

    if ((!slot.prepared_output_batch
            || slot.prepared_output_slot == no_systemverilog_update_slot)
        && internal_output
        && component != no_systemverilog_update_slot) {
        try {
            if (owner.publish_region_internal_value(
                    component, generation, process, signal, slot.value,
                    origin, nullptr, no_systemverilog_update_slot,
                    &role_deferred, &publication_committed)) {
                auto dispatch = detach_and_release();
                RetirePreparedDispatch retire { dispatch, false };
                if (owner.systemverilog_wave_profile_enabled) {
                    ++owner.systemverilog_wave_profile_a2_local_update_dispatches;
                }
                return;
            }
        } catch (...) {
            if (publication_committed) {
                auto dispatch = detach_and_release();
                RetirePreparedDispatch retire { dispatch, false };
            }
            throw;
        }
    }

    // A decline can use the ordinary commit only after the private applied
    // prefix is visible in A4. Keep the token and its value intact if that
    // fallible barrier refuses publication.
    owner.require_region_forwarding_role_journal_flushed_for_signal(signal);
    auto value = std::move(slot.value);
    auto dispatch = detach_and_release();
    RetirePreparedDispatch retire { dispatch };
    if (owner.systemverilog_wave_profile_enabled) {
        if (internal_output) {
            ++owner.systemverilog_wave_profile_a2_ordinary_internal_updates;
        }
        ++owner.systemverilog_wave_profile_a2_local_update_fallbacks;
    }
    if (internal_output) {
        owner.invalidate_region_local_wave_signal(signal);
    }
    owner.commit_driver(process, signal, std::move(value), origin, false);
    retire.cancel = false;
}

void Interpreter::Impl::invalidate_region_local_wave_signal(
    const SignalId signal) noexcept
{
    if (signal >= region_authoritative_component_by_signal.size()) {
        return;
    }
    const auto component = region_authoritative_component_by_signal[signal];
    if (component >= region_local_wave_state_by_component.size()) {
        return;
    }
    const auto& state = region_local_wave_state_by_component[component];
    if (state && state->generation == region_runtime_generation) {
        state->seeded = false;
    }
}

bool Interpreter::Impl::match_region_fanout_sensitivity_ranges(
    const std::size_t component,
    const SignalId signal,
    const std::uint64_t generation,
    const std::size_t range_offset,
    const std::size_t range_count,
    const PackedLogic4& previous,
    const PackedLogic4& current,
    bool& changed,
    bool* const has_partial_range) const noexcept
{
    changed = false;
    if (has_partial_range != nullptr) {
        *has_partial_range = false;
    }
    if (!region_graph || generation == 0U
        || generation != region_runtime_generation
        || signal >= region_graph->signals().size()
        || range_count == 0U
        || range_offset > region_grouped_fanout_sensitivity_ranges.size()
        || range_count
            > region_grouped_fanout_sensitivity_ranges.size() - range_offset) {
        return false;
    }

    const auto signal_width
        = region_graph->signals()[signal].descriptor.width;
    if (signal_width == 0U || previous.width() != signal_width
        || current.width() != signal_width) {
        return false;
    }

    bool any_partial_range { };
    for (std::size_t index = 0U; index < range_count; ++index) {
        const auto& range
            = region_grouped_fanout_sensitivity_ranges[range_offset + index];
        if (range.width == 0U || range.offset >= signal_width
            || range.width > signal_width - range.offset) {
            return false;
        }
        const bool partial_range = range.offset != 0U
            || range.width != signal_width;
        any_partial_range |= partial_range;
        if (has_partial_range != nullptr) {
            *has_partial_range |= partial_range;
        }
        changed |= sensitivity_range_changed(previous, current,
            range.offset, range.width);
    }

    // Ranged bindings are admitted only for the V2-shaped member contract.
    // Legacy whole-signal grouped maps can still retain trigger-region masks.
    if (any_partial_range
        && (component >= region_activation_programs.size()
            || !region_activation_programs[component]
            || !region_activation_programs[component]
                    ->activation_kernel.program.static_trigger_regions.empty()
            || std::ranges::find(
                   region_activation_programs[component]
                       ->activation_kernel.internal_signals,
                   signal)
                == region_activation_programs[component]
                       ->activation_kernel.internal_signals.end())) {
        return false;
    }
    return true;
}

void Interpreter::Impl::notify_region_local_value_change(
    const std::size_t component,
    const SignalId signal,
    const PackedLogic4& previous,
    const PackedLogic4& current,
    const SignalChangeOrigin origin,
    const RegionPreparedOutputBatchState* const prepared_batch,
    const std::size_t prepared_slot)
{
    if (component >= region_authoritative_state_by_component.size()) {
        return;
    }
    auto* const state
        = region_authoritative_state_by_component[component].get();
    if (state == nullptr || !state->valid()
        || state->generation() != region_runtime_generation) {
        return;
    }

    if (prepared_batch != nullptr
        && prepared_batch->successor_masks_verified
        && prepared_batch->component == component
        && prepared_batch->runtime_generation == region_runtime_generation
        && prepared_slot < prepared_batch->descriptors.size()
        && prepared_slot < prepared_batch->successor_masks.size()
        && prepared_slot < prepared_batch->expected_successor_masks.size()
        && prepared_batch->descriptors[prepared_slot].signal_id == signal
        && prepared_batch->successor_masks[prepared_slot]
            == prepared_batch->expected_successor_masks[prepared_slot]
        && previous != current) {
        if (schedule_prepared_successor_readers(
                signal, previous, current, origin,
                *prepared_batch, prepared_slot)
            || schedule_systemverilog_grouped_fanout(
                signal, previous, current, origin,
                prepared_batch, prepared_slot)) {
            return;
        }
    }

    state->fanout().mark_transition(signal, previous, current,
        EdgeKind::any, state->readiness());
    Interpreter::Impl::StaticTransitionMatches transition { };
    if (previous.width() == 1U && current.width() == 1U) {
        const auto low_logic4 = [](const PackedLogic4& value) {
            return value.is_logic9()
                ? to_logic4(value.get_logic9(0U)) : value.get(0U);
        };
        transition = decode_static_transition(
            low_logic4(previous), low_logic4(current));
        if (transition.posedge) {
            state->fanout().mark_transition(signal, previous, current,
                EdgeKind::posedge, state->readiness());
        }
        if (transition.negedge) {
            state->fanout().mark_transition(signal, previous, current,
                EdgeKind::negedge, state->readiness());
        }
    }
    if (schedule_systemverilog_grouped_fanout(
            signal, previous, current, origin)) {
        return;
    }
    const auto fanout = static_fanout_for(signal);
    std::size_t index { };
    while (index < fanout.size()) {
        const auto process = fanout[index].process;
        std::uint64_t fallback_mask { };
        bool matched_clause { };
        do {
            const auto& sensitivity = fanout[index];
            bool matches { };
            switch (sensitivity.edge) {
            case EdgeKind::any:
                matches = sensitivity.width == 0U
                        && sensitivity.offset == 0U
                    ? previous != current
                    : sensitivity_range_changed(previous, current,
                        sensitivity.offset, sensitivity.width);
                break;
            case EdgeKind::posedge:
            case EdgeKind::negedge:
                matches = transition.matches(sensitivity.edge);
                break;
            case EdgeKind::transaction:
                // Transactions have their own notification path.
                break;
            }
            if (matches) {
                matched_clause = true;
                fallback_mask |= sensitivity.static_trigger_mask;
            }
            ++index;
        } while (index < fanout.size()
            && fanout[index].process == process);

        auto& member = get_process(process);
        std::uint64_t grouped_mask { };
        const bool grouped_ready = region_take_ready(
            signal, process, grouped_mask);
        if (!grouped_ready && !matched_clause) {
            continue;
        }
        member.static_trigger_mask |= grouped_mask | fallback_mask;
        merge_systemverilog_readiness_mask(process,
            grouped_mask | fallback_mask);
        if (member.waiting_on_static) {
            queue_static_next_delta(process, origin);
            if (systemverilog_wave_profile_enabled) {
                ++systemverilog_wave_profile_a2_grouped_fanout_members;
            }
        }
    }
}

Interpreter::Impl::PreparedOutputTicketResult
Interpreter::Impl::try_publish_region_prepared_output(
    RegionPreparedOutputBatchState& batch,
    const std::size_t slot,
    const ProcessId process,
    const SignalId signal,
    PackedLogic4& value,
    const SignalChangeOrigin origin,
    bool& publication_committed)
{
    publication_committed = false;
    const auto decline = [&] {
        batch.cancelled = true;
        if (systemverilog_wave_profile_enabled) {
            ++systemverilog_wave_profile_prepared_output_fallbacks;
        }
        return PreparedOutputTicketResult::declined;
    };
    if (!batch.active || batch.cancelled
        || batch.runtime_generation != region_runtime_generation
        || slot >= batch.slot_storage.size()
        || slot >= batch.descriptors.size()
        || slot >= batch.ticket_pending.size()
        || batch.ticket_pending[slot] == 0U
        || batch.component >= region_authoritative_state_by_component.size()) {
        return decline();
    }
    const auto* const state
        = region_authoritative_state_by_component[batch.component].get();
    if (state == nullptr || !state->valid()
        || state->generation() != batch.component_generation
        || state->values().revision() != batch.expected_value_revision) {
        return decline();
    }
    const auto& descriptor = batch.descriptors[slot];
    const auto& storage = batch.slot_storage[slot];
    if (descriptor.selected != 1U || descriptor.signal_id != signal
        || descriptor.owner_id != process || descriptor.width == 0U
        || descriptor.width > 64U || descriptor.width != value.width()
        || descriptor.word_count != 1U
        || descriptor.value_kind != RegionPreparedOutputValueKindV1::logic4
        || value.is_logic9() || storage.changed > 1U
        || storage.value_ready != storage.changed
        || storage.transaction_ready != 1U) {
        return decline();
    }
    if (!publish_region_internal_value(batch.component,
            batch.runtime_generation, process, signal, value, origin,
            &batch, slot, nullptr, &publication_committed)) {
        return decline();
    }
    ++batch.sealed_tickets;
    if (systemverilog_wave_profile_enabled) {
        ++systemverilog_wave_profile_prepared_output_seals;
    }
    return PreparedOutputTicketResult::sealed;
}

bool Interpreter::Impl::publish_region_internal_value(
    const std::size_t component,
    const std::uint64_t generation,
    const ProcessId process,
    const SignalId signal,
    const PackedLogic4& value,
    const SignalChangeOrigin origin,
    RegionPreparedOutputBatchState* const prepared_batch,
    const std::size_t prepared_slot,
    bool* const role_deferred,
    bool* const publication_committed,
    const RegionOutputPublicationKind publication_kind,
    const bool fanout_pre_reserved)
{
    if (role_deferred != nullptr) {
        *role_deferred = false;
    }
    if (publication_committed != nullptr) {
        *publication_committed = false;
    }
    if (!systemverilog_local_wave_enabled || !region_graph
        || generation != region_runtime_generation
        || component >= region_activation_programs.size()
        || !region_activation_programs[component]
        || !region_graph->component_epochs_current(component)
        || scheduler.trace_hook_installed()
        || region_graph->signals().size() != signals.size()
        || signal >= signals.size()
        || signal >= region_authoritative_component_by_signal.size()
        || region_authoritative_component_by_signal[signal] != component
        || component >= region_authoritative_state_by_component.size()
        || std::ranges::find(
            region_activation_programs[component]->activation_kernel
                .internal_signals, signal)
            == region_activation_programs[component]->activation_kernel
                   .internal_signals.end()
        || origin.process_domain != ProcessSchedulingDomain::systemverilog
        || origin.phase != SchedulerPhase::active
        || signal_change_hook || stored_signal_change_hook
        || driver_change_hook || scalar_signal_change_hook
        || container_object_change_hook || container_element_change_hook
        || native_signal_observation_any_hook
        || native_signal_observation_required_hook) {
        return false;
    }

    auto* const state = region_authoritative_state_by_component[component].get();
    if (state == nullptr || !state->valid()
        || state->generation() != generation
        || !state->values().packed_slots_bound()
        || !state->values().packed_signal_slots_bound(signal)
        || signal >= signal_transactions.size()
        || signal >= signal_events.size()
        || signal >= signal_event_scheduling_stamps.size()
        || signal >= signal_value_revisions.size()
        || signal >= direct_signal_materialization_pending.size()
        || direct_signal_materialization_pending[signal] != 0U
        || signal >= driver_values.size()
        || signal >= driven_values.size()
        || signal >= signal_last_values.size()
        || signal >= external_driver_values.size()
        || signal >= forced_values.size()
        || signal >= forced_masks.size()
        || signal >= forced_driver_values.size()
        || signal >= forced_driver_masks.size()
        || signal >= direct_signal_aval.size()
        || signal >= direct_signal_bval.size()
        || signal >= direct_signal_last_aval.size()
        || signal >= direct_signal_last_bval.size()
        || signal >= direct_wide_signal_offsets.size()
        || signal >= dynamic_fanout.size() || !dynamic_fanout[signal].empty()
        || signal >= signal_transaction_observed.size()
        || signal_transaction_observed[signal]
        || (signal < sampled_history_keys_by_clock.size()
            && !sampled_history_keys_by_clock[signal].empty())
        || signal >= signal_container_aliases.size()
        || !signal_container_aliases[signal].empty()
        || signal >= signal_container_element_aliases.size()
        || signal_container_element_aliases[signal]
        || signal >= signal_container_aggregate_aliases.size()
        || signal_container_aggregate_aliases[signal]
        || signal >= switch_endpoint_adjacency.size()
        || !switch_endpoint_adjacency[signal].empty()
        || signal >= module_path_destination_mask.size()
        || module_path_destination_mask[signal] != 0U
        || monitor_watches(signal)) {
        return false;
    }

    const auto& runtime_signal = signals[signal];
    if ((runtime_signal.resolution != ResolutionKind::none
            && runtime_signal.resolution != ResolutionKind::sv_wire)
        || runtime_signal.value_kind != ValueKind::logic4
        || runtime_signal.initial_value.width() == 0U
        || runtime_signal.initial_value.is_logic9()
        || runtime_signal.has_implicit_driver
        || runtime_signal.has_charge_strength
        || runtime_signal.event_variable
        || runtime_signal.public_value_reference_exposed
        || runtime_signal.systemverilog_scalar
            != SystemVerilogScalarKind::None
        || external_driver_values[signal] || forced_values[signal]
        || forced_masks[signal] || forced_driver_values[signal]
        || forced_driver_masks[signal]
        || value.width() != runtime_signal.initial_value.width()
        || value.is_logic9()) {
        return false;
    }

    const auto& kernel
        = region_activation_programs[component]->activation_kernel;
    const auto binding = std::ranges::find(kernel.outputs, signal,
        &RegionConeOutputBinding::signal);
    const auto& graph_signal = region_graph->signals()[signal];
    if (binding == kernel.outputs.end() || binding->owner != process
        || binding->offset != 0U
        || binding->width != runtime_signal.initial_value.width()
        || binding->value_kind != ValueKind::logic4
        || binding->domain != SignalUpdateDomain::systemverilog_active
        || binding->update_kind != RegionUpdateKind::systemverilog_active
        || binding->publication_kind != publication_kind
        || (fanout_pre_reserved
            && publication_kind
                != RegionOutputPublicationKind::blocking_immediate)
        || graph_signal.writers.size() != 1U
        || graph_signal.writers.front().process != process
        || process >= region_graph->processes().size()) {
        return false;
    }

    const auto& layout = state->values().layout();
    if (!layout.contains(signal)) {
        return false;
    }
    const auto& signal_layout = layout.signal(signal);
    const auto owners = layout.owners(signal);
    const bool stored_owner_alias
        = owners.size() == 1U && owners.front().process == process
        && owners.front().aliases_stored;
    const bool driverless_blocking_alias
        = stored_owner_alias
        && runtime_signal.resolution == ResolutionKind::none
        && graph_signal.descriptor.resolution == ResolutionKind::none
        && publication_kind
            == RegionOutputPublicationKind::blocking_immediate
        && fanout_pre_reserved && prepared_batch == nullptr
        && driver_values[signal].empty();
    const auto* const raw_driver = driver_values[signal].find(process);
    if (signal_layout.storage_class != SignalDriverStorageClass::single_owner
        || signal_layout.value_kind != ValueKind::logic4
        || signal_layout.width != runtime_signal.initial_value.width()
        || signal_layout.word_count == 0U
        || owners.size() != 1U || owners.front().process != process
        || (stored_owner_alias
            ? !driverless_blocking_alias
            : (raw_driver == nullptr || driver_values[signal].size() != 1U))
        || !state->values().packed_owner_slot_bound(signal, process)) {
        return false;
    }
    auto* const local_wave_state
        = component < region_local_wave_state_by_component.size()
        ? region_local_wave_state_by_component[component].get()
        : nullptr;
    if (local_wave_state == nullptr
        || local_wave_state->generation != generation
        || !local_wave_state->seeded
        || local_wave_state->authoritative_revision
            != state->values().revision()
        || !local_wave_state->activation.can_publish_internal_update(
            signal, process, signal_layout.width)) {
        return false;
    }
    const auto& cached_internal
        = local_wave_state->activation.internal_state(signal);
    const auto wide_offset = direct_wide_signal_offsets[signal];
    const auto word_count = signal_layout.word_count;
    const auto value_aval = value.aval_words();
    const auto value_bval = value.bval_words();
    if (cached_internal.current.width() != signal_layout.width
        || cached_internal.current.is_logic9()
        || value_aval.size() != word_count
        || value_bval.size() != word_count
        || wide_offset > direct_wide_signal_aval.size()
        || word_count > direct_wide_signal_aval.size() - wide_offset
        || wide_offset > direct_wide_signal_bval.size()
        || word_count > direct_wide_signal_bval.size() - wide_offset) {
        return false;
    }

    const bool narrow_word = signal_layout.width <= 64U;
    const Logic4Word old_word = narrow_word
        ? cached_internal.current.unchecked_low_word() : Logic4Word { };
    const Logic4Word new_word
        = narrow_word ? value.unchecked_low_word() : Logic4Word { };
    const bool changed = cached_internal.current != value;
    const auto revision_before = state->values().revision();
    if (prepared_batch
        && (signal_layout.width > 64U
            || prepared_slot >= prepared_batch->slot_storage.size()
            || prepared_batch->expected_value_revision != revision_before
            || prepared_batch->component_generation != state->generation()
            || !process_signal_access_inventory_complete
            || !process_signal_access_is_complete(process)
            || execution_point_hook || has_bidirectional_switches
            || !module_timing_checks.empty()
            || native_signal_has_runtime_dependency(signal, true)
            || driver_values[signal].size() != 1U
            || !static_fanout_indices_for(
                    signal, EdgeKind::transaction).empty())) {
        return false;
    }
    AuthoritativeSignalPlanes::PreparedMutation* deferred_role_mutation { };
    std::size_t deferred_role_output_index
        = no_systemverilog_update_slot;
    std::uint64_t deferred_role_callback_order { };
    if (!prepared_batch && local_wave_state->forwarding_results) {
        auto& bank = *local_wave_state->forwarding_results;
        if (bank.role_journal_enabled) {
            const auto& outputs
                = region_activation_programs[component]
                      ->activation_kernel.outputs;
            for (std::size_t index = 0U; index < outputs.size(); ++index) {
                if (outputs[index].signal != signal
                    || outputs[index].owner != process) {
                    continue;
                }
                if (deferred_role_mutation != nullptr
                    || index >= bank.prepared_role_mutations.size()
                    || index >= bank.prepared_role_mutation_ready.size()
                    || bank.prepared_role_mutation_ready[index] == 0U) {
                    deferred_role_mutation = nullptr;
                    break;
                }
                deferred_role_output_index = index;
                deferred_role_mutation
                    = &bank.prepared_role_mutations[index];
            }
            if (deferred_role_mutation != nullptr) {
                const auto aval = value.aval_words();
                const auto bval = value.bval_words();
                const auto& prepared_output = outputs[
                    deferred_role_output_index];
                if (prepared_output.width != value.width()
                    || value.is_logic9()
                    || aval.size() != signal_layout.word_count
                    || bval.size() != signal_layout.word_count
                    || deferred_role_mutation->signal != signal
                    || deferred_role_mutation->words.size()
                        != signal_layout.word_count
                    || deferred_role_mutation->preflighted) {
                    deferred_role_mutation = nullptr;
                } else {
                    for (std::size_t word_index = 0U;
                         word_index < deferred_role_mutation->words.size();
                         ++word_index) {
                        const auto& word
                            = deferred_role_mutation->words[word_index];
                        if (word.signal_word
                                != signal_layout.first_value_word
                                    + word_index
                            || word.new_current[0U] != aval[word_index]
                            || word.new_current[1U] != bval[word_index]
                            || word.new_current[2U] != 0U
                            || word.new_current[3U] != 0U
                            || word.new_stored != word.new_current
                            || word.new_owner != word.new_current) {
                            deferred_role_mutation = nullptr;
                            break;
                        }
                    }
                }
            }
            if (deferred_role_mutation != nullptr
                && deferred_role_mutation->any_state_changed
                && (bank.applied_role_mutations.size()
                        >= bank.applied_role_mutations.capacity()
                    || bank.applied_role_metadata.size()
                        >= bank.applied_role_metadata.capacity()
                    || region_forwarding_role_journal_nonempty_components
                        == std::numeric_limits<std::size_t>::max())) {
                deferred_role_mutation = nullptr;
            }
            if (deferred_role_mutation == nullptr) {
                const bool had_applied_rows
                    = !bank.applied_role_mutations.empty()
                    || !bank.applied_role_metadata.empty();
                if (had_applied_rows
                    && !try_flush_region_forwarding_role_journal(component)) {
                    return false;
                }
                bank.role_journal_enabled = false;
                std::ranges::fill(
                    bank.prepared_role_mutation_ready, 0U);
                if (had_applied_rows) {
                    return false;
                }
            }
        }
    }
    if (deferred_role_mutation != nullptr) {
        // The private publication advances activation and event metadata
        // before its applied-role row is appended below. Invalidate the
        // callback observation key before any of those state changes.
        completed_callback_observation_generation = 0U;
    }
    auto& mutation = prepared_batch
        ? prepared_batch->slot_storage[prepared_slot].visible_mutation
        : deferred_role_mutation != nullptr
        ? *deferred_role_mutation
        : local_wave_state->mutation_scratch;
    bool changes_sidecar_state { };
    bool stored_changed { };
    const bool requires_prewrite_unbind
        = state->values().requires_prewrite_unbind();
    std::optional<PackedLogic4> cached_current;
    std::optional<PackedLogic4> cached_raw_driver;
    std::optional<PackedLogic4> previous_for_fanout;
    try {
        if (prepared_batch) {
            const auto& storage = prepared_batch->slot_storage[prepared_slot];
            const auto& descriptor = prepared_batch->descriptors[prepared_slot];
            if (mutation.signal != signal
                || (!mutation.has_owner
                    && !mutation.owner_is_stored_alias)
                || mutation.owner_index != signal_layout.first_owner
                || mutation.words.size() != 1U || mutation.preflighted
                || (storage.changed != 0U) != changed) {
                return false;
            }
            const auto& word = mutation.words.front();
            const auto old_stored
                = driven_values[signal].unchecked_low_word();
            const auto old_owner = raw_driver->value.unchecked_low_word();
            const auto old_last
                = signal_last_values[signal].unchecked_low_word();
            if (word.signal_word != signal_layout.first_value_word
                || word.owner_word != owners.front().first_value_word
                || word.old_current[0U] != old_word.aval
                || word.old_current[1U] != old_word.bval
                || word.new_current[0U] != new_word.aval
                || word.new_current[1U] != new_word.bval
                || word.new_stored != word.new_current
                || word.new_owner != word.new_current
                || word.new_current[2U] != 0U || word.new_current[3U] != 0U
                || storage.old_stored[0U] != old_stored.aval
                || storage.old_stored[1U] != old_stored.bval
                || storage.old_owner[0U] != old_owner.aval
                || storage.old_owner[1U] != old_owner.bval
                || storage.next_last[0U]
                    != (changed ? old_word.aval : old_last.aval)
                || storage.next_last[1U]
                    != (changed ? old_word.bval : old_last.bval)
                || descriptor.next_current_aval != &word.new_current[0U]
                || descriptor.next_current_bval != &word.new_current[1U]
                || descriptor.next_stored_aval != &word.new_stored[0U]
                || descriptor.next_stored_bval != &word.new_stored[1U]
                || descriptor.next_owner_aval != &word.new_owner[0U]
                || descriptor.next_owner_bval != &word.new_owner[1U]) {
                return false;
            }
            // The native entry filled these value fields in private storage.
            // Refresh only publication metadata after prior tickets from this
            // same batch advanced the component revision.
            mutation.prepared_generation = revision_before;
            mutation.any_current_changed = changed;
            mutation.any_stored_changed = old_stored.aval != word.new_stored[0U]
                || old_stored.bval != word.new_stored[1U];
            mutation.any_owner_changed = old_owner.aval != word.new_owner[0U]
                || old_owner.bval != word.new_owner[1U];
            mutation.any_state_changed = mutation.any_current_changed
                || mutation.any_stored_changed || mutation.any_owner_changed;
        } else if (deferred_role_mutation == nullptr) {
            state->values().prepare_owner_change_into(
                mutation, signal, process, value, value, value);
            if (mutation.signal != signal
                || (!mutation.has_owner
                    && !mutation.owner_is_stored_alias)
                || mutation.owner_index != signal_layout.first_owner
                || mutation.words.size() != word_count
                || mutation.preflighted) {
                state->values().cancel_prepared_publication(mutation);
                return false;
            }
        }
        changes_sidecar_state = mutation.any_state_changed;
        stored_changed = mutation.any_stored_changed;
        if (changes_sidecar_state
            && revision_before
                == std::numeric_limits<std::uint64_t>::max()) {
            if (deferred_role_mutation == nullptr) {
                state->values().cancel_prepared_publication(mutation);
            }
            return false;
        }
        cached_current.emplace(value);
        cached_raw_driver.emplace(value);
        if (changed) {
            previous_for_fanout.emplace(cached_internal.current);
        }
        if (deferred_role_mutation == nullptr
            && requires_prewrite_unbind
            && !state->values().begin_prepared_publication(mutation)) {
            state->values().cancel_prepared_publication(mutation);
            request_full_region_recertification();
            demote_region_authoritative_slots(component, false);
            return false;
        }
    } catch (...) {
        // No public state has changed. The original checked commit remains a
        // correct fallback when optional sidecar preparation cannot proceed.
        if (deferred_role_mutation == nullptr
            && (prepared_batch || requires_prewrite_unbind)) {
            state->values().cancel_prepared_publication(mutation);
            request_full_region_recertification();
            demote_region_authoritative_slots(component, false);
        }
        return false;
    }

    if (prepared_batch) {
        // Admission excludes every observer and transaction-sensitive reader.
        // This bookkeeping cannot reenter user code or invalidate the planes.
        try {
            note_signal_transaction(signal, true, origin, true);
        } catch (...) {
            state->values().cancel_prepared_publication(mutation);
            demote_region_authoritative_slots(component, false);
            throw;
        }
    }
    if (deferred_role_mutation == nullptr) {
        state->values().publish(std::move(mutation));
        if (!state->valid()
            || (changes_sidecar_state
                && state->values().revision() == revision_before)) {
            if (prepared_batch || requires_prewrite_unbind) {
                request_full_region_recertification();
                demote_region_authoritative_slots(component, false);
            }
            if (prepared_batch) {
                throw std::logic_error {
                    "preflighted native output publication became invalid"
                };
            }
            return false;
        }
        local_wave_state->authoritative_revision
            = state->values().revision();
        if (prepared_batch) {
            prepared_batch->expected_value_revision
                = state->values().revision();
            if (stored_changed) {
                note_aggregate_leaf_stored_change(signal);
            }
        }
        note_region_authoritative_mirror();
    } else {
        auto& bank = *local_wave_state->forwarding_results;
        if (bank.role_callback_order
            == std::numeric_limits<std::uint64_t>::max()) {
            return false;
        }
        deferred_role_callback_order = ++bank.role_callback_order;
        bank.prepared_role_mutation_ready[
            deferred_role_output_index] = 0U;
    }

    if (!prepared_batch) {
        scheduler.note_signal_transaction(signal);
        signal_transactions[signal]
            = std::pair { scheduler.now(), scheduler.delta() + 1U };
    }
    if (changed) {
        if (narrow_word && deferred_role_mutation == nullptr) {
            direct_signal_last_aval[signal] = old_word.aval;
            direct_signal_last_bval[signal] = old_word.bval;
        }
    }
    if (deferred_role_mutation == nullptr) {
        refresh_direct_signal_planes(signal);
        direct_signal_materialization_pending[signal] = 0U;
    }
    local_wave_state->activation.publish_internal_update(signal, process,
        std::move(*cached_current), std::move(*cached_raw_driver), changed);

    if (changed) {
        ++signal_value_revisions[signal];
        note_aggregate_leaf_current_change(signal);
        if (signal_value_revisions[signal] == 0U) {
            signal_value_revisions[signal] = 1U;
            std::ranges::fill(
                container_materialized_revisions, std::nullopt);
        }
        stamp_signal_event(signal, scheduler.delta() + 1U, origin);
        if (!static_fanout_for(signal).empty()
            && systemverilog_wave_profile_enabled) {
            ++systemverilog_wave_profile_a2_local_fanout_suppressions;
        }
    }
    if (changed) {
        scheduler.note_signal_change(signal);
    }
    if (deferred_role_mutation != nullptr) {
        auto& bank = *local_wave_state->forwarding_results;
        if (mutation.any_state_changed) {
            RegionForwardingAppliedRoleCommitMetadata metadata;
            metadata.output_index = deferred_role_output_index;
            metadata.signal = signal;
            metadata.owner = process;
            metadata.callback_time = scheduler.now();
            metadata.callback_delta = scheduler.delta();
            metadata.callback_systemverilog_round
                = scheduler.systemverilog_round();
            metadata.callback_order = deferred_role_callback_order;
            metadata.origin = origin;
            metadata.expected_signal_event = signal_events[signal];
            metadata.expected_event_stamp
                = signal_event_scheduling_stamps[signal];
            metadata.expected_transaction = signal_transactions[signal];
            metadata.expected_value_revision = signal_value_revisions[signal];
            const bool journal_was_empty
                = bank.applied_role_mutations.empty();
            completed_callback_observation_generation = 0U;
            bank.applied_role_mutations.push_back(std::move(mutation));
            bank.applied_role_metadata.push_back(std::move(metadata));
            if (journal_was_empty) {
                ++region_forwarding_role_journal_nonempty_components;
            }
            if (role_deferred != nullptr) {
                *role_deferred = true;
            }
        }
    }
    if (publication_committed != nullptr) {
        *publication_committed = true;
    }
    if (changed && !fanout_pre_reserved) {
        notify_region_local_value_change(component, signal,
            *previous_for_fanout, value, origin, prepared_batch,
            prepared_slot);
    }
    return true;
}

void Interpreter::Impl::stage_update(
    const std::optional<ProcessId> driver,
    SignalId signal_id,
    PackedLogic4 staged_value,
    const SignalUpdateDomain domain)
{
    (void)get_signal(signal_id);
    if (driven_values[signal_id].width() != staged_value.width()) {
        throw std::invalid_argument("SimIR signal assignment width mismatch");
    }
    staged_value = normalize_signal_value(
        signal_id, std::move(staged_value));
    if (domain != SignalUpdateDomain::generic) {
        if (!driver) {
            throw std::logic_error {
                "a tagged SystemVerilog update has no owning process"
            };
        }
        schedule_systemverilog_update(
            *driver, signal_id, std::move(staged_value), std::nullopt,
            domain);
        return;
    }
    if (driver
        && route_module_path_update(
            *driver, signal_id, staged_value, std::nullopt)) {
        return;
    }
    stage_update_unrouted(
        driver, signal_id, std::move(staged_value), std::nullopt);
}

void Interpreter::Impl::stage_update(
    const SignalId signal_id,
    PackedLogic4 staged_value)
{
    stage_update(
        std::nullopt, signal_id, std::move(staged_value));
}

void Interpreter::Impl::stage_update(
    const ProcessId process,
    const SignalId signal_id,
    PackedLogic4 staged_value,
    const SignalUpdateDomain domain)
{
    stage_update(
        std::optional<ProcessId> { process },
        signal_id,
        std::move(staged_value),
        domain);
}

void Interpreter::Impl::stage_update_slice(
    const std::optional<ProcessId> driver,
    const SignalId signal_id,
    PackedLogic4 value,
    const std::size_t offset)
{
    (void)get_signal(signal_id);
    const auto target_width = driven_values[signal_id].width();
    if (get_signal(signal_id).systemverilog_scalar
        != SystemVerilogScalarKind::None) {
        throw std::invalid_argument {
            "SimIR scalar signals do not support partial update"
        };
    }
    if (value.width() == 0 || offset > target_width
        || value.width() > target_width - offset) {
        throw std::invalid_argument(
            "partial update range is outside its target signal");
    }
    value = coerce_value_kind(
        std::move(value),
        get_signal(signal_id).value_kind);
    if (driver
        && route_module_path_update(
            *driver, signal_id, value, offset)) {
        return;
    }
    stage_update_unrouted(
        driver, signal_id, std::move(value), offset);
}

void Interpreter::Impl::stage_update_slice(
    const SignalId signal_id,
    PackedLogic4 value,
    const std::size_t offset)
{
    stage_update_slice(
        std::nullopt,
        signal_id,
        std::move(value),
        offset);
}

void Interpreter::Impl::stage_update_words(
    const ProcessId process,
    const std::span<const ProcessUpdateWord> updates)
{
    if (process_profile_enabled || update_profile_enabled) {
        processes.add_profile_updates(process, updates.size());
    }
    pending_updates.reserve(pending_updates.size() + updates.size());
    bool staged { };
    for (const auto& update : updates) {
        const auto& signal = get_signal(update.signal);
        std::optional<std::size_t> offset;
        if (update.slice) {
            const auto target_width = driven_values[update.signal].width();
            if (signal.systemverilog_scalar
                != SystemVerilogScalarKind::None) {
                throw std::invalid_argument {
                    "SimIR scalar signals do not support partial update"
                };
            }
            if (update.value.width == 0U || update.offset > target_width
                || update.value.width > target_width - update.offset) {
                throw std::invalid_argument(
                    "partial update range is outside its target signal");
            }
            offset = update.offset;
        } else {
            if (driven_values[update.signal].width()
                != update.value.width) {
                throw std::invalid_argument(
                    "SimIR signal assignment width mismatch");
            }
        }
        if (!module_paths.empty()
            || signal.systemverilog_scalar
                != SystemVerilogScalarKind::None
            || signal.value_kind == ValueKind::logic9) {
            auto value = PackedLogic4::from_aval_bval(
                update.value.width,
                update.value.aval,
                update.value.bval);
            value = update.slice
                ? coerce_value_kind(std::move(value), signal.value_kind)
                : normalize_signal_value(update.signal, std::move(value));
            if (route_module_path_update(
                    process, update.signal, value, offset)) {
                continue;
            }
            const auto value_index = pending_update_values.size();
            pending_update_values.push_back(std::move(value));
            block_native_logic9_update_before_generic(update.signal);
            pending_updates.push_back(PendingUpdate {
                update.signal,
                process,
                offset,
                { },
                value_index });
        } else {
            block_native_logic9_update_before_generic(update.signal);
            pending_updates.push_back(PendingUpdate {
                update.signal, process, offset, update.value, std::nullopt });
        }
        staged = true;
    }
    if (staged) {
        schedule_update_commit();
    }
}

void Interpreter::Impl::stage_validated_update_words(
    const ProcessId process,
    const std::span<const ProcessUpdateWord> updates)
{
    if (native_update_profile_enabled) {
        ++native_update_profile_word_calls;
        native_update_profile_words += updates.size();
    }
    const bool container_alias_update = std::ranges::any_of(
        updates,
        [&](const ProcessUpdateWord& update) {
            return has_container_signal_alias(update.signal);
        });
    bool module_path_requires_checked_staging {
        !module_paths.empty()
        && (native_signal_dependencies_unknown
            || module_path_destination_mask.size() != signals.size())
    };
    if (!module_path_requires_checked_staging && !module_paths.empty()) {
        module_path_requires_checked_staging = std::ranges::any_of(
            updates,
            [&](const ProcessUpdateWord& update) {
                return update.signal >= module_path_destination_mask.size()
                    || module_path_destination_mask[update.signal] != 0U;
            });
    }
    const bool switch_connected_update = has_bidirectional_switches
        && std::ranges::any_of(updates, [&](const auto& update) {
            return update.signal >= switch_endpoint_adjacency.size()
                || update.signal >= switch_control_adjacency.size()
                || !switch_endpoint_adjacency[update.signal].empty()
                || !switch_control_adjacency[update.signal].empty();
        });
    // A bidirectional switch process represents network topology, not a
    // process-owned driver. The checked queue handles it before driver slots
    // are staged.
    if (container_alias_update || module_path_requires_checked_staging
        || switch_connected_update
        || (has_bidirectional_switches && switch_process(process))) {
        if (native_update_profile_enabled) {
            ++native_update_profile_word_fallbacks;
        }
        stage_update_words(process, updates);
        return;
    }
    const auto initial_exceptions = std::uncaught_exceptions();
    const auto clear_provenance_on_exception = [this, initial_exceptions](
                                                   void*) noexcept {
        if (std::uncaught_exceptions() > initial_exceptions) {
            clear_unresolved_update_owner_provenance();
        }
    };
    const std::unique_ptr<void, decltype(clear_provenance_on_exception)>
        provenance_exception_guard(this, clear_provenance_on_exception);
    if (process_profile_enabled || update_profile_enabled) {
        processes.add_profile_updates(process, updates.size());
    }
    if (unresolved_update_scratch.size() < signals.size()) {
        unresolved_update_scratch.resize(signals.size());
    }
    if (unresolved_update_owner_provenance.size() < signals.size()) {
        unresolved_update_owner_provenance.resize(signals.size());
    }
    unresolved_update_owner_provenance_signals.reserve(signals.size());
    if (driver_update_scratch.size() < signals.size()) {
        driver_update_scratch.resize(signals.size());
    }
    if (resolved_update_marked.size() < signals.size()) {
        resolved_update_marked.resize(signals.size());
    }
    pending_updates.reserve(pending_updates.size() + updates.size());
    bool staged_any { };
    for (const auto& update : updates) {
        const auto& signal = signals[update.signal];
        const auto* const direct_record
            = direct_single_driver_record(update.signal);
        const bool direct_single_driver
            = direct_record != nullptr
            && direct_record->process == process
            && !external_driver_values[update.signal]
            && !forced_driver_values[update.signal];
        const bool defer_authoritative_demotion
            = !direct_word_commit_disabled && direct_single_driver
            && signal.systemverilog_scalar
                == SystemVerilogScalarKind::None
            && signal.value_kind != ValueKind::logic9
            && direct_record->value.width() != 0U
            && direct_record->value.width() <= 64U
            && !owned_driver_active(update.signal);
        // Keep an authoritative A4 owner bound while a complete direct word
        // is only staged. Its publication either commits a prepared A4
        // mutation or performs the ordinary prewrite demotion itself. Active
        // owned-driver composites still need their established demotion
        // before the per-driver scratch is inspected.
        if (!defer_authoritative_demotion) {
            demote_owned_driver(update.signal);
        }
        const auto offset = update.slice
            ? std::optional<std::size_t> { update.offset }
            : std::nullopt;
        if (update_profile_enabled) {
            ++update_profile_updates;
            update_profile_bits += update.value.width;
            if (offset) {
                ++update_profile_slices;
            } else {
                ++update_profile_whole;
            }
            if (signal.resolution == ResolutionKind::none) {
                ++update_profile_unresolved;
            } else {
                ++update_profile_resolved;
                const auto& values = driver_values[update.signal];
                if (signal.resolution == ResolutionKind::sv_wire
                    && values.size() == 1U
                    && !external_driver_values[update.signal]
                    && !signal.has_implicit_driver
                    && !signal.has_charge_strength) {
                    ++update_profile_resolved_single_driver;
                }
            }
        }
        if (signal.systemverilog_scalar != SystemVerilogScalarKind::None
            || signal.value_kind == ValueKind::logic9) {
            auto value = PackedLogic4::from_aval_bval(
                update.value.width,
                update.value.aval,
                update.value.bval);
            value = update.slice
                ? coerce_value_kind(std::move(value), signal.value_kind)
                : normalize_signal_value(update.signal, std::move(value));
            const auto value_index = pending_update_values.size();
            pending_update_values.push_back(std::move(value));
            block_native_logic9_update_before_generic(update.signal);
            pending_updates.push_back(PendingUpdate {
                update.signal,
                process,
                offset,
                { },
                value_index });
            if (native_update_profile_enabled) {
                ++native_update_profile_word_scalar;
            }
            staged_any = true;
            continue;
        }

        if (!direct_word_commit_disabled && direct_single_driver
            && direct_record->value.width() <= 64U) {
            auto& staged = direct_single_driver_word_scratch[update.signal];
            const auto direct_width = static_cast<std::uint32_t>(
                direct_record->value.width());
            const auto current = staged.active != 0U
                ? Logic4Word { staged.width, staged.aval, staged.bval }
                : Logic4Word {
                      direct_width,
                      direct_signal_aval[update.signal],
                      direct_signal_bval[update.signal]
                  };
            const auto shift = static_cast<std::uint32_t>(
                offset.value_or(0U));
            const auto source_mask = update.value.width == 64U
                ? std::numeric_limits<std::uint64_t>::max()
                : (UINT64_C(1) << update.value.width) - UINT64_C(1);
            const auto mask = source_mask << shift;
            const auto aval = (update.value.aval & source_mask) << shift;
            const auto bval = (update.value.bval & source_mask) << shift;
            if (!signal_transaction_observed[update.signal]
                && (((current.aval ^ aval) | (current.bval ^ bval)) & mask)
                    == 0U) {
                if (native_update_profile_enabled) {
                    ++native_update_profile_word_unchanged;
                }
                continue;
            }
            if (staged.active == 0U) {
                if (native_word_update_count
                    >= native_word_update_signals.size()) {
                    throw std::logic_error {
                        "native update phase exceeded its signal capacity"
                    };
                }
                native_word_update_signals[native_word_update_count++]
                    = update.signal;
                staged.process = process;
                staged.width = direct_width;
                staged.aval = current.aval;
                staged.bval = current.bval;
                staged.active = 1U;
            }
            staged.aval = (staged.aval & ~mask) | (aval & mask);
            staged.bval = (staged.bval & ~mask) | (bval & mask);
            if (native_update_profile_enabled) {
                ++native_update_profile_word_direct;
            }
            staged_any = true;
            continue;
        }
        if (direct_single_driver) {
            // The native word path may have left the packed driver and
            // published value behind their direct planes. The fallback below
            // merges slices into those packed values.
            materialize_direct_signal(update.signal);
            auto& staged = unresolved_update_scratch[update.signal];
            const auto& current = staged
                ? *staged
                : direct_record->value;
            if (!signal_transaction_observed[update.signal]
                && current.matches_word(
                    update.value, offset.value_or(0U))) {
                if (native_update_profile_enabled) {
                    ++native_update_profile_word_unchanged;
                }
                continue;
            }
            if (!staged) {
                direct_single_driver_update_signals.push_back(update.signal);
                if (!offset) {
                    staged.emplace(PackedLogic4::from_aval_bval(
                        update.value.width,
                        update.value.aval,
                        update.value.bval));
                    if (native_update_profile_enabled) {
                        ++native_update_profile_word_direct;
                    }
                    staged_any = true;
                    continue;
                }
                staged.emplace(direct_record->value);
            }
            if (offset) {
                staged->insert_word(update.value, *offset);
            } else {
                *staged = PackedLogic4::from_aval_bval(
                    update.value.width,
                    update.value.aval,
                    update.value.bval);
            }
            if (native_update_profile_enabled) {
                ++native_update_profile_word_direct;
            }
            staged_any = true;
            continue;
        }

        // Native executors have already validated widths and signal IDs. Apply
        // their ordinary logic4 words directly to the per-update-phase scratch
        // instead of materializing millions of PendingUpdate records and then
        // traversing those records a second time in the update callback.
        PackedLogic4* destination { };
        if (signal.resolution == ResolutionKind::none) {
            auto& staged = unresolved_update_scratch[update.signal];
            if (!staged
                && direct_signal_materialization_pending[update.signal]
                    != 0U) {
                const auto shift = static_cast<std::uint32_t>(
                    offset.value_or(0U));
                const auto source_mask = update.value.width == 64U
                    ? std::numeric_limits<std::uint64_t>::max()
                    : (UINT64_C(1) << update.value.width) - UINT64_C(1);
                const auto mask = source_mask << shift;
                const auto aval
                    = (update.value.aval & source_mask) << shift;
                const auto bval
                    = (update.value.bval & source_mask) << shift;
                if (!signal_transaction_observed[update.signal]
                    && (((direct_signal_aval[update.signal] ^ aval)
                          | (direct_signal_bval[update.signal] ^ bval))
                        & mask)
                    == 0U) {
                    if (native_update_profile_enabled) {
                        ++native_update_profile_word_unchanged;
                    }
                    continue;
                }
            }
            if (!staged) {
                materialize_direct_signal(update.signal);
            }
            const auto& current = staged
                ? *staged
                : driven_values[update.signal];
            if (!signal_transaction_observed[update.signal]
                && current.matches_word(
                    update.value, offset.value_or(0U))) {
                if (native_update_profile_enabled) {
                    ++native_update_profile_word_unchanged;
                }
                continue;
            }
            note_unresolved_update_owner(
                update.signal, process, !offset.has_value());
            if (!staged) {
                unresolved_update_signals.push_back(update.signal);
                if (!offset) {
                    staged.emplace(PackedLogic4::from_aval_bval(
                        update.value.width,
                        update.value.aval,
                        update.value.bval));
                    if (native_update_profile_enabled) {
                        ++native_update_profile_word_unresolved;
                    }
                    staged_any = true;
                    continue;
                }
                staged.emplace(driven_values[update.signal]);
            }
            destination = &*staged;
        } else {
            auto& staged = driver_update_scratch[update.signal];
            auto found = std::ranges::find(
                staged,
                std::optional<ProcessId> { process },
                &PendingDriverCommit::driver);
            if (found == staged.end()) {
                const auto& current = driver_slot(process, update.signal);
                if (!signal_transaction_observed[update.signal]
                    && current.matches_word(
                        update.value, offset.value_or(0U))) {
                    if (native_update_profile_enabled) {
                        ++native_update_profile_word_unchanged;
                    }
                    continue;
                }
                if (staged.empty()) {
                    driver_update_signals.push_back(update.signal);
                }
                if (!offset) {
                    staged.push_back(PendingDriverCommit {
                        process,
                        PackedLogic4::from_aval_bval(
                            update.value.width,
                            update.value.aval,
                            update.value.bval) });
                    if (!resolved_update_marked[update.signal]) {
                        resolved_update_marked[update.signal] = true;
                        resolved_update_signals.push_back(update.signal);
                    }
                    if (native_update_profile_enabled) {
                        ++native_update_profile_word_resolved;
                    }
                    staged_any = true;
                    continue;
                }
                staged.push_back(PendingDriverCommit {
                    process, current });
                found = std::prev(staged.end());
            } else if (!signal_transaction_observed[update.signal]
                && found->value.matches_word(
                    update.value, offset.value_or(0U))) {
                if (native_update_profile_enabled) {
                    ++native_update_profile_word_unchanged;
                }
                continue;
            }
            destination = &found->value;
            if (!resolved_update_marked[update.signal]) {
                resolved_update_marked[update.signal] = true;
                resolved_update_signals.push_back(update.signal);
            }
        }
        if (offset) {
            destination->insert_word(update.value, *offset);
        } else {
            *destination = PackedLogic4::from_aval_bval(
                update.value.width,
                update.value.aval,
                update.value.bval);
        }
        if (native_update_profile_enabled) {
            if (signal.resolution == ResolutionKind::none) {
                ++native_update_profile_word_unresolved;
            } else {
                ++native_update_profile_word_resolved;
            }
        }
        staged_any = true;
    }
    if (staged_any) {
        schedule_update_commit();
    }
}

template <typename Batches>
bool Interpreter::Impl::stage_validated_update_slot_batches_impl(
    Batches& batches,
    const bool schedule_commit,
    bool* staged_any_out,
    const bool count_profile_call)
{
    bool has_container_alias_update { };
    for (const auto& batch : batches) {
        for_each_active_update_slot(batch, [&](const auto& slot) {
            if (slot.active != nullptr && *slot.active != 0U
                && has_container_signal_alias(slot.signal)) {
                has_container_alias_update = true;
            }
        });
        if (has_container_alias_update) {
            break;
        }
    }
    if (has_container_alias_update) {
        return false;
    }

    if (native_update_profile_enabled && count_profile_call) {
        ++native_update_profile_calls;
    }
    // Module-path destinations may redirect or synthesize writes, and
    // profiling owns per-update counts. Leave those slots on the ordinary
    // checked path without consuming any slot state.
    bool module_path_requires_checked_staging {
        !module_paths.empty()
        && (native_signal_dependencies_unknown
            || module_path_destination_mask.size() != signals.size())
    };
    if (!module_path_requires_checked_staging && !module_paths.empty()) {
        for (const auto& batch : batches) {
            for_each_active_update_slot(batch, [&](const auto& slot) {
                if (*slot.active != 0U
                    && (slot.signal >= module_path_destination_mask.size()
                        || module_path_destination_mask[slot.signal] != 0U)) {
                    module_path_requires_checked_staging = true;
                }
            });
            if (module_path_requires_checked_staging) {
                break;
            }
        }
    }
    bool switch_connected_batch { };
    if (has_bidirectional_switches) {
        for (const auto& batch : batches) {
            for_each_active_update_slot(batch, [&](const auto& slot) {
                if (*slot.active != 0U
                    && (slot.signal >= switch_endpoint_adjacency.size()
                        || slot.signal >= switch_control_adjacency.size()
                        || !switch_endpoint_adjacency[slot.signal].empty()
                        || !switch_control_adjacency[slot.signal].empty())) {
                    switch_connected_batch = true;
                }
            });
            if (switch_connected_batch) {
                break;
            }
        }
    }
    const bool switch_process_batch = has_bidirectional_switches
        && std::ranges::any_of(batches, [&](const auto& batch) {
            return switch_process(batch.process);
        });
    if (module_path_requires_checked_staging || switch_connected_batch
        || switch_process_batch
        || process_profile_enabled
        || update_profile_enabled) {
        if (native_update_profile_enabled) {
            ++native_update_profile_fallbacks;
        }
        return false;
    }

    const auto initial_exceptions = std::uncaught_exceptions();
    const auto clear_provenance_on_exception = [this, initial_exceptions](
                                                   void*) noexcept {
        if (std::uncaught_exceptions() > initial_exceptions) {
            clear_unresolved_update_owner_provenance();
        }
    };
    const std::unique_ptr<void, decltype(clear_provenance_on_exception)>
        provenance_exception_guard(this, clear_provenance_on_exception);

    for (const auto& batch : batches) {
        bool compatible = true;
        for_each_active_update_slot(batch, [&](const auto& slot) {
            if (*slot.active != 0U
                && (signals[slot.signal].systemverilog_scalar
                        != SystemVerilogScalarKind::None
                    || signals[slot.signal].value_kind
                        == ValueKind::logic9)) {
                compatible = false;
            }
        });
        if (!compatible) {
            if (native_update_profile_enabled) {
                ++native_update_profile_fallbacks;
            }
            return false;
        }
    }

    if (unresolved_update_scratch.size() < signals.size()) {
        unresolved_update_scratch.resize(signals.size());
    }
    if (unresolved_update_owner_provenance.size() < signals.size()) {
        unresolved_update_owner_provenance.resize(signals.size());
    }
    unresolved_update_owner_provenance_signals.reserve(signals.size());
    if (driver_update_scratch.size() < signals.size()) {
        driver_update_scratch.resize(signals.size());
    }
    if (resolved_update_marked.size() < signals.size()) {
        resolved_update_marked.resize(signals.size());
    }

    const auto word_width = [](const ProcessUpdateSlotView& slot,
                                const std::uint32_t word) {
        return std::min(64U, slot.width - word * 64U);
    };
    const auto width_mask = [](const std::uint32_t width) {
        return width == 64U
            ? std::numeric_limits<std::uint64_t>::max()
            : (UINT64_C(1) << width) - UINT64_C(1);
    };
    const auto slot_full = [&](const ProcessUpdateSlotView& slot) {
        for (std::uint32_t word = 0; word < slot.word_count; ++word) {
            const auto full = width_mask(word_width(slot, word));
            if ((slot.mask[word] & full) != full) {
                return false;
            }
        }
        return true;
    };
    const auto slot_matches = [&](const PackedLogic4& current,
                                  const ProcessUpdateSlotView& slot) {
        for (std::uint32_t word = 0; word < slot.word_count; ++word) {
            const auto width = word_width(slot, word);
            const auto mask = slot.mask[word] & width_mask(width);
            if (!current.matches_masked_word(
                    Logic4Word {
                        width, slot.aval[word], slot.bval[word]
                    },
                    mask,
                    static_cast<std::size_t>(word) * 64U)) {
                return false;
            }
        }
        return true;
    };
    const auto materialize_slot = [](const ProcessUpdateSlotView& slot) {
        if (slot.width <= 64U) {
            return PackedLogic4::from_aval_bval(
                slot.width, slot.aval[0], slot.bval[0]);
        }
        return PackedLogic4::from_word_planes(
            slot.width,
            std::span<const std::uint64_t> {
                slot.aval, slot.word_count
            },
            std::span<const std::uint64_t> {
                slot.bval, slot.word_count
            });
    };
    const auto merge_slot = [&](PackedLogic4& destination,
                                const ProcessUpdateSlotView& slot) {
        for (std::uint32_t word = 0; word < slot.word_count; ++word) {
            const auto width = word_width(slot, word);
            destination.insert_masked_word(
                Logic4Word { width, slot.aval[word], slot.bval[word] },
                slot.mask[word] & width_mask(width),
                static_cast<std::size_t>(word) * 64U);
        }
    };
    const auto consume_slot = [](const ProcessUpdateSlotView& slot) {
        *slot.active = 0U;
        std::fill_n(slot.mask, slot.word_count, UINT64_C(0));
    };

    bool staged_any { };
    if (native_update_profile_enabled && count_profile_call) {
        native_update_profile_batches += static_cast<std::uint64_t>(
            std::ranges::distance(batches));
    }
    for (const auto& batch : batches) {
        for_each_active_update_slot(batch, [&](const auto& slot) {
            if (native_update_profile_enabled) {
                ++native_update_profile_slots;
            }
            if (*slot.active == 0U) {
                if (native_update_profile_enabled) {
                    ++native_update_profile_inactive;
                }
                return;
            }
            bool touched { };
            for (std::uint32_t word = 0; word < slot.word_count; ++word) {
                touched = touched
                    || (slot.mask[word] & width_mask(word_width(slot, word)))
                        != 0U;
            }
            if (!touched) {
                if (native_update_profile_enabled) {
                    ++native_update_profile_untouched;
                }
                consume_slot(slot);
                return;
            }

            const auto signal = slot.signal;
            const bool complete_slot = slot_full(slot);
            const auto* direct_record
                = direct_single_driver_record(signal);
            const bool direct_single_driver
                = direct_record != nullptr
                && direct_record->process == batch.process
                && !external_driver_values[signal]
                && !forced_driver_values[signal];
            if (!direct_word_commit_disabled && direct_single_driver
                && slot.width <= 64U
                && slot.word_count == 1U) {
                auto& staged = direct_single_driver_word_scratch[signal];
                const auto current = staged.active != 0U
                    ? Logic4Word {
                          staged.width, staged.aval, staged.bval
                      }
                    : Logic4Word {
                          slot.width,
                          direct_signal_aval[signal],
                          direct_signal_bval[signal]
                      };
                const auto mask
                    = slot.mask[0] & width_mask(slot.width);
                if (!signal_transaction_observed[signal]
                    && (((current.aval ^ slot.aval[0])
                          | (current.bval ^ slot.bval[0]))
                        & mask)
                    == 0U) {
                    if (native_update_profile_enabled) {
                        ++native_update_profile_unchanged;
                        ++native_update_profile_unchanged_direct_word;
                    }
                    consume_slot(slot);
                    return;
                }
                if (staged.active == 0U) {
                    if (native_word_update_count
                        >= native_word_update_signals.size()) {
                        throw std::logic_error {
                            "native update phase exceeded its signal capacity"
                        };
                    }
                    native_word_update_signals[
                        native_word_update_count++] = signal;
                    staged.process = batch.process;
                    staged.width = static_cast<std::uint32_t>(current.width);
                    staged.aval = current.aval;
                    staged.bval = current.bval;
                    staged.active = 1U;
                }
                staged.aval = (staged.aval & ~mask)
                    | (slot.aval[0] & mask);
                staged.bval = (staged.bval & ~mask)
                    | (slot.bval[0] & mask);
                if (native_update_profile_enabled) {
                    ++native_update_profile_direct_word;
                }
                staged_any = true;
                consume_slot(slot);
                return;
            }
            if (direct_single_driver) {
                materialize_direct_signal(signal);
                auto* record = direct_single_driver_record(signal);
                if (record == nullptr
                    || record->process != batch.process) {
                    throw std::logic_error {
                        "direct driver route changed during materialization"
                    };
                }
                auto& staged = unresolved_update_scratch[signal];
                const auto& current
                    = staged ? *staged : record->value;
                if (!signal_transaction_observed[signal]
                    && slot_matches(current, slot)) {
                    if (native_update_profile_enabled) {
                        ++native_update_profile_unchanged;
                        ++native_update_profile_unchanged_direct_packed;
                    }
                    consume_slot(slot);
                    return;
                }
                if (!staged) {
                    direct_single_driver_update_signals.push_back(signal);
                    if (complete_slot) {
                        staged.emplace(materialize_slot(slot));
                    } else {
                        staged.emplace(record->value);
                        merge_slot(*staged, slot);
                    }
                } else {
                    merge_slot(*staged, slot);
                }
                if (native_update_profile_enabled) {
                    ++native_update_profile_direct_packed;
                }
                staged_any = true;
                consume_slot(slot);
                return;
            }

            if (signals[signal].resolution == ResolutionKind::none) {
                auto& staged = unresolved_update_scratch[signal];
                if (!staged
                    && direct_signal_materialization_pending[signal] != 0U
                    && slot.width <= 64U
                    && slot.word_count == 1U) {
                    const auto mask
                        = slot.mask[0] & width_mask(slot.width);
                    if (!signal_transaction_observed[signal]
                        && (((direct_signal_aval[signal] ^ slot.aval[0])
                              | (direct_signal_bval[signal] ^ slot.bval[0]))
                            & mask)
                        == 0U) {
                        if (native_update_profile_enabled) {
                            ++native_update_profile_unchanged;
                            ++native_update_profile_unchanged_unresolved;
                        }
                        consume_slot(slot);
                        return;
                    }
                }
                if (!staged) {
                    materialize_direct_signal(signal);
                }
                const auto& current = staged ? *staged : driven_values[signal];
                if (!signal_transaction_observed[signal]
                    && slot_matches(current, slot)) {
                    if (native_update_profile_enabled) {
                        ++native_update_profile_unchanged;
                        ++native_update_profile_unchanged_unresolved;
                    }
                    consume_slot(slot);
                    return;
                }
                note_unresolved_update_owner(
                    signal, batch.process, complete_slot);
                if (!staged) {
                    unresolved_update_signals.push_back(signal);
                    if (complete_slot) {
                        staged.emplace(materialize_slot(slot));
                    } else {
                        staged.emplace(driven_values[signal]);
                        merge_slot(*staged, slot);
                    }
                } else {
                    merge_slot(*staged, slot);
                }
                if (native_update_profile_enabled) {
                    ++native_update_profile_unresolved;
                }
                staged_any = true;
                consume_slot(slot);
                return;
            }

            const auto* const prepared = batch.owned_slot_certificate;
            const bool prepared_matches = prepared != nullptr
                && batch.slots.size() == 1U
                && batch.active_words.size() == 1U
                && prepared->owner == this
                && prepared->process == batch.process
                && prepared->signal == signal
                && prepared->width == slot.width
                && prepared->word_count == slot.word_count
                && slot.word_count <= prepared->own_masks.size()
                && prepared->process < owned_driver_spans.size()
                && owned_driver_spans[prepared->process].signal == signal;
            const auto owned_result = prepared_matches
                ? stage_prepared_owned_update_slot(*prepared, slot)
                : stage_owned_driver_slot(batch.process, slot);
            if (owned_result != OwnedDriverStage::unsupported) {
                if (owned_result == OwnedDriverStage::unchanged) {
                    if (native_update_profile_enabled) {
                        ++native_update_profile_unchanged;
                        ++native_update_profile_unchanged_resolved;
                        ++native_update_profile_unchanged_owned;
                    }
                } else {
                    if (!resolved_update_marked[signal]) {
                        resolved_update_marked[signal] = true;
                        resolved_update_signals.push_back(signal);
                    }
                    if (native_update_profile_enabled) {
                        ++native_update_profile_resolved;
                        ++native_update_profile_changed_owned;
                    }
                    staged_any = true;
                }
                consume_slot(slot);
                return;
            }

            auto& staged = driver_update_scratch[signal];
            auto found = std::ranges::find(
                staged,
                std::optional<ProcessId> { batch.process },
                &PendingDriverCommit::driver);
            if (found == staged.end()) {
                const auto& current = driver_slot(batch.process, signal);
                if (!signal_transaction_observed[signal]
                    && slot_matches(current, slot)) {
                    if (native_update_profile_enabled) {
                        ++native_update_profile_unchanged;
                        ++native_update_profile_unchanged_resolved;
                    }
                    consume_slot(slot);
                    return;
                }
                if (staged.empty()) {
                    driver_update_signals.push_back(signal);
                }
                staged.push_back(PendingDriverCommit {
                    batch.process,
                    complete_slot ? materialize_slot(slot) : current
                });
                found = std::prev(staged.end());
                if (!complete_slot) {
                    merge_slot(found->value, slot);
                }
            } else {
                if (!signal_transaction_observed[signal]
                    && slot_matches(found->value, slot)) {
                    if (native_update_profile_enabled) {
                        ++native_update_profile_unchanged;
                        ++native_update_profile_unchanged_resolved;
                    }
                    consume_slot(slot);
                    return;
                }
                merge_slot(found->value, slot);
            }
            if (!resolved_update_marked[signal]) {
                resolved_update_marked[signal] = true;
                resolved_update_signals.push_back(signal);
            }
            if (native_update_profile_enabled) {
                ++native_update_profile_resolved;
            }
            staged_any = true;
            consume_slot(slot);
        });
        std::ranges::fill(batch.active_words, UINT64_C(0));
    }
    if (staged_any_out != nullptr) {
        *staged_any_out |= staged_any;
    }
    if (staged_any && schedule_commit) {
        schedule_update_commit();
    }
    return true;
}

bool Interpreter::Impl::stage_validated_update_slot_batches(
    const std::span<const ProcessUpdateSlotBatch> batches)
{
    auto source = batches;
    return stage_validated_update_slot_batches_impl(source);
}

bool Interpreter::Impl::stage_validated_logic9_update_batch(
    const ProcessLogic9UpdateBatch& batch)
{
    return stage_validated_logic9_update_batches(
        std::span { &batch, 1U });
}

bool Interpreter::Impl::stage_validated_logic9_update_batches(
    const std::span<const ProcessLogic9UpdateBatch> batches)
{
    struct Logic9BatchProfile {
        bool enabled { };
        std::uint64_t calls { };
        std::uint64_t active_slots { };
        std::uint64_t rejected_runtime { };
        std::uint64_t rejected_slot { };
        std::uint64_t rejected_layout { };
        std::uint64_t rejected_resolution { };
        std::uint64_t rejected_std_logic_direct_owner { };
        std::uint64_t rejected_record { };
        std::uint64_t rejected_owner { };
        std::uint64_t rejected_ordering { };
        std::uint64_t accepted { };
        std::uint64_t consumed_slots { };
        std::uint64_t changed_slots { };

        ~Logic9BatchProfile()
        {
            if (enabled) {
                std::fprintf(
                    stderr,
                    "fsim-profile: logic9-batch calls=%llu active_slots=%llu "
                    "rejected_runtime=%llu rejected_slot=%llu "
                    "rejected_layout=%llu rejected_resolution=%llu "
                    "rejected_std_logic_direct_owner=%llu "
                    "rejected_record=%llu rejected_owner=%llu "
                    "rejected_ordering=%llu accepted=%llu "
                    "consumed_slots=%llu "
                    "changed_slots=%llu\n",
                    static_cast<unsigned long long>(calls),
                    static_cast<unsigned long long>(active_slots),
                    static_cast<unsigned long long>(rejected_runtime),
                    static_cast<unsigned long long>(rejected_slot),
                    static_cast<unsigned long long>(rejected_layout),
                    static_cast<unsigned long long>(rejected_resolution),
                    static_cast<unsigned long long>(
                        rejected_std_logic_direct_owner),
                    static_cast<unsigned long long>(rejected_record),
                    static_cast<unsigned long long>(rejected_owner),
                    static_cast<unsigned long long>(rejected_ordering),
                    static_cast<unsigned long long>(accepted),
                    static_cast<unsigned long long>(consumed_slots),
                    static_cast<unsigned long long>(changed_slots));
            }
        }
    };
    static Logic9BatchProfile profile;
    profile.enabled = profile.enabled || logic9_batch_profile_enabled;
    if (logic9_batch_profile_enabled) {
        profile.calls += batches.size();
    }
    const bool container_alias_update = std::ranges::any_of(
        batches,
        [&](const ProcessLogic9UpdateBatch& batch) {
            return std::ranges::any_of(
                batch.slots,
                [&](const ProcessLogic9UpdateSlotView& slot) {
                    return slot.mask != nullptr && *slot.mask != 0U
                        && has_container_signal_alias(slot.signal);
                });
        });
    if (container_alias_update) {
        if (logic9_batch_profile_enabled) {
            profile.rejected_runtime += batches.size();
        }
        return false;
    }
    bool module_path_requires_checked_staging {
        !module_paths.empty()
        && (native_signal_dependencies_unknown
            || module_path_destination_mask.size() != signals.size())
    };
    if (!module_path_requires_checked_staging && !module_paths.empty()) {
        for (const auto& batch : batches) {
            for (const auto& slot : batch.slots) {
                if (slot.mask != nullptr && *slot.mask != 0U
                    && (slot.signal >= module_path_destination_mask.size()
                        || module_path_destination_mask[slot.signal] != 0U)) {
                    module_path_requires_checked_staging = true;
                    break;
                }
            }
            if (module_path_requires_checked_staging) {
                break;
            }
        }
    }
    bool switch_connected_batch { };
    if (has_bidirectional_switches) {
        for (const auto& batch : batches) {
            for (const auto& slot : batch.slots) {
                if (slot.mask != nullptr && *slot.mask != 0U
                    && (slot.signal >= switch_endpoint_adjacency.size()
                        || slot.signal >= switch_control_adjacency.size()
                        || !switch_endpoint_adjacency[slot.signal].empty()
                        || !switch_control_adjacency[slot.signal].empty())) {
                    switch_connected_batch = true;
                    break;
                }
            }
            if (switch_connected_batch) {
                break;
            }
        }
    }
    const bool switch_process_batch = has_bidirectional_switches
        && std::ranges::any_of(batches, [&](const auto& batch) {
            return switch_process(batch.process);
        });
    if (module_path_requires_checked_staging || switch_connected_batch
        || switch_process_batch
        || process_profile_enabled
        || update_profile_enabled) {
        if (logic9_batch_profile_enabled) {
            profile.rejected_runtime += batches.size();
        }
        return false;
    }

    bool staged_any { };
    bool consumed_all { true };
    for (const auto& batch : batches) {
      for (const auto& slot : batch.slots) {
        if (slot.mask == nullptr || *slot.mask == 0U) {
            continue;
        }
        if (logic9_batch_profile_enabled) {
            ++profile.active_slots;
        }
        const auto* direct_record
            = direct_single_driver_record(slot.signal);
        const bool ordering_blocked
            = slot.signal < direct_single_driver_logic9_word_scratch.size()
            && direct_single_driver_logic9_word_scratch[slot.signal].active
                == DirectSingleDriverLogic9WordUpdate::generic_blocked;
        const bool wrong_staged_owner
            = slot.signal < direct_single_driver_logic9_word_scratch.size()
            && direct_single_driver_logic9_word_scratch[slot.signal].active
                == DirectSingleDriverLogic9WordUpdate::native
            && direct_single_driver_logic9_word_scratch[slot.signal].process
                != batch.process;
        if (slot.planes == nullptr || slot.signal >= signals.size()
            || slot.signal >= direct_single_driver_routes.size()
            || slot.width == 0U || slot.width > 64U
            || signals[slot.signal].value_kind != ValueKind::logic9
            || signals[slot.signal].resolution != ResolutionKind::std_logic
            || signals[slot.signal].initial_value.width() != slot.width
            || direct_record == nullptr
            || direct_record->process != batch.process
            || ordering_blocked || wrong_staged_owner) {
            consumed_all = false;
            if (logic9_batch_profile_enabled) {
                ++profile.rejected_slot;
                if (slot.planes == nullptr || slot.signal >= signals.size()
                    || slot.signal >= direct_single_driver_routes.size()
                    || slot.width == 0U || slot.width > 64U
                    || signals[slot.signal].value_kind != ValueKind::logic9) {
                    ++profile.rejected_layout;
                } else if (signals[slot.signal].resolution
                           != ResolutionKind::std_logic) {
                    ++profile.rejected_resolution;
                } else if (signals[slot.signal].initial_value.width()
                           != slot.width) {
                    ++profile.rejected_layout;
                } else if (direct_record == nullptr) {
                    ++profile.rejected_record;
                } else if (ordering_blocked) {
                    ++profile.rejected_ordering;
                } else {
                    ++profile.rejected_owner;
                }
            }
            continue;
        }
        const auto full_mask = slot.width == 64U
            ? std::numeric_limits<std::uint64_t>::max()
            : (UINT64_C(1) << slot.width) - UINT64_C(1);
        const auto mask = *slot.mask & full_mask;
        if (mask == 0U) {
            *slot.mask = 0U;
            if (logic9_batch_profile_enabled) {
                ++profile.consumed_slots;
            }
            continue;
        }
        auto& staged = direct_single_driver_logic9_word_scratch[slot.signal];
        const auto value = Logic9Word {
            slot.width,
            { slot.planes[0], slot.planes[1],
                slot.planes[2], slot.planes[3] }
        };
        auto canonical_value = value;
        canonical_value.normalize_invalid_codes_to_x();
        if (staged.active
            == DirectSingleDriverLogic9WordUpdate::unlisted) {
            if (native_logic9_word_update_count
                >= native_logic9_word_update_signals.size()) {
                throw std::logic_error {
                    "native Logic9 update phase exceeded its signal capacity"
                };
            }
            native_logic9_word_update_signals[
                native_logic9_word_update_count++] = slot.signal;
            staged.process = batch.process;
            staged.width = slot.width;
            staged.planes = { };
            staged.mask = 0U;
            staged.active = DirectSingleDriverLogic9WordUpdate::native;
        }
        for (std::size_t plane = 0; plane < staged.planes.size(); ++plane) {
            staged.planes[plane] = (staged.planes[plane] & ~mask)
                | (canonical_value.planes[plane] & mask);
        }
        staged.mask |= mask;
        staged_any = true;
        if (logic9_batch_profile_enabled) {
            ++profile.changed_slots;
            ++profile.consumed_slots;
        }
        *slot.mask = 0U;
      }
      if (logic9_batch_profile_enabled) {
          ++profile.accepted;
      }
    }
    if (staged_any) {
        schedule_update_commit();
    }
    return consumed_all;
}

} // namespace fsim::runtime::simir
