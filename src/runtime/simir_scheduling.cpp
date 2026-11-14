// SPDX-License-Identifier: Apache-2.0
#include "simir_internal.hpp"
#include "simir_events.hpp"
#include "fsim/runtime/simir_interpreter.hpp"

#include <cstdio>
#include <cstdlib>

namespace fsim::runtime::simir {

void Interpreter::Impl::stage_update_unrouted(
    const std::optional<ProcessId> driver,
    const SignalId signal,
    PackedLogic4 value,
    const std::optional<std::size_t> offset,
    const SignalChangeOrigin origin)
{
    if (origin.process_domain
        == ProcessSchedulingDomain::systemverilog) {
        scheduler.schedule_systemverilog(
            origin.phase, driver.value_or(signal),
            [this, driver, signal, value = std::move(value), offset,
                origin](Scheduler&) mutable {
                if (driver) {
                    if (offset) {
                        commit_driver_slice(
                            *driver, signal, std::move(value), *offset,
                            origin, false);
                    } else {
                        commit_driver(
                            *driver, signal, std::move(value), origin, false);
                    }
                } else if (offset) {
                    commit_slice(
                        signal, std::move(value), *offset, origin);
                } else {
                    commit(signal, std::move(value), origin);
                }
            });
        return;
    }
    if (driver && (process_profile_enabled || update_profile_enabled)) {
        processes.add_profile_updates(*driver, 1U);
    }
    const auto value_index = pending_update_values.size();
    pending_update_values.push_back(std::move(value));
    block_native_logic9_update_before_generic(signal);
    pending_updates.push_back(PendingUpdate {
        signal,
        driver,
        offset,
        { },
        value_index });
    stage_selected_signal_transaction(signal, offset.value_or(0U),
        pending_update_values.back().width(), driver);
    schedule_update_commit();
}

bool Interpreter::Impl::route_module_path_update(
    const ProcessId driver,
    const SignalId signal,
    const PackedLogic4& value,
    const std::optional<std::size_t> offset,
    const TransitionDelays* intrinsic_delays,
    const SimulationTick fixed_delay,
    const std::optional<SignalChangeOrigin> origin)
{
    const auto change_origin = origin.value_or(
        capture_signal_change_origin(driver));
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
            // The driver runs in a later delta of the time step in which
            // the source changed (IEEE 1800-2017 30.4: a path delay is
            // measured from the source transition).
            const auto& stamp = signal_events.at(terminal.signal);
            if (!stamp || stamp->first != scheduler.now()) {
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
                // A path's data source and polarity describe the data flow
                // only; the destination takes the value the module's logic
                // drives (IEEE 1800-2023 30.4.4.3).
                const auto routed_value = value.get(bit);
                const InertialDriverKey projected_key {
                    driver,
                    signal,
                    static_cast<std::uint32_t>(target_bit),
                    1U,
                    change_origin.process_domain,
                    change_origin.phase
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
                stage_update_unrouted(
                    driver, signal, std::move(scalar), target_bit,
                    change_origin);
                continue;
            }
            selected[bit] = accumulated_delay(
                driver_current.get(target_bit), scalar.get(0), 0);
            orders[bit] = std::numeric_limits<std::uint32_t>::max();
        }
        const InertialDriverKey key {
            driver,
            signal,
            static_cast<std::uint32_t>(target_bit),
            1U,
            change_origin.process_domain,
            change_origin.phase
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
                    // A registered event (#128): the pulse reaches the load
                    // as x at `x_delay`.
                    if (x_delay
                        > std::numeric_limits<SimulationTick>::max() - now) {
                        throw std::overflow_error {
                            "simulation time overflow while scheduling event"
                        };
                    }
                    const auto update_x
                        = events::make<events::ModulePathPulseX>(
                            events::ModulePathPulseX::Payload { this, driver,
                                signal, static_cast<std::uint32_t>(target_bit),
                                1U, change_origin.process_domain,
                                change_origin.phase });
                    if (change_origin.process_domain
                        == ProcessSchedulingDomain::systemverilog) {
                        scheduler.schedule_internal_systemverilog_at(
                            now + x_delay, SchedulerPhase::active, orders[bit],
                            update_x);
                    } else {
                        scheduler.schedule_internal_at(
                            now + x_delay, SchedulerPhase::update, orders[bit],
                            update_x);
                    }
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
            stage_update_unrouted(
                driver, signal, std::move(scalar), target_bit,
                change_origin);
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
            // A registered event (#128): the pending record keeps the value.
            const Scheduler::Task mature { events::make<events::ModulePathMature>(
                events::ModulePathMature::Payload { this, driver, signal,
                    static_cast<std::uint32_t>(target_bit), 1U,
                    change_origin.process_domain, change_origin.phase }) };
            if (change_origin.process_domain
                == ProcessSchedulingDomain::systemverilog) {
                pending->second.handle
                    = scheduler.schedule_systemverilog_after_cancelable(
                        *selected[bit], SchedulerPhase::active, orders[bit],
                        mature);
            } else {
                pending->second.handle = scheduler.schedule_after_cancelable(
                    *selected[bit], SchedulerPhase::update, orders[bit],
                    mature);
            }
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
    const std::size_t offset,
    const SignalUpdateDomain domain,
    const std::optional<StableOrder> order)
{
    if (domain != SignalUpdateDomain::generic) {
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
            std::move(value), get_signal(signal_id).value_kind);
        schedule_systemverilog_update(
            process, signal_id, std::move(value), offset, domain, 0, order);
        return;
    }
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
    const TransitionDelays& delays,
    const SignalUpdateDomain domain)
{
    const auto origin = capture_signal_change_origin(process, domain);
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
            process, signal, value, offset, &delays, 0, origin)) {
        return;
    }
    const InertialDriverKey key {
        process,
        signal,
        static_cast<std::uint32_t>(offset.value_or(0)),
        static_cast<std::uint32_t>(value.width()),
        origin.process_domain,
        origin.phase
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
        // A registered event (#128): the pending record keeps the value.
        const Scheduler::Task mature { events::make<events::InertialMature>(
            events::InertialMature::Payload { this, process, signal,
                key.offset, key.width, origin.process_domain, origin.phase,
                offset.has_value() }) };
        if (origin.process_domain
            == ProcessSchedulingDomain::systemverilog) {
            pending->second.handle
                = scheduler.schedule_systemverilog_after_cancelable(
                    *delay, origin.phase, process, mature);
        } else {
            pending->second.handle = scheduler.schedule_after_cancelable(
                *delay, SchedulerPhase::update, process, mature);
        }
    } catch (...) {
        pending_inertial_writes.erase(pending);
        throw;
    }
}

Interpreter::Impl::NetDelayLimits Interpreter::Impl::net_delay_limits(
    const ProcessId process, const WriteNetDelay& operation) const
{
    // A table given to the driver replaces the operation's.
    if (const auto put = net_delay_overrides.empty()
            ? net_delay_overrides.end()
            : net_delay_overrides.find(process);
        put != net_delay_overrides.end()) {
        return { put->second.delay, put->second.reject, put->second.error };
    }
    if (operation.table.size() != 36U) {
        throw std::invalid_argument(
            "interconnect delay write has no delay table");
    }
    const std::span<const SimulationTick> stored { operation.table };
    return { stored.first(12U), stored.subspan(12U, 12U),
        stored.subspan(24U, 12U) };
}

void Interpreter::Impl::schedule_net_delay(
    const ProcessId process,
    const WriteNetDelay& operation,
    PackedLogic4 value)
{
    const auto signal = operation.signal;
    const auto origin = capture_signal_change_origin(process, operation.domain);
    (void)get_signal(signal);
    const auto target_width = driven_values[signal].width();
    const std::size_t offset = operation.offset.value_or(0U);
    if (value.width() == 0
        || value.width() > std::numeric_limits<std::uint32_t>::max()
        || offset > std::numeric_limits<std::uint32_t>::max()
        || (operation.offset
            && (offset > target_width || value.width() > target_width - offset))
        || (!operation.offset && value.width() != target_width)) {
        throw std::invalid_argument(
            "interconnect delay write range is outside its target signal");
    }
    value = coerce_value_kind(std::move(value), get_signal(signal).value_kind);
    const auto width = static_cast<std::uint32_t>(value.width());
    const InertialDriverKey key { process, signal,
        static_cast<std::uint32_t>(offset), width, origin.process_domain,
        origin.phase };
    const auto& driver_current = switch_process(process)
        ? get_signal(signal).initial_value
        : get_signal(signal).resolution == ResolutionKind::none
        ? driven_values[signal]
        : driver_slot(process, signal);
    const auto [delays, rejects, errors] = net_delay_limits(process, operation);
    auto pending = pending_net_delay_writes.find(key);
    if (pending == pending_net_delay_writes.end()) {
        // Nothing pending and nothing to change: the usual case.
        const auto current = extract_value(driver_current, offset, width);
        if (current == value) {
            return;
        }
        pending = pending_net_delay_writes.try_emplace(key).first;
        pending->second.bits.resize(width);
    }
    auto& state = pending->second;
    const auto now = scheduler.now();
    const Scheduler::Task mature { events::make<events::NetDelayMature>(
        events::NetDelayMature::Payload { this, process, signal, key.offset,
            key.width, origin.process_domain, origin.phase }) };
    for (std::uint32_t bit = 0U; bit < width; ++bit) {
        state.change(
            bit, driver_current.get(offset + bit), value.get(bit), now, delays,
            rejects, errors,
            [&](const SimulationTick delay, const SimulationTick,
                PendingNetDelayWrites::Wake& wake) {
                wake.handle = origin.process_domain
                        == ProcessSchedulingDomain::systemverilog
                    ? scheduler.schedule_systemverilog_after_cancelable(
                        delay, origin.phase, process, mature)
                    : scheduler.schedule_after_cancelable(
                        delay, SchedulerPhase::update, process, mature);
            },
            [&](PendingNetDelayWrites::Wake& wake) {
                scheduler.cancel(wake.handle);
            });
    }
    if (state.wakes.empty()) {
        pending_net_delay_writes.erase(pending);
    }
}

void Interpreter::Impl::mature_net_delay(const InertialDriverKey& key,
    const ProcessId process,
    const SignalId signal,
    const SignalChangeOrigin origin)
{
    const auto pending = pending_net_delay_writes.find(key);
    if (pending == pending_net_delay_writes.end()) {
        return;
    }
    auto& state = pending->second;
    const auto matured = state.mature(scheduler.now());
    if (state.wakes.empty()) {
        pending_net_delay_writes.erase(pending);
    }
    if (matured.empty()) {
        return;
    }
    const auto low = matured.front().first;
    const auto high = matured.back().first;
    const auto& driver_current = switch_process(process)
        ? get_signal(signal).initial_value
        : get_signal(signal).resolution == ResolutionKind::none
        ? driven_values[signal]
        : driver_slot(process, signal);
    auto slice = extract_value(driver_current, key.offset + low, high - low + 1U);
    for (const auto& [bit, value] : matured) {
        slice.set(bit - low, value);
    }
    if (origin.process_domain == ProcessSchedulingDomain::systemverilog) {
        commit_driver_slice(
            process, signal, std::move(slice), key.offset + low, origin, false);
    } else {
        stage_update_slice(process, signal, std::move(slice), key.offset + low);
    }
}

void Interpreter::Impl::schedule_projected_scalar_waveform(
    const ProcessId process,
    const SignalId signal,
    const std::uint32_t offset,
    const std::vector<ProjectedWaveformValue>& elements,
    const SimulationTick rejection,
    const ProjectedDelayMode mode,
    const std::vector<bool>* const retained_old)
{
    if (elements.empty()) {
        throw std::invalid_argument(
            "a projected waveform must contain at least one element");
    }
    const auto first_delay = elements.front().delay;
    if (mode == ProjectedDelayMode::inertial
        && rejection > first_delay) {
        throw std::invalid_argument(
            "projected-waveform rejection limit exceeds its first delay");
    }
    auto previous_delay = first_delay;
    for (std::size_t index = 0; index < elements.size(); ++index) {
        const auto delay = elements[index].delay;
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
    for (const auto& element : elements) {
        const auto id = next_projected_transaction_id++;
        new_ids.push_back(id);
        transactions.push_back(ProjectedTransaction {
            id, scheduler.now() + element.delay,
            element.value.get_logic9(0U), { }, element.null });
    }
    if (mode == ProjectedDelayMode::inertial
        && old_count != 0) {
        std::vector<bool> marked(transactions.size(), false);
        for (std::size_t index = old_count;
            index < transactions.size();
            ++index) {
            marked[index] = true;
        }
        if (retained_old != nullptr && retained_old->size() == old_count) {
            // The scalar this bit belongs to decided which of its old
            // transactions survive.
            for (std::size_t index = 0; index < old_count; ++index) {
                marked[index] = (*retained_old)[index];
            }
        } else {
            const auto threshold = first_time - rejection;
            for (std::size_t index = 0; index < old_count; ++index) {
                marked[index] = transactions[index].time < threshold;
            }
            for (std::size_t index = transactions.size() - 1;
                index-- > 0;) {
                if (!marked[index] && marked[index + 1]
                    && same_projected_value(
                        transactions[index], transactions[index + 1])) {
                    marked[index] = true;
                }
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
        const auto delay = elements[index].delay;
        const auto pending = std::ranges::find(
            transactions, id, &ProjectedTransaction::id);
        if (pending == transactions.end()) {
            throw std::logic_error(
                "new projected transaction was not retained");
        }
        try {
            // A registered event (#128), identified by the transaction id.
            pending->handle = scheduler.schedule_after_cancelable(
                delay,
                SchedulerPhase::update,
                process,
                Scheduler::Task { events::make<
                    events::ProjectedTransactionMature>(
                    events::ProjectedTransactionMature::Payload {
                        this, process, signal, offset, id }) });
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
    // The new transactions delete the driver's pending null transactions
    // at or after the first of them, and an inertial assignment those in
    // its pulse rejection interval: a null transaction never has the value
    // of the new one (IEEE 1076-2019 10.5.2.2). A null transaction
    // disconnects the whole driver, so only an assignment of the whole
    // driver deletes one.
    if (!vhdl_pending_disconnects.empty() && first == 0U
        && width == target_width) {
        if (const auto pending = vhdl_pending_disconnects.find(
                std::pair { signal, process });
            pending != vhdl_pending_disconnects.end()) {
            const auto delay = elements.front().delay;
            const auto earliest = scheduler.now() + delay
                - (mode == ProjectedDelayMode::inertial
                        ? std::min(rejection, delay)
                        : SimulationTick { });
            std::erase_if(pending->second, [&](const SimulationTick time) {
                return time >= earliest;
            });
            if (pending->second.empty()) {
                vhdl_pending_disconnects.erase(pending);
            }
        }
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
        && elements.front().null == 0U
        && !target_has_pending_transaction()) {
        const auto& value = elements.front().value;
        // A value transaction reconnects disconnected elements (14.7.2).
        reconnect_vhdl_null_elements(process, signal, first, width);
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
    // The bits of a multi-bit VHDL scalar (an integer, say) keep or reject
    // their old transactions together (IEEE 1076-2019 10.5.2.2 step 3
    // compares the value components of whole transactions), which the
    // per-element path below decides; this bit-wise shortcut serves arrays
    // and the cases without an old transaction to compare.
    const bool whole_scalar_inertial = mode == ProjectedDelayMode::inertial
        && first == 0U && width == target_width
        && [&] {
               const auto scalar = vhdl_scalar_widths.find(signal);
               return scalar != vhdl_scalar_widths.end()
                   && scalar->second == width && target_has_pending_transaction();
           }();
    if (width > 1U && elements.size() == 1U && !whole_scalar_inertial) {
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
                id, transaction_time, element.value.get_logic9(bit), { },
                element.null });

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
                        && same_projected_value(
                            transactions[index], transactions[index + 1U])) {
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

        // A registered event (#128): the bits' transaction ids are
        // consecutive, and their transactions hold the value.
        scheduler.schedule_internal_at(
            scheduler.now() + element.delay,
            SchedulerPhase::update,
            process,
            events::make<events::ProjectedGroupMature>(
                events::ProjectedGroupMature::Payload { this, process, signal,
                    static_cast<std::uint32_t>(first),
                    static_cast<std::uint32_t>(width),
                    transaction_ids.front() }));
        return;
    }
    // The bits of a multi-bit VHDL scalar keep or reject their old
    // transactions together, by comparing whole values (IEEE 1076-2008
    // 10.5.2.2); the bits of an array are separate scalars.
    std::optional<std::vector<bool>> scalar_retained;
    if (mode == ProjectedDelayMode::inertial && first == 0U
        && width == target_width) {
        const auto scalar = vhdl_scalar_widths.find(signal);
        if (scalar != vhdl_scalar_widths.end() && scalar->second == width) {
            const auto first_time = scheduler.now() + elements.front().delay;
            std::vector<const std::vector<ProjectedTransaction>*> bits(width);
            bool aligned = true;
            std::size_t old_count = std::numeric_limits<std::size_t>::max();
            for (std::size_t bit = 0; bit < width && aligned; ++bit) {
                const auto driver = projected_drivers.find(ProjectedDriverKey {
                    process, signal, static_cast<std::uint32_t>(bit) });
                static const std::vector<ProjectedTransaction> empty;
                bits[bit] = driver == projected_drivers.end()
                    ? &empty : &driver->second.transactions;
                std::size_t kept { };
                while (kept < bits[bit]->size()
                    && (*bits[bit])[kept].time < first_time) {
                    ++kept;
                }
                if (old_count == std::numeric_limits<std::size_t>::max()) {
                    old_count = kept;
                } else if (kept != old_count) {
                    aligned = false;
                }
            }
            for (std::size_t index = 0; aligned && index < old_count;
                 ++index) {
                for (std::size_t bit = 1; bit < width; ++bit) {
                    if ((*bits[bit])[index].time != (*bits[0])[index].time) {
                        aligned = false;
                        break;
                    }
                }
            }
            if (aligned && old_count != 0U
                && old_count != std::numeric_limits<std::size_t>::max()) {
                const auto threshold = first_time - rejection;
                const auto old_value_equals_next = [&](const std::size_t index) {
                    for (std::size_t bit = 0; bit < width; ++bit) {
                        const auto& here = (*bits[bit])[index];
                        const auto next = index + 1U < old_count
                            ? (*bits[bit])[index + 1U]
                            : ProjectedTransaction { 0U, 0U,
                                  elements.front().value.get_logic9(bit), { },
                                  elements.front().null };
                        if (!same_projected_value(here, next)) {
                            return false;
                        }
                    }
                    return true;
                };
                std::vector<bool> retained(old_count + 1U, true);
                for (std::size_t index = 0; index < old_count; ++index) {
                    retained[index] = (*bits[0])[index].time < threshold;
                }
                for (std::size_t index = old_count; index-- > 0;) {
                    if (!retained[index] && retained[index + 1U]
                        && old_value_equals_next(index)) {
                        retained[index] = true;
                    }
                }
                retained.pop_back();
                scalar_retained = std::move(retained);
            }
        }
    }
    std::vector<ProjectedWaveformValue> scalar_elements;
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
            scalar_elements.push_back(
                { std::move(scalar), element.delay, element.null });
        }
        schedule_projected_scalar_waveform(
            process,
            signal,
            static_cast<std::uint32_t>(first + bit),
            scalar_elements,
            rejection,
            mode,
            scalar_retained ? &*scalar_retained : nullptr);
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
    if (suspension.kind
        != ExternalSuspendKind::validated_wait_sensitivity) {
        if (instruction >= process.program().operations().size()) {
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
    }
    process.pc = next_instruction;
    if (suspension.kind != ExternalSuspendKind::wait_on) {
        clear_wait_timeout(process);
    }

    switch (suspension.kind) {
    case ExternalSuspendKind::simir_boundary:
        process.pc = instruction;
        fail(process, "missing dynamic suspension kind");
    case ExternalSuspendKind::wait_for:
        process.status = ProcessStatus::waiting;
        if (suspension.delay == 0) {
            queue_zero_delay_resume(process);
        } else {
            if (suspension.delay
                > std::numeric_limits<SimulationTick>::max() - scheduler.now()) {
                process.pc = instruction;
                fail(process, "simulation time overflow in dynamic wait");
            }
            queue_timeout(process, scheduler.now() + suspension.delay);
        }
        break;
    case ExternalSuspendKind::wait_on: {
        if (suspension.sensitivity.empty()) {
            clear_wait_timeout(process);
            process.pc = instruction;
            fail(process, "dynamic wait requires at least one event");
        }
        const auto resume_pc = process.pc;
        process.pc = instruction;
        auto* dynamic_wait = &process.cold().ensure_dynamic_wait_state();
        process.pc = resume_pc;
        clear_wait_timeout(process);
        process.status = ProcessStatus::waiting;
        process.waiting_on_signal = true;
        dynamic_wait->dynamic_sensitivity = suspension.sensitivity;
        std::sort(
            dynamic_wait->dynamic_sensitivity.begin(),
            dynamic_wait->dynamic_sensitivity.end(),
            [](const Sensitivity& lhs, const Sensitivity& rhs) {
                return lhs.signal < rhs.signal
                    || (lhs.signal == rhs.signal && lhs.edge < rhs.edge);
            });
        dynamic_wait->dynamic_sensitivity.erase(
            std::unique(
                dynamic_wait->dynamic_sensitivity.begin(),
                dynamic_wait->dynamic_sensitivity.end()),
            dynamic_wait->dynamic_sensitivity.end());
        dynamic_wait->dynamic_wait_all = suspension.wait_all;
        dynamic_wait->dynamic_triggered.assign(
            dynamic_wait->dynamic_sensitivity.size(), false);
        for (const auto& sensitivity : dynamic_wait->dynamic_sensitivity) {
            (void)get_signal(sensitivity.signal);
            if (sensitivity.edge != EdgeKind::any) {
                process.pc = instruction;
                fail(process, "dynamic event wait must use any-change sensitivity");
            }
        }
        register_dynamic_wait_fanout(process);
        if (suspension.timeout) {
            begin_wait_timeout(
                process, instruction, *suspension.timeout, std::nullopt);
        }
        break;
    }
    case ExternalSuspendKind::wait_sensitivity:
    case ExternalSuspendKind::validated_wait_sensitivity:
        process.status = ProcessStatus::waiting;
        if (suspension.kind == ExternalSuspendKind::wait_sensitivity
            && process.program().static_sensitivity().empty()) {
            process.pc = instruction;
            fail(process, "dynamic static wait has no sensitivity list");
        }
        process.waiting_on_static = true;
        process.static_trigger_mask = 0U;
        break;
    case ExternalSuspendKind::yield:
        process.status = ProcessStatus::waiting;
        queue_next_delta(process.id);
        break;
    case ExternalSuspendKind::halt:
        complete_process(process, ProcessStatus::finished);
        break;
    }
    if (execution_point_hook) {
        notify_execution_point(
            process, instruction, ExecutionPointKind::process_suspend,
            process.cold().current_source);
    }
}

[[noreturn]] void Interpreter::Impl::fail(const ProcessState& process,
    const std::string& message) const
{
    throw InterpreterError(process.id, process.pc, message);
}

} // namespace fsim::runtime::simir
