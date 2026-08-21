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

void Interpreter::Impl::commit(SignalId signal_id, PackedLogic4 value)
{
    const auto& signal = get_signal(signal_id);
    if (driven_values[signal_id].width() != value.width()) {
        throw std::invalid_argument("SimIR signal assignment width mismatch");
    }
    value = normalize_signal_value(
        signal_id, std::move(value));
    if (signal.event_variable) {
        const auto identity = event_identities.at(signal_id);
        if (!identity) {
            return;
        }
        const auto old_value = signals[signal_id].initial_value;
        std::vector<SignalId> members;
        for (std::size_t index = 0; index < signals.size(); ++index) {
            if (signals[index].event_variable
                && event_identities[index] == identity) {
                members.push_back(static_cast<SignalId>(index));
            }
        }
        for (const auto member : members) {
            const bool stored_changed = driven_values[member] != value;
            driven_values[member] = value;
            if (stored_changed && stored_signal_change_hook) {
                stored_signal_change_hook(member, scheduler.now());
            }
            publish_normalized(member, apply_force(member, value), false);
        }
        for (const auto member : members) {
            for (const auto& sensitivity : static_fanout[member]) {
                auto& process = get_process(sensitivity.process);
                process.static_trigger_mask
                    |= sensitivity.static_trigger_mask;
                if (!process.waiting_on_static) {
                    continue;
                }
                if (sensitivity.edge != EdgeKind::any
                    && sensitivity.edge != EdgeKind::transaction
                    && (old_value.width() != 1
                        || !edge_matches(
                            sensitivity.edge, old_value.get(0),
                            value.get(0)))) {
                    continue;
                }
                queue_static_next_delta(sensitivity.process);
            }
        }
        const auto dynamic = dynamic_fanout.at(*identity);
        for (const auto sensitivity : dynamic) {
            auto& process = get_process(sensitivity.process);
            if (dynamic_wait_satisfied(
                    process, *identity, sensitivity.edge)) {
                mark_dynamic_event_resume(process);
                queue_next_delta(sensitivity.process);
            }
        }
        refresh_switch_network();
        return;
    }
    const bool stored_changed = driven_values[signal_id] != value;
    driven_values[signal_id] = value;
    if (stored_changed && stored_signal_change_hook) {
        stored_signal_change_hook(signal_id, scheduler.now());
    }
    publish_normalized(signal_id, apply_force(signal_id, std::move(value)));
    if (stored_changed) {
        publish_container_signal_aliases(signal_id);
    }
    refresh_switch_network();
}

void Interpreter::Impl::commit_direct_single_driver(
    const SignalId signal_id,
    PackedLogic4 value)
{
    // This path is selected only for a prevalidated native write to an
    // unforced, strength-free SystemVerilog net with one driver. The generic
    // resolved-net path would normalize the value, resolve the sole driver,
    // apply an absent force, and probe an absent switch network before doing
    // the same publication. Recheck the route at commit time because a force
    // or external deposit may have invalidated it after the write was staged.
    const auto& route = direct_single_driver_routes[signal_id];
    if (route.value == nullptr) {
        commit_resolved(signal_id, std::move(value));
        return;
    }
    const bool stored_changed = driven_values[signal_id] != value;
    driven_values[signal_id] = value;
    if (stored_changed && stored_signal_change_hook) {
        stored_signal_change_hook(signal_id, scheduler.now());
    }
    publish_normalized(signal_id, std::move(value));
    if (stored_changed) {
        publish_container_signal_aliases(signal_id);
    }
}

void Interpreter::Impl::publish_container_signal_aliases(
    const SignalId signal_id)
{
    if (!container_object_change_hook) {
        return;
    }
    for (const auto object : signal_container_aliases.at(signal_id)) {
        container_object_change_hook(object, scheduler.now());
    }
}

void Interpreter::Impl::refresh_switch_network()
{
    if (!has_bidirectional_switches || switch_refreshing)
        return;
    std::set<SignalId> endpoints;
    for (const auto& state : processes) {
        if (!state.program.switch_bidirectional
            || !state.program.switch_source
            || !state.program.switch_target) {
            continue;
        }
        endpoints.insert(*state.program.switch_source);
        endpoints.insert(*state.program.switch_target);
    }
    if (endpoints.empty())
        return;
    std::vector<std::pair<SignalId, PackedLogic4>> values;
    values.reserve(endpoints.size());
    for (const auto signal : endpoints) {
        values.emplace_back(signal, resolved_driver_value(signal));
    }
    switch_refreshing = true;
    for (auto& [signal, value] : values) {
        const bool stored_changed = driven_values[signal] != value;
        driven_values[signal] = value;
        if (stored_changed && stored_signal_change_hook) {
            stored_signal_change_hook(signal, scheduler.now());
        }
        publish(signal, apply_force(signal, std::move(value)));
        if (stored_changed) {
            publish_container_signal_aliases(signal);
        }
    }
    switch_refreshing = false;
}

PackedLogic4 Interpreter::Impl::apply_force(
    const SignalId signal_id,
    PackedLogic4 value) const
{
    if (!forced_values[signal_id]) {
        return value;
    }
    const auto& forced = *forced_values[signal_id];
    const auto& mask = *forced_masks[signal_id];
    for (std::size_t bit = 0; bit < value.width(); ++bit) {
        if (mask.get(bit) != Logic4::one) {
            continue;
        }
        if (value.is_logic9()) {
            value.set_logic9(bit, forced.get_logic9(bit));
        } else {
            value.set(bit, forced.get(bit));
        }
    }
    return value;
}

void Interpreter::Impl::force_slice(
    const SignalId signal_id,
    PackedLogic4 value,
    const std::size_t offset)
{
    const auto& signal = get_signal(signal_id);
    if (signal.systemverilog_scalar != SystemVerilogScalarKind::None
        && (offset != 0 || value.width() != signal.initial_value.width())) {
        throw std::invalid_argument {
            "SimIR scalar signals do not support partial force"
        };
    }
    if (offset > signal.initial_value.width()
        || value.width() > signal.initial_value.width() - offset) {
        throw std::invalid_argument("SimIR signal force slice is out of range");
    }
    value = coerce_value_kind(std::move(value), signal.value_kind);
    if (!forced_values[signal_id]) {
        forced_values[signal_id]
            = std::make_unique<PackedLogic4>(driven_values[signal_id]);
        forced_masks[signal_id] = std::make_unique<PackedLogic4>(
            signal.initial_value.width(), Logic4::zero);
    }
    *forced_values[signal_id] = insert_value(
        std::move(*forced_values[signal_id]), value, offset);
    for (std::size_t bit = 0; bit < value.width(); ++bit) {
        forced_masks[signal_id]->set(offset + bit, Logic4::one);
    }
    refresh_direct_single_driver_route(signal_id);
    publish(
        signal_id,
        apply_force(signal_id, driven_values[signal_id]));
}

void Interpreter::Impl::release_slice(
    const SignalId signal_id,
    const std::size_t offset,
    const std::size_t width)
{
    const auto& signal = get_signal(signal_id);
    if (signal.systemverilog_scalar != SystemVerilogScalarKind::None
        && (offset != 0 || width != signal.initial_value.width())) {
        throw std::invalid_argument {
            "SimIR scalar signals do not support partial release"
        };
    }
    if (offset > signal.initial_value.width()
        || width > signal.initial_value.width() - offset) {
        throw std::invalid_argument("SimIR signal release slice is out of range");
    }
    if (!forced_values[signal_id]) {
        return;
    }
    for (std::size_t bit = 0; bit < width; ++bit) {
        forced_masks[signal_id]->set(offset + bit, Logic4::zero);
    }
    bool any_forced = false;
    for (std::size_t bit = 0; bit < signal.initial_value.width(); ++bit) {
        any_forced = any_forced
            || forced_masks[signal_id]->get(bit) == Logic4::one;
    }
    if (!any_forced) {
        forced_values[signal_id].reset();
        forced_masks[signal_id].reset();
    }
    refresh_direct_single_driver_route(signal_id);
    publish(
        signal_id,
        apply_force(signal_id, driven_values[signal_id]));
}

PackedLogic4 Interpreter::Impl::apply_driver_force(
    const SignalId signal_id,
    const ProcessId process,
    PackedLogic4 value) const
{
    const auto& values = forced_driver_values.at(signal_id);
    if (!values) {
        return value;
    }
    const auto forced = values->find(process);
    if (forced == values->end()) {
        return value;
    }
    const auto& masks = forced_driver_masks.at(signal_id);
    const auto mask = masks->find(process);
    if (mask == masks->end()) {
        throw std::logic_error { "forced driver has no force mask" };
    }
    for (std::size_t bit = 0; bit < value.width(); ++bit) {
        if (mask->second.get(bit) != Logic4::one) {
            continue;
        }
        if (value.is_logic9()) {
            value.set_logic9(bit, forced->second.get_logic9(bit));
        } else {
            value.set(bit, forced->second.get(bit));
        }
    }
    return value;
}

Logic4 Interpreter::Impl::driver_force_logic4_at(
    const SignalId signal_id,
    const ProcessId process,
    const PackedLogic4& value,
    const std::size_t bit) const
{
    const auto& values = forced_driver_values.at(signal_id);
    if (!values) {
        return value.get(bit);
    }
    const auto forced = values->find(process);
    if (forced == values->end()) {
        return value.get(bit);
    }
    const auto& masks = forced_driver_masks.at(signal_id);
    const auto mask = masks->find(process);
    if (mask == masks->end()) {
        throw std::logic_error { "forced driver has no force mask" };
    }
    return mask->second.get(bit) == Logic4::one
        ? forced->second.get(bit)
        : value.get(bit);
}

void Interpreter::Impl::force_driver_slice(
    const ProcessId process,
    const SignalId signal_id,
    PackedLogic4 value,
    const std::size_t offset)
{
    const auto& signal = get_signal(signal_id);
    if (offset > signal.initial_value.width()
        || value.width() > signal.initial_value.width() - offset) {
        throw std::invalid_argument {
            "SimIR driver force slice is out of range"
        };
    }
    if (signal.resolution == ResolutionKind::none) {
        force_slice(signal_id, std::move(value), offset);
        return;
    }
    value = coerce_value_kind(std::move(value), signal.value_kind);
    auto& forced = forced_driver_values.at(signal_id);
    auto& masks = forced_driver_masks.at(signal_id);
    if (!forced) {
        forced = std::make_unique<ForcedDriverMap>();
        masks = std::make_unique<ForcedDriverMap>();
    }
    auto [forced_entry, inserted] = forced->try_emplace(
        process, driver_slot(process, signal_id));
    if (inserted) {
        masks->emplace(
            process,
            PackedLogic4 { signal.initial_value.width(), Logic4::zero });
    }
    forced_entry->second = insert_value(
        std::move(forced_entry->second), value, offset);
    auto& mask = masks->at(process);
    for (std::size_t bit = 0; bit < value.width(); ++bit) {
        mask.set(offset + bit, Logic4::one);
    }
    refresh_direct_single_driver_route(signal_id);
    auto resolved = resolved_driver_value(signal_id);
    driven_values[signal_id] = resolved;
    publish(signal_id, apply_force(signal_id, std::move(resolved)));
}

void Interpreter::Impl::release_driver_slice(
    const ProcessId process,
    const SignalId signal_id,
    const std::size_t offset,
    const std::size_t width)
{
    const auto& signal = get_signal(signal_id);
    if (offset > signal.initial_value.width()
        || width > signal.initial_value.width() - offset) {
        throw std::invalid_argument {
            "SimIR driver release slice is out of range"
        };
    }
    if (signal.resolution == ResolutionKind::none) {
        release_slice(signal_id, offset, width);
        return;
    }
    auto& forced = forced_driver_values.at(signal_id);
    auto& masks = forced_driver_masks.at(signal_id);
    if (!forced) {
        return;
    }
    const auto forced_entry = forced->find(process);
    const auto mask_entry = masks->find(process);
    if (forced_entry == forced->end() || mask_entry == masks->end()) {
        return;
    }
    for (std::size_t bit = 0; bit < width; ++bit) {
        mask_entry->second.set(offset + bit, Logic4::zero);
    }
    bool any_forced = false;
    for (std::size_t bit = 0; bit < signal.initial_value.width(); ++bit) {
        any_forced = any_forced
            || mask_entry->second.get(bit) == Logic4::one;
    }
    if (!any_forced) {
        forced->erase(forced_entry);
        masks->erase(mask_entry);
        if (forced->empty()) {
            forced.reset();
            masks.reset();
        }
    }
    refresh_direct_single_driver_route(signal_id);
    auto resolved = resolved_driver_value(signal_id);
    driven_values[signal_id] = resolved;
    publish(signal_id, apply_force(signal_id, std::move(resolved)));
}

[[nodiscard]] PackedLogic4 Interpreter::Impl::initial_driver_value(
    const SignalId signal_id) const
{
    const auto& signal = get_signal(signal_id);
    if (signal.value_kind == ValueKind::logic9) {
        auto result = PackedLogic4 {
            signal.initial_value.width(), Logic4::x
        };
        result.fill(Logic9::u);
        return result;
    }
    const auto initial = signal.resolution == ResolutionKind::sv_wire
            || signal.resolution == ResolutionKind::sv_wand
            || signal.resolution == ResolutionKind::sv_wor
            || signal.resolution == ResolutionKind::sv_user_first
        ? Logic4::z
        : signal.resolution == ResolutionKind::vhdl_user_or
        ? Logic4::zero
        : signal.resolution == ResolutionKind::vhdl_user_and
        ? Logic4::one
        : Logic4::x;
    return PackedLogic4 { signal.initial_value.width(), initial };
}

PackedLogic4& Interpreter::Impl::driver_slot(
    const ProcessId process,
    const SignalId signal_id)
{
    auto& values = driver_values.at(signal_id);
    const auto found = values.find(process);
    if (found != values.end()) {
        return found->second;
    }
    const auto [entry, inserted] = values.try_emplace(
        process, initial_driver_value(signal_id));
    if (inserted) {
        driver_strengths.at(signal_id).insert_or_assign(
            process, get_process(process).program.drive_strength);
        refresh_direct_single_driver_route(signal_id);
    }
    return entry->second;
}

void Interpreter::Impl::refresh_direct_single_driver_route(
    const SignalId signal_id)
{
    materialize_direct_signal(signal_id);
    auto& route = direct_single_driver_routes.at(signal_id);
    route = { };
    direct_single_driver_processes.at(signal_id)
        = std::numeric_limits<ProcessId>::max();
    const auto& signal = get_signal(signal_id);
    auto& values = driver_values.at(signal_id);
    const bool identity_single_driver_resolution
        = signal.resolution == ResolutionKind::sv_wire
        || signal.resolution == ResolutionKind::std_logic;
    if (has_bidirectional_switches
        || !identity_single_driver_resolution
        || values.size() != 1U
        || signal.implicit_driver
        || signal.charge_strength
        || external_driver_values.at(signal_id)
        || forced_values.at(signal_id)
        || forced_driver_values.at(signal_id)) {
        return;
    }
    const auto process = values.begin()->first;
    if (driver_strengths.at(signal_id).at(process) != DriveStrength { }) {
        return;
    }
    route.process = process;
    route.value = &values.begin()->second;
    direct_single_driver_processes[signal_id] = process;
}

[[nodiscard]] PackedLogic4 Interpreter::Impl::resolved_local_driver_value(
    const SignalId signal_id) const
{
    const auto& values = driver_values.at(signal_id);
    if (values.empty()
        && !external_driver_values.at(signal_id)) {
        return get_signal(signal_id).initial_value;
    }
    const auto& signal = get_signal(signal_id);
    const bool identity_single_driver_resolution
        = signal.resolution == ResolutionKind::sv_wire
        || signal.resolution == ResolutionKind::std_logic;
    if (identity_single_driver_resolution
        && values.size() == 1U
        && !external_driver_values.at(signal_id)
        && !signal.implicit_driver
        && !signal.charge_strength
        && driver_strengths.at(signal_id).at(values.begin()->first)
            == DriveStrength { }) {
        return apply_driver_force(
            signal_id, values.begin()->first, values.begin()->second);
    }
    struct DriverContribution {
        const PackedLogic4* value { };
        DriveStrength strength;
    };
    std::vector<PackedLogic4> drivers;
    std::vector<DriverContribution> strength_drivers;
    drivers.reserve(
        values.size()
        + (external_driver_values.at(signal_id) ? 1U : 0U));
    strength_drivers.reserve(
        values.size() + (external_driver_values.at(signal_id) ? 1U : 0U));
    for (const auto& [process, value] : values) {
        drivers.push_back(
            apply_driver_force(signal_id, process, value));
        strength_drivers.push_back({ &drivers.back(), driver_strengths.at(signal_id).at(process) });
    }
    if (external_driver_values.at(signal_id)) {
        drivers.push_back(
            *external_driver_values.at(signal_id));
        strength_drivers.push_back({ &*external_driver_values.at(signal_id), { } });
    }
    const auto resolution = signal.resolution;
    if (resolution == ResolutionKind::sv_user_first) {
        return drivers.front();
    }
    if (resolution == ResolutionKind::sv_wire) {
        const auto equal_strength_logic4 = !signal.implicit_driver
            && !signal.charge_strength
            && std::ranges::all_of(
                strength_drivers,
                [](const DriverContribution& driver) {
                    return driver.strength == DriveStrength { }
                        && !driver.value->is_logic9();
                });
        if (equal_strength_logic4) {
            return runtime::resolve(
                std::span<const PackedLogic4> { drivers });
        }
        auto result = PackedLogic4 {
            signal.initial_value.width(), Logic4::z
        };
        const auto rank = [](const StrengthRank strength) {
            return static_cast<std::underlying_type_t<StrengthRank>>(strength);
        };
        for (std::size_t bit = 0; bit < result.width(); ++bit) {
            auto zero = StrengthRank::highz;
            auto one = StrengthRank::highz;
            if (signal.implicit_driver == Logic4::zero) {
                zero = signal.implicit_drive_strength.zero;
            } else if (signal.implicit_driver == Logic4::one) {
                one = signal.implicit_drive_strength.one;
            } else if (signal.implicit_driver == Logic4::x) {
                zero = signal.implicit_drive_strength.zero;
                one = signal.implicit_drive_strength.one;
            }
            if (signal.charge_strength
                && charge_values.at(signal_id)) {
                const auto charge = charge_values.at(signal_id)->get(bit);
                if (charge == Logic4::zero || charge == Logic4::x) {
                    zero = std::max(zero, *signal.charge_strength);
                }
                if (charge == Logic4::one || charge == Logic4::x) {
                    one = std::max(one, *signal.charge_strength);
                }
            }
            for (const auto& driver : strength_drivers) {
                switch (driver.value->get(bit)) {
                case Logic4::zero:
                    if (rank(driver.strength.zero) > rank(zero)) {
                        zero = driver.strength.zero;
                    }
                    break;
                case Logic4::one:
                    if (rank(driver.strength.one) > rank(one)) {
                        one = driver.strength.one;
                    }
                    break;
                case Logic4::x:
                    if (rank(driver.strength.zero) > rank(zero)) {
                        zero = driver.strength.zero;
                    }
                    if (rank(driver.strength.one) > rank(one)) {
                        one = driver.strength.one;
                    }
                    break;
                case Logic4::z:
                    break;
                }
            }
            const auto zero_rank = rank(zero);
            const auto one_rank = rank(one);
            result.set(
                bit,
                zero_rank == 0 && one_rank == 0
                    ? Logic4::z
                    : zero_rank > one_rank
                    ? Logic4::zero
                    : one_rank > zero_rank
                    ? Logic4::one
                    : Logic4::x);
        }
        return result;
    }
    if (resolution == ResolutionKind::sv_wand
        || resolution == ResolutionKind::sv_wor) {
        const bool bitwise_and = resolution == ResolutionKind::sv_wand;
        auto result = PackedLogic4 {
            signal.initial_value.width(), Logic4::z
        };
        for (std::size_t bit = 0; bit < result.width(); ++bit) {
            auto value = bitwise_and ? Logic4::one : Logic4::zero;
            bool driven = false;
            for (const auto& driver : strength_drivers) {
                auto candidate = driver.value->get(bit);
                if (candidate == Logic4::zero
                    && driver.strength.zero == StrengthRank::highz) {
                    candidate = Logic4::z;
                } else if (candidate == Logic4::one
                    && driver.strength.one == StrengthRank::highz) {
                    candidate = Logic4::z;
                } else if (candidate == Logic4::x) {
                    const auto zero = static_cast<
                        std::underlying_type_t<StrengthRank>>(
                        driver.strength.zero);
                    const auto one = static_cast<
                        std::underlying_type_t<StrengthRank>>(
                        driver.strength.one);
                    candidate = zero > one
                        ? Logic4::zero
                        : one > zero ? Logic4::one
                                     : Logic4::x;
                }
                if (candidate == Logic4::z) {
                    continue;
                }
                driven = true;
                value = bitwise_and
                    ? runtime::logic_and(value, candidate)
                    : runtime::logic_or(value, candidate);
            }
            result.set(bit, driven ? value : Logic4::z);
        }
        return result;
    }
    if (resolution
            != ResolutionKind::vhdl_user_or
        && resolution
            != ResolutionKind::vhdl_user_and) {
        return runtime::resolve(
            std::span<const PackedLogic4> { drivers });
    }
    const bool bitwise_and = signal.resolution
        == ResolutionKind::vhdl_user_and;
    auto result = drivers.front();
    for (auto driver_index = std::size_t { 1 };
        driver_index < drivers.size(); ++driver_index) {
        const auto& driver = drivers[driver_index];
        for (std::size_t bit = 0; bit < result.width(); ++bit) {
            if (signal.value_kind == ValueKind::logic9) {
                result.set_logic9(
                    bit,
                    bitwise_and
                        ? runtime::logic_and(
                              result.get_logic9(bit),
                              driver.get_logic9(bit))
                        : runtime::logic_or(
                              result.get_logic9(bit),
                              driver.get_logic9(bit)));
                continue;
            }
            const auto current = result.get(bit);
            const auto value = driver.get(bit);
            result.set(
                bit,
                bitwise_and
                    ? runtime::logic_and(current, value)
                    : runtime::logic_or(current, value));
        }
    }
    return result;
}

PackedLogic4 Interpreter::Impl::resolved_driver_value(
    const SignalId signal_id) const
{
    if (!has_bidirectional_switches
        || get_signal(signal_id).resolution != ResolutionKind::sv_wire) {
        return resolved_local_driver_value(signal_id);
    }
    const auto connected = std::ranges::any_of(
        processes, [&](const ProcessState& process) {
            return process.program.switch_bidirectional
                && process.program.switch_source
                && process.program.switch_target
                && (*process.program.switch_source == signal_id
                    || *process.program.switch_target == signal_id);
        });
    if (!connected) {
        return resolved_local_driver_value(signal_id);
    }
    const auto reduce = [](StrengthRank rank, std::uint8_t count) {
        while (count-- != 0) {
            switch (rank) {
            case StrengthRank::supply:
            case StrengthRank::strong:
                rank = StrengthRank::pull;
                break;
            case StrengthRank::pull:
                rank = StrengthRank::weak;
                break;
            case StrengthRank::large:
            case StrengthRank::weak:
                rank = StrengthRank::medium;
                break;
            case StrengthRank::medium:
            case StrengthRank::small:
                rank = StrengthRank::small;
                break;
            case StrengthRank::highz:
                break;
            }
        }
        return rank;
    };
    struct Node {
        SignalId signal { };
        std::size_t bit { };
        std::uint8_t resistance { };
        bool uncertain { };
    };
    auto result = PackedLogic4 {
        get_signal(signal_id).initial_value.width(), Logic4::z
    };
    for (std::size_t target_bit = 0;
        target_bit < result.width(); ++target_bit) {
        std::deque<Node> pending { { signal_id, target_bit, 0, false } };
        std::map<std::tuple<SignalId, std::size_t, bool>, std::uint8_t> visited;
        auto zero = StrengthRank::highz;
        auto one = StrengthRank::highz;
        while (!pending.empty()) {
            const auto node = pending.front();
            pending.pop_front();
            const auto key = std::tuple { node.signal, node.bit, node.uncertain };
            if (const auto found = visited.find(key);
                found != visited.end() && found->second <= node.resistance) {
                continue;
            }
            visited.insert_or_assign(key, node.resistance);
            const auto local = resolved_local_driver_value(node.signal);
            auto logic = local.get(node.bit);
            auto strength = resolved_signal_strength(node.signal);
            strength.zero = reduce(strength.zero, node.resistance);
            strength.one = reduce(strength.one, node.resistance);
            if (node.uncertain && logic != Logic4::z) {
                const auto uncertain_strength = std::max(strength.zero, strength.one);
                strength = DriveStrength {
                    uncertain_strength, uncertain_strength
                };
                logic = Logic4::x;
            }
            if (logic == Logic4::zero || logic == Logic4::x) {
                zero = std::max(zero, strength.zero);
            }
            if (logic == Logic4::one || logic == Logic4::x) {
                one = std::max(one, strength.one);
            }
            for (const auto& state : processes) {
                const auto& edge = state.program;
                if (!edge.switch_bidirectional
                    || !edge.switch_source || !edge.switch_target)
                    continue;
                SignalId other { };
                std::vector<std::pair<std::size_t, std::size_t>>
                    selected_bits;
                if (edge.switch_width != 0) {
                    const auto width = static_cast<std::size_t>(
                        edge.switch_width);
                    const auto source_offset = static_cast<std::size_t>(
                        edge.switch_source_offset);
                    const auto target_offset = static_cast<std::size_t>(
                        edge.switch_target_offset);
                    if (*edge.switch_source == node.signal
                        && node.bit >= source_offset
                        && node.bit - source_offset < width) {
                        const auto lane = node.bit - source_offset;
                        other = *edge.switch_target;
                        selected_bits.emplace_back(
                            target_offset + lane, lane);
                    }
                    if (*edge.switch_target == node.signal
                        && node.bit >= target_offset
                        && node.bit - target_offset < width) {
                        const auto lane = node.bit - target_offset;
                        other = *edge.switch_source;
                        selected_bits.emplace_back(
                            source_offset + lane, lane);
                    }
                    if (selected_bits.empty())
                        continue;
                } else if (*edge.switch_source == node.signal) {
                    other = *edge.switch_target;
                } else if (*edge.switch_target == node.signal) {
                    other = *edge.switch_source;
                } else {
                    continue;
                }
                const auto other_width = get_signal(other).initial_value.width();
                const auto next_resistance = static_cast<std::uint8_t>(
                    std::min<unsigned>(
                        8, node.resistance + (edge.switch_resistive ? 1 : 0)));
                const auto enqueue = [&](const std::size_t other_bit,
                                         const std::size_t selected_lane) {
                    auto uncertain = node.uncertain;
                    if (edge.switch_control) {
                        const auto& control_signal = get_signal(*edge.switch_control);
                        const auto control_width = control_signal.initial_value.width();
                        const auto lane = edge.switch_width == 0
                            ? std::max(node.bit, other_bit)
                            : selected_lane;
                        const auto control_bit = control_width == 1 ? 0 : lane;
                        if (control_bit >= control_width)
                            return;
                        const auto control = control_signal.initial_value.get(control_bit);
                        const auto active = edge.switch_active_high
                            ? Logic4::one
                            : Logic4::zero;
                        if (control == Logic4::zero || control == Logic4::one) {
                            if (control != active)
                                return;
                        } else {
                            uncertain = true;
                        }
                    }
                    pending.push_back(
                        { other, other_bit, next_resistance, uncertain });
                };
                if (!selected_bits.empty()) {
                    for (const auto& [other_bit, lane] : selected_bits) {
                        enqueue(other_bit, lane);
                    }
                } else if (local.width() == 1 && other_width > 1) {
                    for (std::size_t bit = 0; bit < other_width; ++bit) {
                        enqueue(bit, bit);
                    }
                } else if (other_width == 1 || node.bit < other_width) {
                    enqueue(
                        other_width == 1 ? 0 : node.bit,
                        std::max(node.bit, other_width == 1 ? 0 : node.bit));
                }
            }
        }
        result.set(
            target_bit,
            zero == StrengthRank::highz && one == StrengthRank::highz
                ? Logic4::z
                : zero > one ? Logic4::zero
                : one > zero ? Logic4::one
                             : Logic4::x);
    }
    return result;
}

PackedLogic4& Interpreter::Impl::external_driver_slot(
    const SignalId signal_id)
{
    auto& value = external_driver_values.at(signal_id);
    if (!value) {
        value = std::make_unique<PackedLogic4>(
            initial_driver_value(signal_id));
    }
    return *value;
}

DriveStrength Interpreter::Impl::resolved_signal_strength(
    const SignalId signal_id) const
{
    const auto& signal = get_signal(signal_id);
    auto result = DriveStrength { StrengthRank::highz, StrengthRank::highz };
    const auto include = [&](const Logic4 logic, const DriveStrength strength) {
        if (logic == Logic4::zero || logic == Logic4::x) {
            result.zero = std::max(result.zero, strength.zero);
        }
        if (logic == Logic4::one || logic == Logic4::x) {
            result.one = std::max(result.one, strength.one);
        }
    };
    if (signal.resolution == ResolutionKind::none) {
        const auto& value = driven_values.at(signal_id);
        for (std::size_t bit = 0; bit < value.width(); ++bit) {
            include(value.get(bit), DriveStrength { });
        }
        return result;
    }
    if (signal.implicit_driver) {
        include(*signal.implicit_driver, signal.implicit_drive_strength);
    }
    if (signal.charge_strength && charge_values.at(signal_id)) {
        const auto strength = DriveStrength {
            *signal.charge_strength, *signal.charge_strength
        };
        const auto& value = *charge_values.at(signal_id);
        for (std::size_t bit = 0; bit < value.width(); ++bit) {
            include(value.get(bit), strength);
        }
    }
    for (const auto& [process, value] : driver_values.at(signal_id)) {
        const auto strength = driver_strengths.at(signal_id).at(process);
        const auto effective = apply_driver_force(signal_id, process, value);
        for (std::size_t bit = 0; bit < effective.width(); ++bit) {
            include(effective.get(bit), strength);
        }
    }
    if (external_driver_values.at(signal_id)) {
        const auto& value = *external_driver_values.at(signal_id);
        for (std::size_t bit = 0; bit < value.width(); ++bit) {
            include(value.get(bit), { });
        }
    }
    return result;
}

DriveStrength Interpreter::signal_strength(const SignalId signal) const
{
    return impl_->resolved_signal_strength(signal);
}

bool Interpreter::Impl::switch_process(const ProcessId process) const
{
    return process < processes.size()
        && processes[process].program.switch_bidirectional;
}

void Interpreter::Impl::reset_switch_drivers()
{
    for (SignalId signal = 0; signal < driver_values.size(); ++signal) {
        for (auto& [process, value] : driver_values[signal]) {
            if (switch_process(process)) {
                value = initial_driver_value(signal);
            }
        }
    }
}

void Interpreter::Impl::register_driver(
    const ProcessId process,
    const SignalId signal_id,
    const std::span<const Process::DriverRegion> regions,
    const DriveStrength strength)
{
    const auto& signal = get_signal(signal_id);
    for (const auto& region : regions) {
        if (region.signal != signal_id
            || (!region.whole
                && (region.width == 0
                    || region.offset > signal.initial_value.width()
                    || region.width
                        > signal.initial_value.width() - region.offset))) {
            throw std::invalid_argument(
                "SimIR driver region is outside its signal");
        }
    }
    if (signal.resolution == ResolutionKind::none) {
        return;
    }
    auto initial = initial_driver_value(signal_id);
    if (signal.resolution == ResolutionKind::std_logic
        && !regions.empty()
        && std::ranges::none_of(
            regions, &Process::DriverRegion::whole)) {
        auto selected = PackedLogic4 {
            initial.width(), Logic4::z
        };
        selected.fill(Logic9::z);
        for (const auto& region : regions) {
            selected = insert_value(
                std::move(selected),
                extract_value(initial, region.offset, region.width),
                region.offset);
        }
        initial = std::move(selected);
    }
    auto& values = driver_values.at(signal_id);
    const auto [entry, inserted] = values.try_emplace(
        process, std::move(initial));
    (void)entry;
    if (!inserted) {
        return;
    }
    driver_strengths.at(signal_id).insert_or_assign(
        process, strength);
    refresh_direct_single_driver_route(signal_id);
    auto resolved = resolved_driver_value(signal_id);
    driven_values[signal_id] = resolved;
    signals[signal_id].initial_value = resolved;
    refresh_direct_signal_planes(signal_id);
    signal_last_values[signal_id] = std::move(resolved);
}

void Interpreter::Impl::set_driver(
    const ProcessId process,
    const SignalId signal_id,
    PackedLogic4 value)
{
    const auto& signal = get_signal(signal_id);
    if (signal.initial_value.width() != value.width()) {
        throw std::invalid_argument(
            "SimIR driver assignment width mismatch");
    }
    value = normalize_signal_value(
        signal_id, std::move(value));
    if (const auto source = get_process(process).program.switch_source) {
        auto strength = resolved_signal_strength(*source);
        if (get_process(process).program.switch_resistive) {
            const auto reduce = [](const StrengthRank rank) {
                switch (rank) {
                case StrengthRank::supply:
                case StrengthRank::strong:
                    return StrengthRank::pull;
                case StrengthRank::pull:
                    return StrengthRank::weak;
                case StrengthRank::large:
                case StrengthRank::weak:
                    return StrengthRank::medium;
                case StrengthRank::medium:
                case StrengthRank::small:
                    return StrengthRank::small;
                case StrengthRank::highz:
                    return StrengthRank::highz;
                }
                return StrengthRank::highz;
            };
            strength = DriveStrength {
                reduce(strength.zero), reduce(strength.one)
            };
        }
        driver_strengths.at(signal_id).insert_or_assign(process, strength);
    }
    auto& slot = driver_slot(process, signal_id);
    if (slot == value) {
        return;
    }
    slot = std::move(value);
    if (driver_change_hook) {
        driver_change_hook(process, signal_id, scheduler.now());
    }
}

void Interpreter::Impl::commit_driver(
    const ProcessId process,
    const SignalId signal_id,
    PackedLogic4 value)
{
    if (route_module_path_update(
            process, signal_id, value, std::nullopt)) {
        return;
    }
    if (get_signal(signal_id).resolution
        == ResolutionKind::none) {
        commit(signal_id, std::move(value));
        return;
    }
    set_driver(process, signal_id, std::move(value));
    commit_resolved(signal_id, resolved_driver_value(signal_id));
}

void Interpreter::Impl::commit_resolved(
    const SignalId signal_id,
    PackedLogic4 value)
{
    const auto& signal = get_signal(signal_id);
    auto& handle = charge_decay_handles.at(signal_id);
    if (!signal.charge_strength) {
        commit(signal_id, std::move(value));
        return;
    }

    const auto actively_driven = [&](const std::size_t bit) {
        if (signal.implicit_driver
            && *signal.implicit_driver != Logic4::z) {
            return true;
        }
        const auto contributes = [&](const Logic4 logic,
                                     const DriveStrength strength) {
            if (logic == Logic4::zero) {
                return strength.zero != StrengthRank::highz;
            }
            if (logic == Logic4::one) {
                return strength.one != StrengthRank::highz;
            }
            return logic == Logic4::x
                && (strength.zero != StrengthRank::highz
                    || strength.one != StrengthRank::highz);
        };
        for (const auto& [process, driver] : driver_values.at(signal_id)) {
            if (contributes(
                    driver_force_logic4_at(
                        signal_id, process, driver, bit),
                    driver_strengths.at(signal_id).at(process))) {
                return true;
            }
        }
        return external_driver_values.at(signal_id)
            && contributes(
                external_driver_values.at(signal_id)->get(bit), { });
    };
    bool any_released = false;
    auto& charge = *charge_values.at(signal_id);
    for (std::size_t bit = 0; bit < value.width(); ++bit) {
        if (actively_driven(bit)) {
            charge.set(bit, value.get(bit));
        } else {
            any_released = true;
        }
    }
    if (handle) {
        scheduler.cancel(*handle);
        handle.reset();
    }
    if (!any_released || signal.charge_decay == 0) {
        if (signal.charge_decay == 0) {
            for (std::size_t bit = 0; bit < charge.width(); ++bit) {
                if (!actively_driven(bit))
                    charge.set(bit, Logic4::z);
            }
            value = resolved_driver_value(signal_id);
        }
        commit(signal_id, std::move(value));
        return;
    }

    commit(signal_id, std::move(value));
    if (!signal.charge_decay) {
        return;
    }
    handle = scheduler.schedule_after_cancelable(
        *signal.charge_decay,
        SchedulerPhase::update,
        signal_id,
        [this, signal_id](Scheduler&) {
            charge_decay_handles.at(signal_id).reset();
            auto& decaying_charge = *charge_values.at(signal_id);
            const auto active = [&](const std::size_t bit) {
                for (const auto& [process, driver] :
                    driver_values.at(signal_id)) {
                    const auto logic = driver_force_logic4_at(
                        signal_id, process, driver, bit);
                    const auto strength = driver_strengths.at(signal_id).at(process);
                    if ((logic == Logic4::zero
                            && strength.zero != StrengthRank::highz)
                        || (logic == Logic4::one
                            && strength.one != StrengthRank::highz)
                        || (logic == Logic4::x
                            && (strength.zero != StrengthRank::highz
                                || strength.one != StrengthRank::highz))) {
                        return true;
                    }
                }
                return external_driver_values.at(signal_id)
                    && external_driver_values.at(signal_id)->get(bit)
                    != Logic4::z;
            };
            for (std::size_t bit = 0; bit < decaying_charge.width(); ++bit) {
                if (!active(bit))
                    decaying_charge.set(bit, Logic4::z);
            }
            commit(signal_id, resolved_driver_value(signal_id));
        });
}

[[nodiscard]] PackedLogic4 Interpreter::Impl::underlying_driver_value(
    const ProcessId process,
    const SignalId signal_id) const
{
    if (get_signal(signal_id).resolution
        == ResolutionKind::none) {
        return driven_values.at(signal_id);
    }
    const auto& values = driver_values.at(signal_id);
    const auto found = values.find(process);
    if (found == values.end()) {
        throw std::out_of_range(
            "process has no driver slot for SimIR signal");
    }
    return found->second;
}

[[nodiscard]] PackedLogic4 Interpreter::Impl::current_driver_value(
    const ProcessId process,
    const SignalId signal_id) const
{
    auto value = underlying_driver_value(process, signal_id);
    if (get_signal(signal_id).resolution == ResolutionKind::none) {
        return apply_force(signal_id, std::move(value));
    }
    return apply_driver_force(signal_id, process, std::move(value));
}

void Interpreter::Impl::commit_slice(
    const SignalId signal_id,
    PackedLogic4 value,
    const std::size_t offset)
{
    if (get_signal(signal_id).systemverilog_scalar
        != SystemVerilogScalarKind::None) {
        throw std::invalid_argument {
            "SimIR scalar signals do not support partial assignment"
        };
    }
    commit(
        signal_id,
        insert_value(
            driven_values[signal_id], value, offset));
}

void Interpreter::Impl::commit_driver_slice(
    const ProcessId process,
    const SignalId signal_id,
    PackedLogic4 value,
    const std::size_t offset)
{
    if (route_module_path_update(
            process, signal_id, value, offset)) {
        return;
    }
    if (get_signal(signal_id).resolution
        == ResolutionKind::none) {
        commit_slice(
            signal_id, std::move(value), offset);
        return;
    }
    const auto updated = insert_value(
        driver_slot(process, signal_id),
        value,
        offset);
    commit_driver(process, signal_id, std::move(updated));
}

void Interpreter::Impl::schedule_update_commit()
{
    if (native_update_profile_enabled) {
        ++native_update_profile_schedule_requests;
    }
    if (update_commit_scheduled) {
        if (native_update_profile_enabled) {
            ++native_update_profile_schedule_coalesced;
        }
        return;
    }
    update_commit_scheduled = true;
    scheduler.schedule(
        SchedulerPhase::update,
        std::numeric_limits<StableOrder>::max(),
        [this](Scheduler&) {
            if (native_update_profile_enabled) {
                ++native_update_profile_commits;
            }
            if (update_profile_enabled) {
                ++update_profile_commits;
            }
            bool has_non_switch_update { };
            if (has_bidirectional_switches) {
                has_non_switch_update = std::ranges::any_of(
                    pending_updates, [&](const PendingUpdate& update) {
                        return !update.driver_present()
                            || !switch_process(update.driver);
                    });
            }
            if (has_non_switch_update) {
                reset_switch_drivers();
                std::erase_if(
                    pending_updates, [&](const PendingUpdate& update) {
                        return update.driver_present()
                            && switch_process(update.driver);
                    });
            }
            if (unresolved_update_scratch.size() < signals.size()) {
                unresolved_update_scratch.resize(signals.size());
                driver_update_scratch.resize(signals.size());
                resolved_update_marked.resize(signals.size());
            }
            if (direct_single_driver_commit_marked.size() < signals.size()) {
                direct_single_driver_commit_marked.resize(signals.size());
            }
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
                            && !signal.implicit_driver
                            && !signal.charge_strength) {
                            ++update_profile_resolved_single_driver;
                        }
                    }
                }
                const auto driver = pending.driver_present()
                    ? std::optional<ProcessId> { pending.driver }
                    : std::nullopt;
                if (has_bidirectional_switches
                    && driver && switch_process(*driver)) {
                    const auto& connection = get_process(*driver).program;
                    if (connection.switch_source) {
                        mark_resolved(*connection.switch_source);
                    }
                    if (connection.switch_target) {
                        mark_resolved(*connection.switch_target);
                    }
                    continue;
                }
                PackedLogic4* destination { };
                if (get_signal(pending.signal).resolution
                    == ResolutionKind::none) {
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
                    const auto& direct_route
                        = direct_single_driver_routes[pending.signal];
                    const bool direct_single_driver
                        = driver && direct_route.value != nullptr
                        && direct_route.process == *driver
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
                            staged.emplace(*direct_route.value);
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
                            staged.push_back(PendingDriverCommit {
                                driver,
                                driver
                                    ? driver_slot(
                                          *driver, pending.signal)
                                    : external_driver_slot(
                                          pending.signal) });
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
            if (has_non_switch_update) {
                for (const auto& state : processes) {
                    if (!state.program.switch_bidirectional)
                        continue;
                    mark_resolved(*state.program.switch_source);
                    mark_resolved(*state.program.switch_target);
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
                staged.active = 0U;
                const auto& route = direct_single_driver_routes[signal];
                if (route.value == nullptr || route.process != process
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
                    publish_native_word(signal, value);
                    return;
                }
                materialize_direct_signal(signal);
                const bool driver_changed
                    = !route.value->matches_word(value, 0U);
                if (driver_changed) {
                    route.value->assign_word(value);
                    if (driver_change_hook) {
                        driver_change_hook(process, signal, scheduler.now());
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
                publish_normalized_word(signal, value);
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

            for (std::uint32_t index = 0U;
                index < native_logic9_word_update_count; ++index) {
                const auto signal
                    = native_logic9_word_update_signals[index];
                auto& staged
                    = direct_single_driver_logic9_word_scratch[signal];
                const auto process = staged.process;
                const auto value = Logic9Word {
                    staged.width, staged.planes
                };
                const auto written_mask = staged.mask;
                staged.active = 0U;
                staged.mask = 0U;
                const auto& route = direct_single_driver_routes[signal];
                if (!can_publish_native_logic9_word(signal, process)
                    || route.value == nullptr
                    || route.process != process) {
                    auto driver = route.value != nullptr
                            && route.process == process
                        ? route.value
                        : [&]() -> PackedLogic4* {
                              const auto found
                                  = driver_values[signal].find(process);
                              return found == driver_values[signal].end()
                                  ? nullptr
                                  : &found->second;
                          }();
                    auto merged = value;
                    if (driver != nullptr && written_mask != 0U) {
                        const auto previous = driver->logic9_low_word();
                        for (std::size_t plane = 0U;
                            plane < merged.planes.size(); ++plane) {
                            merged.planes[plane]
                                = (previous.planes[plane] & ~written_mask)
                                | (merged.planes[plane] & written_mask);
                        }
                    }
                    set_driver(
                        process, signal,
                        PackedLogic4::from_logic9_word(merged));
                    mark_resolved(signal);
                    continue;
                }
                publish_native_logic9_word(signal, value);
            }
            native_logic9_word_update_count = 0U;

            for (const auto signal : direct_single_driver_update_signals) {
                auto& staged = unresolved_update_scratch[signal];
                auto& values = driver_values[signal];
                if (!staged || values.size() != 1U) {
                    throw std::logic_error {
                        "direct single-driver update lost its staged value"
                    };
                }
                const auto process = values.begin()->first;
                if (values.begin()->second != *staged) {
                    values.begin()->second = *staged;
                    if (driver_change_hook) {
                        driver_change_hook(process, signal, scheduler.now());
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
            for (const auto signal : unresolved_update_signals) {
                auto& staged = unresolved_update_scratch[signal];
                update_commit_scratch.emplace_back(
                    signal, std::move(*staged));
                staged.reset();
            }
            unresolved_update_signals.clear();
            for (const auto signal : resolved_update_signals) {
                update_commit_scratch.emplace_back(
                    signal, resolved_driver_value(signal));
                resolved_update_marked[signal] = false;
            }
            resolved_update_signals.clear();
            std::sort(
                update_commit_scratch.begin(),
                update_commit_scratch.end(),
                [](const auto& lhs, const auto& rhs) {
                    return lhs.first < rhs.first;
                });
            if (native_update_profile_enabled) {
                native_update_profile_commit_value_signals
                    += update_commit_scratch.size();
            }
            for (auto& [signal, value] : update_commit_scratch) {
                if (direct_single_driver_commit_marked[signal]) {
                    direct_single_driver_commit_marked[signal] = false;
                    commit_direct_single_driver(signal, std::move(value));
                } else if (get_signal(signal).resolution
                    == ResolutionKind::none) {
                    commit(signal, std::move(value));
                } else {
                    commit_resolved(signal, std::move(value));
                }
            }
        });
}

void Interpreter::Impl::stage_update(
    const std::optional<ProcessId> driver,
    SignalId signal_id,
    PackedLogic4 staged_value)
{
    (void)get_signal(signal_id);
    if (driven_values[signal_id].width() != staged_value.width()) {
        throw std::invalid_argument("SimIR signal assignment width mismatch");
    }
    staged_value = normalize_signal_value(
        signal_id, std::move(staged_value));
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
    PackedLogic4 staged_value)
{
    stage_update(
        std::optional<ProcessId> { process },
        signal_id,
        std::move(staged_value));
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
        processes[process].profile_updates += updates.size();
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
            pending_updates.push_back(PendingUpdate {
                update.signal,
                process,
                offset,
                { },
                value_index });
        } else {
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
    static const bool disable_direct_word_commit
        = std::getenv("FSIM_DISABLE_DIRECT_WORD_COMMIT") != nullptr;
    if (native_update_profile_enabled) {
        ++native_update_profile_word_calls;
        native_update_profile_words += updates.size();
    }
    if (!module_paths.empty() || has_bidirectional_switches) {
        if (native_update_profile_enabled) {
            ++native_update_profile_word_fallbacks;
        }
        stage_update_words(process, updates);
        return;
    }
    if (process_profile_enabled || update_profile_enabled) {
        processes[process].profile_updates += updates.size();
    }
    if (unresolved_update_scratch.size() < signals.size()) {
        unresolved_update_scratch.resize(signals.size());
        driver_update_scratch.resize(signals.size());
        resolved_update_marked.resize(signals.size());
    }
    pending_updates.reserve(pending_updates.size() + updates.size());
    bool staged_any { };
    for (const auto& update : updates) {
        const auto& signal = signals[update.signal];
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
                    && !signal.implicit_driver
                    && !signal.charge_strength) {
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

        const auto& direct_route = direct_single_driver_routes[update.signal];
        const bool direct_single_driver
            = direct_route.value != nullptr
            && direct_route.process == process
            && !external_driver_values[update.signal]
            && !forced_driver_values[update.signal];
        if (!disable_direct_word_commit && direct_single_driver
            && direct_route.value->width() <= 64U) {
            auto& staged = direct_single_driver_word_scratch[update.signal];
            const auto direct_width = static_cast<std::uint32_t>(
                direct_route.value->width());
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
            if ((((current.aval ^ aval) | (current.bval ^ bval)) & mask)
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
            auto& staged = unresolved_update_scratch[update.signal];
            const auto& current = staged
                ? *staged
                : *direct_route.value;
            if (current.matches_word(
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
                staged.emplace(driver_values[update.signal].begin()->second);
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
                if ((((direct_signal_aval[update.signal] ^ aval)
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
            if (current.matches_word(
                    update.value, offset.value_or(0U))) {
                if (native_update_profile_enabled) {
                    ++native_update_profile_word_unchanged;
                }
                continue;
            }
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
                if (current.matches_word(
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
            } else if (found->value.matches_word(
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

bool Interpreter::Impl::stage_validated_update_slot_batches(
    const std::span<const ProcessUpdateSlotBatch> batches)
{
    static const bool disable_direct_word_commit
        = std::getenv("FSIM_DISABLE_DIRECT_WORD_COMMIT") != nullptr;
    if (native_update_profile_enabled) {
        ++native_update_profile_calls;
    }
    // Module paths and switch networks may redirect or synthesize writes, and
    // profiling owns per-update counts. Leave those configurations on the
    // ordinary checked path without consuming any slot state.
    if (!module_paths.empty() || has_bidirectional_switches
        || process_profile_enabled || update_profile_enabled) {
        if (native_update_profile_enabled) {
            ++native_update_profile_fallbacks;
        }
        return false;
    }

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
        driver_update_scratch.resize(signals.size());
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
    if (native_update_profile_enabled) {
        native_update_profile_batches += batches.size();
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
            const auto& direct_route = direct_single_driver_routes[signal];
            const bool direct_single_driver
                = direct_route.value != nullptr
                && direct_route.process == batch.process
                && !external_driver_values[signal]
                && !forced_driver_values[signal];
            if (!disable_direct_word_commit && direct_single_driver
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
                if ((((current.aval ^ slot.aval[0])
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
                auto& staged = unresolved_update_scratch[signal];
                const auto& current = staged ? *staged : *direct_route.value;
                if (slot_matches(current, slot)) {
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
                        staged.emplace(*direct_route.value);
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
                    if ((((direct_signal_aval[signal] ^ slot.aval[0])
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
                if (slot_matches(current, slot)) {
                    if (native_update_profile_enabled) {
                        ++native_update_profile_unchanged;
                        ++native_update_profile_unchanged_unresolved;
                    }
                    consume_slot(slot);
                    return;
                }
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

            auto& staged = driver_update_scratch[signal];
            auto found = std::ranges::find(
                staged,
                std::optional<ProcessId> { batch.process },
                &PendingDriverCommit::driver);
            if (found == staged.end()) {
                const auto& current = driver_slot(batch.process, signal);
                if (slot_matches(current, slot)) {
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
                if (slot_matches(found->value, slot)) {
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
    if (staged_any) {
        schedule_update_commit();
    }
    return true;
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
        bool enabled {
            std::getenv("FSIM_PROFILE_LOGIC9_BATCH") != nullptr
        };
        std::uint64_t calls { };
        std::uint64_t active_slots { };
        std::uint64_t rejected_runtime { };
        std::uint64_t rejected_slot { };
        std::uint64_t accepted { };
        std::uint64_t unchanged_current { };
        std::uint64_t unchanged_staged { };
        std::uint64_t changed_slots { };

        ~Logic9BatchProfile()
        {
            if (enabled) {
                std::fprintf(
                    stderr,
                    "FSIM-LOGIC9-BATCH calls=%llu active_slots=%llu "
                    "rejected_runtime=%llu rejected_slot=%llu accepted=%llu "
                    "unchanged_current=%llu unchanged_staged=%llu "
                    "changed_slots=%llu\n",
                    static_cast<unsigned long long>(calls),
                    static_cast<unsigned long long>(active_slots),
                    static_cast<unsigned long long>(rejected_runtime),
                    static_cast<unsigned long long>(rejected_slot),
                    static_cast<unsigned long long>(accepted),
                    static_cast<unsigned long long>(unchanged_current),
                    static_cast<unsigned long long>(unchanged_staged),
                    static_cast<unsigned long long>(changed_slots));
            }
        }
    };
    static Logic9BatchProfile profile;
    if (profile.enabled) {
        profile.calls += batches.size();
    }
    if (module_paths.size() != 0U || has_bidirectional_switches
        || process_profile_enabled || update_profile_enabled) {
        if (profile.enabled) {
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
        if (profile.enabled) {
            ++profile.active_slots;
        }
        if (slot.planes == nullptr || slot.signal >= signals.size()
            || slot.signal >= direct_single_driver_routes.size()
            || slot.width == 0U || slot.width > 64U
            || signals[slot.signal].value_kind != ValueKind::logic9
            || signals[slot.signal].initial_value.width() != slot.width
            || direct_single_driver_routes[slot.signal].value == nullptr
            || direct_single_driver_routes[slot.signal].process
                != batch.process) {
            consumed_all = false;
            if (profile.enabled) {
                ++profile.rejected_slot;
            }
            continue;
        }
        const auto full_mask = slot.width == 64U
            ? std::numeric_limits<std::uint64_t>::max()
            : (UINT64_C(1) << slot.width) - UINT64_C(1);
        const auto mask = *slot.mask & full_mask;
        if (mask == 0U) {
            *slot.mask = 0U;
            continue;
        }
        auto& staged = direct_single_driver_logic9_word_scratch[slot.signal];
        Logic9Word current;
        if (staged.active != 0U) {
            current = Logic9Word { staged.width, staged.planes };
        } else {
            current = Logic9Word {
                slot.width,
                { direct_signal_logic9_plane0[slot.signal],
                    direct_signal_logic9_plane1[slot.signal],
                    direct_signal_logic9_plane2[slot.signal],
                    direct_signal_logic9_plane3[slot.signal] }
            };
        }
        const auto value = Logic9Word {
            slot.width,
            { slot.planes[0], slot.planes[1],
                slot.planes[2], slot.planes[3] }
        };
        std::uint64_t changed { };
        for (std::size_t plane = 0; plane < staged.planes.size(); ++plane) {
            changed |= current.planes[plane] ^ value.planes[plane];
        }
        if ((changed & mask) == 0U && staged.active == 0U) {
            if (profile.enabled) {
                ++profile.unchanged_current;
            }
            *slot.mask = 0U;
            continue;
        }
        if ((changed & mask) == 0U) {
            if (profile.enabled) {
                ++profile.unchanged_staged;
            }
            *slot.mask = 0U;
            continue;
        }
        if (staged.active == 0U) {
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
            staged.planes = current.planes;
            staged.mask = 0U;
            staged.active = 1U;
        }
        for (std::size_t plane = 0; plane < staged.planes.size(); ++plane) {
            staged.planes[plane] = (staged.planes[plane] & ~mask)
                | (value.planes[plane] & mask);
        }
        staged.mask |= mask;
        staged_any = true;
        if (profile.enabled) {
            ++profile.changed_slots;
        }
        *slot.mask = 0U;
      }
      if (profile.enabled) {
          ++profile.accepted;
      }
    }
    if (staged_any) {
        schedule_update_commit();
    }
    return consumed_all;
}

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
