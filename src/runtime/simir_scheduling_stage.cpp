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
#include <unordered_map>

namespace fsim::runtime::simir {

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
            update_commit_running = true;
            const auto clear_running = [this](void*) noexcept {
                update_commit_running = false;
            };
            const std::unique_ptr<void, decltype(clear_running)>
                clear_running_on_exit(this, clear_running);
            const auto clear_provenance = [this](void*) noexcept {
                clear_unresolved_update_owner_provenance();
            };
            const std::unique_ptr<void, decltype(clear_provenance)>
                clear_provenance_on_exit(this, clear_provenance);
            const auto clear_selected_transactions = [this](void*) noexcept {
                clear_pending_selected_signal_transactions();
            };
            const std::unique_ptr<void, decltype(clear_selected_transactions)>
                clear_selected_transactions_on_exit(
                    this, clear_selected_transactions);
            if (native_update_profile_enabled) {
                ++native_update_profile_commits;
            }
            if (update_profile_enabled) {
                ++update_profile_commits;
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
                            return !signals.at(leaf).has_implicit_driver
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
                    record->value.assign_word(value);
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
                    driven_values[signal].assign_word(value);
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
                    record->value = *staged;
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
                for (auto& update : staged) {
                    if (update.driver) {
                        set_driver(
                            *update.driver,
                            signal,
                            std::move(update.value));
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
                                               PackedLogic4 value) {
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
                    std::move(value), word.head });
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
                    append_commit_row(
                        signal, resolved_driver_value(signal));
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
                if (direct_single_driver_commit_marked[signal]) {
                    direct_single_driver_commit_marked[signal] = false;
                    commit_direct_single_driver(signal, std::move(value));
                } else if (get_signal(signal).resolution
                    == ResolutionKind::none) {
                    commit(signal, std::move(value));
                } else {
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
            // VHDL resolution functions run once all of this update's
            // driving values are committed.
            drain_vhdl_resolutions();
        };
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
    auto process_origin = processes.scheduling_origin(process);
    if (static_kernel_region_host == process) {
        process_origin.phase = static_kernel_region_host_phase;
    }
    const auto process_domain = process_origin.process_domain;
    // A process that moved to a later region, such as an assertion action
    // block resumed in the Reactive region, writes from that region.
    const auto phase = process_origin.phase == SchedulerPhase::observed
        ? scheduler.current_phase().value_or(process_origin.phase)
        : process_origin.phase;
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
    const SimulationTick delay,
    const std::optional<StableOrder> order)
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
    try {
        const auto descriptor
            = detail::make_scheduler_task_descriptor<
                SystemVerilogUpdateToken,
                &Interpreter::Impl::dispatch_systemverilog_update>(token);
        scheduler.schedule_internal_systemverilog_at(
            time, origin.phase, order.value_or(process), descriptor);
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
            count_if_tagged(operation_get_if<WriteDelayed>(&operation));
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
    slot.next_free = no_systemverilog_update_slot;
    slot.occupied = true;
    slot.reserved = false;
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
    slot.value = PackedLogic4 { };
    slot.process = ProcessId { };
    slot.signal = SignalId { };
    slot.offset.reset();
    slot.origin = { };
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
        slot.value = PackedLogic4 { };
        slot.process = ProcessId { };
        slot.signal = SignalId { };
        slot.offset.reset();
        slot.origin = { };
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

    const auto process = slot.process;
    const auto signal = slot.signal;
    auto value = std::move(slot.value);
    const auto offset = slot.offset;
    const auto origin = slot.origin;
    owner.release_systemverilog_update(token);
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
}

void Interpreter::Impl::stage_update(
    const std::optional<ProcessId> driver,
    SignalId signal_id,
    PackedLogic4 staged_value,
    const SignalUpdateDomain domain,
    const std::optional<StableOrder> order)
{
    (void)get_signal(signal_id);
    if (driven_values[signal_id].width() != staged_value.width()) {
        throw std::invalid_argument(
            "SimIR signal assignment width mismatch (signal "
            + std::to_string(signal_id) + " has width "
            + std::to_string(driven_values[signal_id].width())
            + ", value has width " + std::to_string(staged_value.width())
            + ")");
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
            domain, 0, order);
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
    const SignalUpdateDomain domain,
    const std::optional<StableOrder> order)
{
    stage_update(
        std::optional<ProcessId> { process },
        signal_id,
        std::move(staged_value),
        domain,
        order);
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
                    "SimIR signal assignment width mismatch (signal "
                    + std::to_string(update.signal) + " has width "
                    + std::to_string(driven_values[update.signal].width())
                    + ", value has width "
                    + std::to_string(update.value.width) + ")");
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
            stage_selected_signal_transaction(
                update.signal, offset.value_or(0U), update.value.width);
        } else {
            block_native_logic9_update_before_generic(update.signal);
            pending_updates.push_back(PendingUpdate {
                update.signal, process, offset, update.value, std::nullopt });
            stage_selected_signal_transaction(
                update.signal, offset.value_or(0U), update.value.width);
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
            stage_selected_signal_transaction(
                update.signal, offset.value_or(0U), update.value.width);
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
            stage_selected_signal_transaction(
                update.signal, offset.value_or(0U), update.value.width);
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
                    stage_selected_signal_transaction(
                        update.signal, offset.value_or(0U), update.value.width);
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
            stage_selected_signal_transaction(
                update.signal, offset.value_or(0U), update.value.width);
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
                    stage_selected_signal_transaction(
                        update.signal, offset.value_or(0U), update.value.width);
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
                    stage_selected_signal_transaction(
                        update.signal, offset.value_or(0U), update.value.width);
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
        stage_selected_signal_transaction(
            update.signal, offset.value_or(0U), update.value.width);
        staged_any = true;
    }
    if (staged_any) {
        schedule_update_commit();
    }
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
        const std::array<std::uint64_t, 1U> mask_words { mask };
        stage_selected_signal_transaction_mask(slot.signal, mask_words);
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
