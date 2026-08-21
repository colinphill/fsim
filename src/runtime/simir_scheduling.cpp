// SPDX-License-Identifier: Apache-2.0
#include "simir_internal.hpp"

#include <bit>
#include <cstdio>
#include <cstdlib>
#include <deque>

namespace fsim::runtime::simir {

namespace {

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

#include "simir_scheduling_commit.tpp"
#include "simir_scheduling_stage.tpp"
void Interpreter::Impl::stage_update_unrouted(
    const std::optional<ProcessId> driver,
    const SignalId signal,
    PackedLogic4 value,
    const std::optional<std::size_t> offset)
{
    if (driver && (process_profile_enabled || update_profile_enabled)) {
        processes[*driver].profile_updates += 1U;
    }
    const auto value_index = pending_update_values.size();
    pending_update_values.push_back(std::move(value));
    pending_updates.push_back(PendingUpdate {
        signal,
        driver,
        offset,
        { },
        value_index });
    schedule_update_commit();
}

bool Interpreter::Impl::route_module_path_update(
    const ProcessId driver,
    const SignalId signal,
    const PackedLogic4& value,
    const std::optional<std::size_t> offset,
    const TransitionDelays* intrinsic_delays,
    const SimulationTick fixed_delay)
{
    const auto write_offset = offset.value_or(0U);
    const auto write_end = write_offset + value.width();
    const bool has_routed_destination = std::ranges::any_of(
        module_paths,
        [&](const ModulePath& path) {
            return std::ranges::binary_search(path.drivers, driver)
                && std::ranges::any_of(
                    path.destinations,
                    [&](const ModulePathTerminal& destination) {
                        const auto destination_end = static_cast<std::size_t>(destination.offset)
                            + destination.width;
                        return destination.signal == signal
                            && write_offset < destination_end
                            && static_cast<std::size_t>(destination.offset)
                            < write_end;
                    });
        });
    if (!has_routed_destination)
        return false;
    const auto driver_current = get_signal(signal).resolution
            == ResolutionKind::none
        ? driven_values.at(signal)
        : driver_slot(driver, signal);
    std::vector<std::optional<SimulationTick>> selected(value.width());
    std::vector<std::uint32_t> orders(value.width());
    std::vector<std::optional<Logic4>> routed_values(value.width());
    std::vector<ModulePathPulseStyle> pulse_styles(
        value.width(), ModulePathPulseStyle::onevent);
    std::vector<bool> show_cancelled(value.width());
    std::vector<std::optional<SimulationTick>> reject_limits(value.width());
    std::vector<std::optional<SimulationTick>> error_limits(value.width());
    std::vector<std::optional<SimulationTick>> retain_delays(value.width());
    bool routed = false;
    const auto accumulated_delay = [&](
                                       const Logic4 before,
                                       const Logic4 after,
                                       const SimulationTick path_delay) {
        const std::array intrinsic {
            intrinsic_delays ? intrinsic_delays->rise : SimulationTick { },
            intrinsic_delays ? intrinsic_delays->fall : SimulationTick { },
            intrinsic_delays ? intrinsic_delays->turnoff : SimulationTick { }
        };
        const auto intrinsic_delay = intrinsic_delays
            ? module_path_transition_delay(before, after, intrinsic).value_or(0)
            : SimulationTick { };
        if (path_delay > std::numeric_limits<SimulationTick>::max() - fixed_delay
            || intrinsic_delay
                > std::numeric_limits<SimulationTick>::max()
                    - fixed_delay - path_delay) {
            throw std::overflow_error {
                "simulation time overflow while accumulating module-path delay"
            };
        }
        return fixed_delay + path_delay + intrinsic_delay;
    };
    std::map<std::uint32_t, std::optional<std::uint32_t>> selected_conditions;
    for (const auto& path : module_paths) {
        if (!path.conditional
            || !std::ranges::binary_search(path.drivers, driver)) {
            continue;
        }
        auto [group, inserted] = selected_conditions.try_emplace(
            path.selection_group, std::nullopt);
        (void)inserted;
        if (!group->second
            && truth_value(evaluate_module_path_expression(path.condition))
                == Logic4::one) {
            group->second = path.id;
        }
    }
    const auto source_event = [&](
                                  const ModulePath& path,
                                  const std::optional<std::size_t> source_terminal,
                                  const std::optional<std::size_t> parallel_bit) {
        for (std::size_t terminal_index = 0;
            terminal_index < path.sources.size(); ++terminal_index) {
            if (source_terminal && terminal_index != *source_terminal)
                continue;
            const auto& terminal = path.sources[terminal_index];
            const auto& stamp = signal_events.at(terminal.signal);
            if (!stamp || stamp->first != scheduler.now()
                || stamp->second != scheduler.delta()) {
                continue;
            }
            const auto begin = parallel_bit.value_or(0U);
            const auto end = parallel_bit
                ? std::min(*parallel_bit + 1U,
                      static_cast<std::size_t>(terminal.width))
                : terminal.width;
            if (begin >= end)
                continue;
            for (auto bit = begin; bit < end; ++bit) {
                const auto lane = terminal.offset + bit;
                const auto before = signal_last_values[terminal.signal].get(lane);
                const auto after = signals[terminal.signal].initial_value.get(lane);
                if (path.source_edge == ModulePathEdge::none
                    || path.source_edge == ModulePathEdge::edge
                    || (path.source_edge == ModulePathEdge::posedge
                        && edge_matches(EdgeKind::posedge, before, after))
                    || (path.source_edge == ModulePathEdge::negedge
                        && edge_matches(EdgeKind::negedge, before, after))) {
                    return before != after;
                }
            }
        }
        return false;
    };
    for (const auto& path : module_paths) {
        if (!std::ranges::binary_search(path.drivers, driver))
            continue;
        const auto selected_condition = selected_conditions.find(
            path.selection_group);
        if ((path.conditional
                && (selected_condition == selected_conditions.end()
                    || selected_condition->second != path.id))
            || (path.ifnone
                && selected_condition != selected_conditions.end()
                && selected_condition->second)) {
            continue;
        }
        const auto data_source = path.data_source.empty()
            ? std::optional<PackedLogic4> { }
            : std::optional<PackedLogic4> {
                  evaluate_module_path_expression(path.data_source)
              };
        for (std::size_t destination_index = 0;
            destination_index < path.destinations.size(); ++destination_index) {
            const auto& destination = path.destinations[destination_index];
            if (destination.signal != signal)
                continue;
            for (std::size_t bit = 0; bit < value.width(); ++bit) {
                const auto target_bit = write_offset + bit;
                if (target_bit < destination.offset
                    || target_bit >= destination.offset + destination.width) {
                    continue;
                }
                const auto path_bit = target_bit - destination.offset;
                if (!source_event(
                        path,
                        path.full
                            ? std::nullopt
                            : std::optional<std::size_t> { destination_index },
                        path.full
                            ? std::nullopt
                            : std::optional<std::size_t> { path_bit })) {
                    continue;
                }
                auto routed_value = value.get(bit);
                if (data_source) {
                    const auto data_bit = data_source->width() == 1
                        ? 0U
                        : path_bit;
                    if (data_bit >= data_source->width())
                        continue;
                    routed_value = data_source->get(data_bit);
                    if (path.polarity == ModulePathPolarity::negative) {
                        auto scalar = unary_not(PackedLogic4 { 1U, routed_value });
                        routed_value = scalar.get(0);
                    }
                }
                const InertialDriverKey projected_key {
                    driver,
                    signal,
                    static_cast<std::uint32_t>(target_bit),
                    1U
                };
                const auto projected = pending_module_path_writes.find(projected_key);
                const auto transition_before = projected == pending_module_path_writes.end()
                    ? driver_current.get(target_bit)
                    : projected->second.source_value.get(0);
                const auto delay = accumulated_delay(
                    transition_before,
                    routed_value,
                    module_path_transition_delay(
                        transition_before, routed_value, path.delays)
                        .value_or(0));
                if (!selected[bit] || delay < *selected[bit]
                    || (delay == *selected[bit] && path.id < orders[bit])) {
                    selected[bit] = delay;
                    orders[bit] = path.id;
                    routed_values[bit] = routed_value;
                    pulse_styles[bit] = path.pulse_style;
                    show_cancelled[bit] = path.show_cancelled;
                    reject_limits[bit] = path.pulse_reject_delays.empty()
                        ? path.pulse_reject_limit
                        : module_path_transition_delay(transition_before,
                              routed_value, path.pulse_reject_delays);
                    error_limits[bit] = path.pulse_error_delays.empty()
                        ? path.pulse_error_limit
                        : module_path_transition_delay(transition_before,
                              routed_value, path.pulse_error_delays);
                    retain_delays[bit] = path.retain_delays.empty()
                        ? std::nullopt
                        : module_path_transition_delay(transition_before,
                              routed_value, path.retain_delays);
                }
                routed = true;
            }
        }
    }
    if (!routed)
        return false;

    for (std::size_t bit = 0; bit < value.width(); ++bit) {
        auto scalar = PackedLogic4 {
            1U, routed_values[bit].value_or(value.get(bit))
        };
        const auto target_bit = write_offset + bit;
        if (!selected[bit]) {
            if (!intrinsic_delays && fixed_delay == 0) {
                stage_update_unrouted(driver, signal, std::move(scalar), target_bit);
                continue;
            }
            selected[bit] = accumulated_delay(
                driver_current.get(target_bit), scalar.get(0), 0);
            orders[bit] = std::numeric_limits<std::uint32_t>::max();
        }
        const InertialDriverKey key {
            driver, signal, static_cast<std::uint32_t>(target_bit), 1U
        };
        bool force_recovery = false;
        if (const auto pending = pending_module_path_writes.find(key);
            pending != pending_module_path_writes.end()) {
            if (pending->second.source_value == scalar)
                continue;
            const auto now = scheduler.now();
            const auto pulse_width = now - pending->second.detected_at;
            const bool rejected = pulse_width < pending->second.reject_limit;
            const auto new_target = *selected[bit]
                    > std::numeric_limits<SimulationTick>::max() - now
                ? std::optional<SimulationTick> { }
                : std::optional<SimulationTick> { now + *selected[bit] };
            if (!new_target) {
                throw std::overflow_error {
                    "simulation time overflow while scheduling module path"
                };
            }
            const bool corrupt = !rejected
                && (pulse_width < pending->second.error_limit
                    || (pending->second.show_cancelled
                        && *new_target < pending->second.target_time));
            const bool negative_cancelled = !rejected
                && pending->second.show_cancelled
                && *new_target < pending->second.target_time;
            scheduler.cancel(pending->second.handle);
            if (corrupt) {
                force_recovery = true;
                auto x_delay = negative_cancelled
                    ? *new_target - now
                    : pending->second.pulse_style == ModulePathPulseStyle::ondetect
                    ? SimulationTick { }
                    : pending->second.target_time > now
                    ? pending->second.target_time - now
                    : SimulationTick { };
                if (negative_cancelled) {
                    *selected[bit] = pending->second.target_time - now;
                }
                if (pending->second.retain_delay) {
                    x_delay = *pending->second.retain_delay > pulse_width
                        ? *pending->second.retain_delay - pulse_width
                        : SimulationTick { };
                }
                if (!pending->second.retain_delay
                    || x_delay < *selected[bit]) {
                    scheduler.schedule_after(
                        x_delay, SchedulerPhase::update, orders[bit],
                        [this, driver, signal, target_bit](Scheduler&) {
                            stage_update_unrouted(
                                driver,
                                signal,
                                PackedLogic4 { 1U, Logic4::x },
                                target_bit);
                        });
                }
            }
            pending_module_path_writes.erase(pending);
            if (rejected && driver_current.get(target_bit) == scalar.get(0)) {
                continue;
            }
        }
        if (!force_recovery
            && driver_current.get(target_bit) == scalar.get(0)) {
            continue;
        }
        if (*selected[bit] == 0) {
            stage_update_unrouted(driver, signal, std::move(scalar), target_bit);
            continue;
        }
        const auto now = scheduler.now();
        if (*selected[bit]
            > std::numeric_limits<SimulationTick>::max() - now) {
            throw std::overflow_error {
                "simulation time overflow while scheduling module path"
            };
        }
        const auto reject = reject_limits[bit].value_or(
            retain_delays[bit] ? SimulationTick { } : *selected[bit]);
        const auto error = error_limits[bit].value_or(
            retain_delays[bit] ? *selected[bit] : reject);
        auto [pending, inserted] = pending_module_path_writes.try_emplace(
            key,
            PendingModulePathWrite {
                ScheduledTaskHandle { },
                scalar,
                now,
                now + *selected[bit],
                reject,
                error,
                retain_delays[bit],
                pulse_styles[bit],
                show_cancelled[bit] });
        (void)inserted;
        try {
            pending->second.handle = scheduler.schedule_after_cancelable(
                *selected[bit], SchedulerPhase::update, orders[bit],
                [this, key, driver, signal, target_bit,
                    scalar = std::move(scalar)](Scheduler&) mutable {
                    pending_module_path_writes.erase(key);
                    stage_update_unrouted(
                        driver, signal, std::move(scalar), target_bit);
                });
        } catch (...) {
            pending_module_path_writes.erase(pending);
            throw;
        }
    }
    return true;
}

void Interpreter::Impl::stage_update_slice(
    const ProcessId process,
    const SignalId signal_id,
    PackedLogic4 value,
    const std::size_t offset)
{
    stage_update_slice(
        std::optional<ProcessId> { process },
        signal_id,
        std::move(value),
        offset);
}

void Interpreter::Impl::schedule_inertial(
    const ProcessId process,
    const SignalId signal,
    PackedLogic4 value,
    const std::optional<std::size_t> offset,
    const TransitionDelays& delays)
{
    (void)get_signal(signal);
    const auto target_width = driven_values[signal].width();
    if (value.width() == 0
        || value.width()
            > std::numeric_limits<std::uint32_t>::max()
        || offset.value_or(0)
            > std::numeric_limits<std::uint32_t>::max()
        || (offset
            && (*offset > target_width
                || value.width() > target_width - *offset))
        || (!offset && value.width() != target_width)) {
        throw std::invalid_argument(
            "inertial write range is outside its target signal");
    }
    value = coerce_value_kind(
        std::move(value), get_signal(signal).value_kind);
    if (route_module_path_update(
            process, signal, value, offset, &delays)) {
        return;
    }
    const InertialDriverKey key {
        process,
        signal,
        static_cast<std::uint32_t>(offset.value_or(0)),
        static_cast<std::uint32_t>(value.width())
    };
    if (const auto pending = pending_inertial_writes.find(key);
        pending != pending_inertial_writes.end()) {
        if (pending->second.source_value == value) {
            return;
        }
        scheduler.cancel(pending->second.handle);
        pending_inertial_writes.erase(pending);
    }
    const auto& driver_current = switch_process(process)
        ? get_signal(signal).initial_value
        : get_signal(signal).resolution == ResolutionKind::none
        ? driven_values[signal]
        : driver_slot(process, signal);
    const auto current = offset
        ? extract_value(
              driver_current, *offset, value.width())
        : driver_current;
    const auto delay = transition_delay(current, value, delays);
    if (!delay) {
        return;
    }
    auto [pending, inserted] = pending_inertial_writes.try_emplace(
        key,
        PendingInertialWrite {
            ScheduledTaskHandle { }, value });
    (void)inserted;
    try {
        pending->second.handle = scheduler.schedule_after_cancelable(
            *delay,
            SchedulerPhase::update,
            process,
            [this,
                key,
                process,
                signal,
                offset,
                value = std::move(value)](Scheduler&) mutable {
                pending_inertial_writes.erase(key);
                if (offset) {
                    stage_update_slice(
                        process,
                        signal,
                        std::move(value),
                        *offset);
                } else {
                    stage_update(
                        process, signal, std::move(value));
                }
            });
    } catch (...) {
        pending_inertial_writes.erase(pending);
        throw;
    }
}

void Interpreter::Impl::schedule_projected_scalar_waveform(
    const ProcessId process,
    const SignalId signal,
    const std::uint32_t offset,
    const std::vector<
        std::pair<PackedLogic4, SimulationTick>>& elements,
    const SimulationTick rejection,
    const ProjectedDelayMode mode)
{
    if (elements.empty()) {
        throw std::invalid_argument(
            "a projected waveform must contain at least one element");
    }
    const auto first_delay = elements.front().second;
    if (mode == ProjectedDelayMode::inertial
        && rejection > first_delay) {
        throw std::invalid_argument(
            "projected-waveform rejection limit exceeds its first delay");
    }
    auto previous_delay = first_delay;
    for (std::size_t index = 0; index < elements.size(); ++index) {
        const auto delay = elements[index].second;
        if (index != 0 && delay <= previous_delay) {
            throw std::invalid_argument(
                "projected-waveform delays must be strictly ascending");
        }
        if (delay
            > std::numeric_limits<SimulationTick>::max()
                - scheduler.now()) {
            throw std::overflow_error(
                "simulation time overflow while scheduling projected waveform");
        }
        previous_delay = delay;
    }
    const auto first_time = scheduler.now() + first_delay;
    const ProjectedDriverKey key { process, signal, offset };
    auto [driver, inserted] = projected_drivers.try_emplace(key);
    (void)inserted;
    auto& transactions = driver->second.transactions;

    const auto first_deleted = std::lower_bound(
        transactions.begin(),
        transactions.end(),
        first_time,
        [](const ProjectedTransaction& transaction,
            const SimulationTick candidate) {
            return transaction.time < candidate;
        });
    for (auto transaction = first_deleted;
        transaction != transactions.end();
        ++transaction) {
        scheduler.cancel(transaction->handle);
    }
    transactions.erase(first_deleted, transactions.end());

    const auto old_count = transactions.size();
    std::vector<std::uint64_t> new_ids;
    new_ids.reserve(elements.size());
    for (const auto& [value, delay] : elements) {
        const auto id = next_projected_transaction_id++;
        new_ids.push_back(id);
        transactions.push_back(ProjectedTransaction {
            id, scheduler.now() + delay, value.get_logic9(0U), { } });
    }
    if (mode == ProjectedDelayMode::inertial
        && old_count != 0) {
        std::vector<bool> marked(transactions.size(), false);
        for (std::size_t index = old_count;
            index < transactions.size();
            ++index) {
            marked[index] = true;
        }
        const auto threshold = first_time - rejection;
        for (std::size_t index = 0; index < old_count; ++index) {
            marked[index] = transactions[index].time < threshold;
        }
        for (std::size_t index = transactions.size() - 1;
            index-- > 0;) {
            if (!marked[index] && marked[index + 1]
                && transactions[index].value
                    == transactions[index + 1].value) {
                marked[index] = true;
            }
        }

        for (std::size_t index = old_count; index-- > 0;) {
            if (!marked[index]) {
                scheduler.cancel(transactions[index].handle);
                transactions.erase(
                    transactions.begin()
                    + static_cast<std::ptrdiff_t>(index));
            }
        }
    }

    for (std::size_t index = 0; index < new_ids.size(); ++index) {
        const auto id = new_ids[index];
        const auto delay = elements[index].second;
        const auto pending = std::ranges::find(
            transactions, id, &ProjectedTransaction::id);
        if (pending == transactions.end()) {
            throw std::logic_error(
                "new projected transaction was not retained");
        }
        try {
            pending->handle = scheduler.schedule_after_cancelable(
                delay,
                SchedulerPhase::update,
                process,
                [this, key, id, process, signal, offset](Scheduler&) {
                    const auto found_driver = projected_drivers.find(key);
                    if (found_driver == projected_drivers.end()) {
                        return;
                    }
                    auto& state = found_driver->second;
                    const auto found_transaction = std::ranges::find(
                        state.transactions,
                        id,
                        &ProjectedTransaction::id);
                    if (found_transaction == state.transactions.end()) {
                        return;
                    }
                    auto committed_value = PackedLogic4 { 1U, Logic4::x };
                    committed_value.fill(found_transaction->value);
                    state.transactions.erase(found_transaction);
                    stage_update_slice(
                        process,
                        signal,
                        committed_value,
                        offset);
                });
        } catch (...) {
            transactions.erase(pending);
            throw;
        }
    }
}

void Interpreter::Impl::schedule_projected_waveform(
    const ProcessId process,
    const SignalId signal,
    const std::vector<ProjectedWaveformValue>& elements,
    const std::optional<std::size_t> offset,
    const SimulationTick rejection,
    const ProjectedDelayMode mode)
{
    (void)get_signal(signal);
    if (elements.empty()) {
        throw std::invalid_argument(
            "a projected waveform must contain at least one element");
    }
    const auto width = elements.front().value.width();
    for (const auto& element : elements) {
        if (element.value.width() != width) {
            throw std::invalid_argument(
                "projected-waveform element widths do not match");
        }
    }
    const auto target_width = driven_values[signal].width();
    const auto first = offset.value_or(0);
    if (width == 0
        || first > target_width
        || width > target_width - first
        || (!offset && width != target_width)
        || first > std::numeric_limits<std::uint32_t>::max()
        || width
            > std::numeric_limits<std::uint32_t>::max()
        || first + width
            > static_cast<std::size_t>(
                std::numeric_limits<std::uint32_t>::max())) {
        throw std::invalid_argument(
            "projected write range is outside its target signal");
    }
    // A single zero-delay inertial element has no pulse to reject.  When this
    // driver's selected scalar elements have no pending projected
    // transactions, staging the update in the current update phase is
    // equivalent to creating and immediately consuming the complete scalar
    // transaction group.  Pending transactions on unrelated drivers or
    // disjoint elements cannot affect this assignment and must not disable the
    // shortcut globally.
    const auto target_has_pending_transaction = [&] {
        for (std::size_t bit = 0; bit < width; ++bit) {
            const auto driver = projected_drivers.find(ProjectedDriverKey {
                process, signal,
                static_cast<std::uint32_t>(first + bit)
            });
            if (driver != projected_drivers.end()
                && !driver->second.transactions.empty()) {
                return true;
            }
        }
        return false;
    };
    if (elements.size() == 1U
        && mode == ProjectedDelayMode::inertial
        && rejection == 0U
        && elements.front().delay == 0U
        && !target_has_pending_transaction()) {
        const auto& value = elements.front().value;
        if (target_width <= 64U && value.is_logic9()) {
            const auto source = value.logic9_low_word();
            std::array<std::uint64_t, 4> planes { };
            for (std::size_t plane = 0; plane < planes.size(); ++plane) {
                planes[plane] = source.planes[plane] << first;
            }
            const auto source_mask = width == 64U
                ? std::numeric_limits<std::uint64_t>::max()
                : (UINT64_C(1) << width) - UINT64_C(1);
            auto mask = source_mask << first;
            const auto slot = ProcessLogic9UpdateSlotView {
                signal,
                static_cast<std::uint32_t>(target_width),
                planes.data(),
                &mask
            };
            if (stage_validated_logic9_update_batch({
                    process, std::span { &slot, 1U } })) {
                return;
            }
        }
        if (offset) {
            stage_update_slice(
                process, signal, value, *offset);
        } else {
            stage_update(
                process, signal, value);
        }
        return;
    }
    if (width > 1U && elements.size() == 1U) {
        const auto& element = elements.front();
        if (mode == ProjectedDelayMode::inertial
            && rejection > element.delay) {
            throw std::invalid_argument(
                "projected-waveform rejection limit exceeds its first delay");
        }
        if (element.delay
            > std::numeric_limits<SimulationTick>::max()
                - scheduler.now()) {
            throw std::overflow_error(
                "simulation time overflow while scheduling projected waveform");
        }
        const auto transaction_time = scheduler.now() + element.delay;
        std::vector<std::uint64_t> transaction_ids;
        transaction_ids.reserve(width);
        for (std::size_t bit = 0; bit < width; ++bit) {
            const ProjectedDriverKey key {
                process, signal,
                static_cast<std::uint32_t>(first + bit)
            };
            auto [driver, inserted] = projected_drivers.try_emplace(key);
            (void)inserted;
            auto& transactions = driver->second.transactions;
            const auto first_deleted = std::lower_bound(
                transactions.begin(),
                transactions.end(),
                transaction_time,
                [](const ProjectedTransaction& transaction,
                    const SimulationTick candidate) {
                    return transaction.time < candidate;
                });
            for (auto transaction = first_deleted;
                transaction != transactions.end(); ++transaction) {
                scheduler.cancel(transaction->handle);
            }
            transactions.erase(first_deleted, transactions.end());

            const auto old_count = transactions.size();
            const auto id = next_projected_transaction_id++;
            transaction_ids.push_back(id);
            transactions.push_back(ProjectedTransaction {
                id, transaction_time, element.value.get_logic9(bit), { } });

            if (mode == ProjectedDelayMode::inertial
                && old_count != 0U) {
                std::vector<bool> marked(transactions.size(), false);
                marked.back() = true;
                const auto threshold = transaction_time - rejection;
                for (std::size_t index = 0; index < old_count; ++index) {
                    marked[index] = transactions[index].time < threshold;
                }
                for (std::size_t index = transactions.size() - 1U;
                    index-- > 0U;) {
                    if (!marked[index] && marked[index + 1U]
                        && transactions[index].value
                            == transactions[index + 1U].value) {
                        marked[index] = true;
                    }
                }
                for (std::size_t index = old_count; index-- > 0U;) {
                    if (!marked[index]) {
                        scheduler.cancel(transactions[index].handle);
                        transactions.erase(
                            transactions.begin()
                            + static_cast<std::ptrdiff_t>(index));
                    }
                }
            }
        }

        scheduler.schedule_after(
            element.delay,
            SchedulerPhase::update,
            process,
            [this, process, signal, first, width,
                ids = std::move(transaction_ids),
                value = element.value](Scheduler&) mutable {
                bool complete_group = true;
                for (std::size_t bit = 0; bit < width; ++bit) {
                    const ProjectedDriverKey key {
                        process, signal,
                        static_cast<std::uint32_t>(first + bit)
                    };
                    const auto driver = projected_drivers.find(key);
                    if (driver == projected_drivers.end()
                        || std::ranges::find(
                               driver->second.transactions,
                               ids[bit],
                               &ProjectedTransaction::id)
                            == driver->second.transactions.end()) {
                        complete_group = false;
                        break;
                    }
                }
                if (complete_group) {
                    for (std::size_t bit = 0; bit < width; ++bit) {
                        const ProjectedDriverKey key {
                            process, signal,
                            static_cast<std::uint32_t>(first + bit)
                        };
                        auto& transactions
                            = projected_drivers.find(key)->second.transactions;
                        transactions.erase(std::ranges::find(
                            transactions,
                            ids[bit],
                            &ProjectedTransaction::id));
                    }
                    stage_update_slice(
                        process, signal, std::move(value), first);
                    return;
                }
                for (std::size_t bit = 0; bit < width; ++bit) {
                    const ProjectedDriverKey key {
                        process, signal,
                        static_cast<std::uint32_t>(first + bit)
                    };
                    const auto driver = projected_drivers.find(key);
                    if (driver == projected_drivers.end()) {
                        continue;
                    }
                    auto& transactions = driver->second.transactions;
                    const auto transaction = std::ranges::find(
                        transactions,
                        ids[bit],
                        &ProjectedTransaction::id);
                    if (transaction == transactions.end()) {
                        continue;
                    }
                    auto scalar = PackedLogic4 { 1U, Logic4::x };
                    scalar.fill(transaction->value);
                    transactions.erase(transaction);
                    stage_update_slice(
                        process,
                        signal,
                        std::move(scalar),
                        first + bit);
                }
            });
        return;
    }
    std::vector<
        std::pair<PackedLogic4, SimulationTick>>
        scalar_elements;
    scalar_elements.reserve(elements.size());
    for (std::size_t bit = 0; bit < width; ++bit) {
        scalar_elements.clear();
        for (const auto& element : elements) {
            auto scalar = PackedLogic4 { 1, Logic4::x };
            if (element.value.is_logic9()) {
                scalar.fill(element.value.get_logic9(bit));
            } else {
                scalar.set(0, element.value.get(bit));
            }
            scalar_elements.emplace_back(
                std::move(scalar), element.delay);
        }
        schedule_projected_scalar_waveform(
            process,
            signal,
            static_cast<std::uint32_t>(first + bit),
            scalar_elements,
            rejection,
            mode);
    }
}

void Interpreter::Impl::schedule_projected(
    const ProcessId process,
    const SignalId signal,
    const PackedLogic4& value,
    const std::optional<std::size_t> offset,
    const SimulationTick delay,
    const SimulationTick rejection,
    const ProjectedDelayMode mode)
{
    schedule_projected_waveform(
        process,
        signal,
        std::vector<ProjectedWaveformValue> { { value, delay } },
        offset,
        rejection,
        mode);
}

void Interpreter::Impl::handle_external_boundary(
    ProcessState& process,
    const InstructionIndex instruction,
    const InstructionIndex next_instruction,
    const ExternalSuspension& suspension)
{
    if (instruction >= process.program.operations.size()) {
        process.pc = instruction;
        fail(process, "executor returned an invalid dynamic boundary instruction");
    }
    if (instruction == std::numeric_limits<InstructionIndex>::max()
        || next_instruction != instruction + 1) {
        process.pc = instruction;
        fail(
            process,
            "executor returned a non-sequential dynamic boundary resume "
            "instruction");
    }
    process.pc = next_instruction;
    clear_wait_timeout(process);

    switch (suspension.kind) {
    case ExternalSuspendKind::simir_boundary:
        process.pc = instruction;
        fail(process, "missing dynamic suspension kind");
    case ExternalSuspendKind::wait_for:
        process.status = ProcessStatus::waiting;
        if (suspension.delay == 0) {
            queue_next_delta(process.program.id);
        } else {
            if (suspension.delay
                > std::numeric_limits<SimulationTick>::max() - scheduler.now()) {
                process.pc = instruction;
                fail(process, "simulation time overflow in dynamic wait");
            }
            queue_at(process.program.id, scheduler.now() + suspension.delay);
        }
        break;
    case ExternalSuspendKind::wait_on:
        process.status = ProcessStatus::waiting;
        if (suspension.sensitivity.empty()) {
            process.pc = instruction;
            fail(process, "dynamic wait requires at least one event");
        }
        process.waiting_on_signal = true;
        process.dynamic_sensitivity = suspension.sensitivity;
        std::sort(
            process.dynamic_sensitivity.begin(),
            process.dynamic_sensitivity.end(),
            [](const Sensitivity& lhs, const Sensitivity& rhs) {
                return lhs.signal < rhs.signal
                    || (lhs.signal == rhs.signal && lhs.edge < rhs.edge);
            });
        process.dynamic_sensitivity.erase(
            std::unique(
                process.dynamic_sensitivity.begin(),
                process.dynamic_sensitivity.end()),
            process.dynamic_sensitivity.end());
        process.dynamic_wait_all = suspension.wait_all;
        process.dynamic_triggered.assign(
            process.dynamic_sensitivity.size(), false);
        for (const auto& sensitivity : process.dynamic_sensitivity) {
            (void)get_signal(sensitivity.signal);
            if (sensitivity.edge != EdgeKind::any) {
                process.pc = instruction;
                fail(process, "dynamic event wait must use any-change sensitivity");
            }
            dynamic_fanout[sensitivity.signal].push_back(
                { process.program.id, sensitivity.edge });
        }
        if (suspension.timeout) {
            begin_wait_timeout(
                process, instruction, *suspension.timeout, std::nullopt);
        }
        break;
    case ExternalSuspendKind::wait_sensitivity:
        process.status = ProcessStatus::waiting;
        if (process.program.static_sensitivity.empty()) {
            process.pc = instruction;
            fail(process, "dynamic static wait has no sensitivity list");
        }
        process.waiting_on_static = true;
        process.static_trigger_mask = 0U;
        break;
    case ExternalSuspendKind::yield:
        process.status = ProcessStatus::waiting;
        queue_next_delta(process.program.id);
        break;
    case ExternalSuspendKind::halt:
        complete_process(process, ProcessStatus::finished);
        break;
    }
    notify_execution_point(
        process, instruction, ExecutionPointKind::process_suspend,
        process.current_source);
}

[[noreturn]] void Interpreter::Impl::fail(const ProcessState& process,
    const std::string& message) const
{
    throw InterpreterError(process.program.id, process.pc, message);
}

} // namespace fsim::runtime::simir
