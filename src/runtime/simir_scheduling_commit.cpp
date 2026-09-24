// SPDX-License-Identifier: Apache-2.0

#include "simir_internal.hpp"

#include <algorithm>
#include <bit>
#include <optional>

namespace fsim::runtime::simir {
namespace {

[[nodiscard]] std::uint64_t word_mask(const std::size_t width) noexcept
{
    return width == 64U
        ? ~std::uint64_t { 0 }
        : (std::uint64_t { 1 } << width) - 1U;
}

void update_force_mask(
    PackedLogic4& mask,
    const std::size_t offset,
    const std::size_t width,
    const bool forced)
{
    if (mask.is_logic9()) {
        for (std::size_t bit = 0; bit < width; ++bit) {
            mask.set(offset + bit, forced ? Logic4::one : Logic4::zero);
        }
        return;
    }
    auto updated = std::size_t { 0 };
    while (updated < width) {
        const auto chunk_width = std::min<std::size_t>(
            64U, width - updated);
        const auto source = runtime::Logic4Word {
            chunk_width,
            forced ? word_mask(chunk_width) : std::uint64_t { 0 },
            0
        };
        mask.insert_word(source, offset + updated);
        updated += chunk_width;
    }
}

[[nodiscard]] bool has_forced_bits(const PackedLogic4& mask)
{
    if (!mask.is_logic9()) {
        const auto aval = mask.aval_words();
        const auto bval = mask.bval_words();
        for (std::size_t index = 0; index < aval.size(); ++index) {
            if ((aval[index] & ~bval[index]) != 0U) {
                return true;
            }
        }
        return false;
    }
    for (std::size_t bit = 0; bit < mask.width(); ++bit) {
        if (mask.get(bit) == Logic4::one) {
            return true;
        }
    }
    return false;
}

void insert_force_value(
    PackedLogic4& target,
    const PackedLogic4& value,
    const std::size_t offset)
{
    if (!target.is_logic9() && !value.is_logic9()
        && value.width() > 0U && value.width() <= 64U) {
        target.insert_word(value.unchecked_low_word(), offset);
        return;
    }
    target = insert_value(std::move(target), value, offset);
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
        const auto& members = event_identity_members.at(*identity);
        for (const auto member : members) {
            const bool stored_changed = driven_values[member] != value;
            driven_values[member] = value;
            if (stored_changed && stored_signal_change_hook) {
                stored_signal_change_hook(member, scheduler.now());
            }
            publish_normalized(member, apply_force(member, value), false);
            mark_switch_network_dirty(member);
        }
        auto transition = StaticTransitionMatches { };
        if (old_value.width() == 1U && value.width() == 1U) {
            transition = decode_static_transition(
                old_value.get(0), value.get(0));
        }
        for (const auto member : members) {
            for (const auto& sensitivity : static_fanout_for(member)) {
                auto& process = get_process(sensitivity.process);
                process.static_trigger_mask
                    |= sensitivity.static_trigger_mask;
                if (!process.waiting_on_static) {
                    continue;
                }
                if (sensitivity.edge != EdgeKind::any
                    && sensitivity.edge != EdgeKind::transaction
                    && (old_value.width() != 1U
                        || !transition.matches(sensitivity.edge))) {
                    continue;
                }
                queue_static_next_delta(sensitivity.process);
            }
        }
        const auto dynamic = dynamic_fanout.at(*identity);
        for (const auto sensitivity : dynamic) {
            auto& process = get_process(sensitivity.process);
            if (dynamic_wait_satisfied(
                    process, *identity, sensitivity)) {
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
    mark_switch_network_dirty(signal_id);
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
    if (direct_single_driver_record(signal_id) == nullptr) {
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
    mark_switch_network_dirty(signal_id);
    refresh_switch_network();
}

void Interpreter::Impl::publish_container_signal_aliases(
    const SignalId signal_id)
{
    for (const auto object : signal_container_aliases.at(signal_id)) {
        for (const auto process : container_dynamic_fanout.at(object)) {
            queue_next_delta(process);
        }
        if (container_object_change_hook) {
            container_object_change_hook(object, scheduler.now());
        }
    }
}

void Interpreter::Impl::invalidate_switch_components()
{
    switch_components_built = false;
    switch_components.clear();
    switch_component_by_signal.clear();
    switch_component_by_connection.clear();
    dirty_switch_components.clear();
}

void Interpreter::Impl::build_switch_components()
{
    if (switch_components_built) {
        return;
    }
    switch_components.clear();
    switch_component_by_signal.assign(
        signals.size(), no_switch_component);
    switch_component_by_connection.assign(
        switch_connections.size(), no_switch_component);
    dirty_switch_components.clear();
    if (switch_connections.empty()) {
        switch_components_built = true;
        return;
    }

    std::vector<std::size_t> parents(signals.size());
    std::vector<std::uint8_t> ranks(signals.size(), 0U);
    for (std::size_t index = 0U; index < parents.size(); ++index) {
        parents[index] = index;
    }
    const auto find_root = [&parents](std::size_t signal) {
        while (parents[signal] != signal) {
            parents[signal] = parents[parents[signal]];
            signal = parents[signal];
        }
        return signal;
    };
    for (const auto& connection : switch_connections) {
        auto source = find_root(connection.source);
        auto target = find_root(connection.target);
        if (source == target) {
            continue;
        }
        if (ranks[source] < ranks[target]) {
            std::swap(source, target);
        }
        parents[target] = source;
        if (ranks[source] == ranks[target]) {
            ++ranks[source];
        }
    }

    std::vector<std::size_t> component_by_root(
        signals.size(), no_switch_component);
    for (SignalId signal = 0U; signal < signals.size(); ++signal) {
        if (signal >= switch_endpoint_adjacency.size()
            || switch_endpoint_adjacency[signal].empty()) {
            continue;
        }
        const auto root = find_root(signal);
        auto& component = component_by_root[root];
        if (component == no_switch_component) {
            component = switch_components.size();
            switch_components.emplace_back();
        }
        switch_component_by_signal[signal] = component;
        switch_components[component].signals.push_back(signal);
    }
    for (std::size_t index = 0U;
        index < switch_connections.size(); ++index) {
        const auto signal = switch_connections[index].source;
        const auto component = switch_component_by_signal[signal];
        if (component == no_switch_component) {
            continue;
        }
        switch_component_by_connection[index] = component;
    }
    for (std::size_t index = 0U;
        index < switch_components.size(); ++index) {
        switch_components[index].dirty = true;
        dirty_switch_components.push_back(index);
    }
    switch_components_built = true;
}

void Interpreter::Impl::mark_switch_network_dirty(
    const SignalId signal_id,
    const bool non_switch_update)
{
    if (!has_bidirectional_switches
        || signal_id >= switch_endpoint_adjacency.size()) {
        return;
    }
    build_switch_components();
    const auto mark_component = [&](const std::size_t component) {
        if (component == no_switch_component
            || component >= switch_components.size()) {
            return;
        }
        auto& state = switch_components[component];
        if (!state.dirty) {
            state.dirty = true;
            dirty_switch_components.push_back(component);
        }
        state.has_non_switch_update
            = state.has_non_switch_update || non_switch_update;
    };
    mark_component(switch_component_by_signal[signal_id]);
    if (signal_id < switch_control_adjacency.size()) {
        for (const auto connection : switch_control_adjacency[signal_id]) {
            if (connection < switch_component_by_connection.size()) {
                mark_component(
                    switch_component_by_connection[connection]);
            }
        }
    }
}

void Interpreter::Impl::refresh_switch_network()
{
    if (!has_bidirectional_switches || switch_refreshing) {
        return;
    }
    build_switch_components();
    if (dirty_switch_components.empty()) {
        return;
    }

    std::vector<SignalId> endpoints;
    for (const auto component : dirty_switch_components) {
        for (const auto signal : switch_components[component].signals) {
            endpoints.push_back(signal);
        }
    }
    std::ranges::sort(endpoints);
    endpoints.erase(
        std::unique(endpoints.begin(), endpoints.end()), endpoints.end());
    std::vector<std::pair<SignalId, PackedLogic4>> values;
    values.reserve(endpoints.size());
    for (const auto signal : endpoints) {
        values.emplace_back(signal, resolved_driver_value(signal));
    }

    const auto refreshing_components
        = std::move(dirty_switch_components);
    dirty_switch_components.clear();
    for (const auto component : refreshing_components) {
        switch_components[component].dirty = false;
        switch_components[component].has_non_switch_update = false;
    }
    switch_refreshing = true;
    try {
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
    } catch (...) {
        switch_refreshing = false;
        for (const auto component : refreshing_components) {
            auto& state = switch_components[component];
            if (!state.dirty) {
                state.dirty = true;
                dirty_switch_components.push_back(component);
            }
        }
        throw;
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
    if (!value.is_logic9() && !forced.is_logic9() && !mask.is_logic9()
        && value.width() > 0U && value.width() <= 64U
        && forced.width() == value.width()
        && mask.width() == value.width()) {
        const auto mask_word = mask.unchecked_low_word();
        value.assign_word(runtime::apply_force_word(
            value.unchecked_low_word(), forced.unchecked_low_word(),
            mask_word.aval & ~mask_word.bval));
        return value;
    }
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
    insert_force_value(*forced_values[signal_id], value, offset);
    update_force_mask(*forced_masks[signal_id], offset, value.width(), true);
    refresh_direct_single_driver_route(signal_id);
    publish(
        signal_id,
        apply_force(signal_id, driven_values[signal_id]));
    mark_switch_network_dirty(signal_id);
    refresh_switch_network();
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
    update_force_mask(*forced_masks[signal_id], offset, width, false);
    const bool any_forced = has_forced_bits(*forced_masks[signal_id]);
    if (!any_forced) {
        forced_values[signal_id].reset();
        forced_masks[signal_id].reset();
    }
    refresh_direct_single_driver_route(signal_id);
    publish(
        signal_id,
        apply_force(signal_id, driven_values[signal_id]));
    mark_switch_network_dirty(signal_id);
    refresh_switch_network();
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
    if (!value.is_logic9() && !forced->second.is_logic9()
        && !mask->second.is_logic9()
        && value.width() > 0U && value.width() <= 64U
        && forced->second.width() == value.width()
        && mask->second.width() == value.width()) {
        const auto mask_word = mask->second.unchecked_low_word();
        value.assign_word(runtime::apply_force_word(
            value.unchecked_low_word(), forced->second.unchecked_low_word(),
            mask_word.aval & ~mask_word.bval));
        return value;
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
    insert_force_value(forced_entry->second, value, offset);
    auto& mask = masks->at(process);
    update_force_mask(mask, offset, value.width(), true);
    refresh_direct_single_driver_route(signal_id);
    auto resolved = resolved_driver_value(signal_id);
    driven_values[signal_id] = resolved;
    publish(signal_id, apply_force(signal_id, std::move(resolved)));
    mark_switch_network_dirty(signal_id);
    refresh_switch_network();
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
    update_force_mask(mask_entry->second, offset, width, false);
    const bool any_forced = has_forced_bits(mask_entry->second);
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
    mark_switch_network_dirty(signal_id);
    refresh_switch_network();
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
    if (auto* record = values.find(process)) {
        return record->value;
    }
    const bool inserted = values.insert_if_absent(DriverRecord {
        process,
        initial_driver_value(signal_id),
        get_process(process).program().drive_strength
    });
    if (inserted) {
        refresh_direct_single_driver_route(signal_id);
    }
    auto* record = values.find(process);
    if (record == nullptr) {
        throw std::logic_error {
            "driver slot insertion did not retain its SimIR process"
        };
    }
    return record->value;
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
    const bool switch_adjacency_unknown = has_bidirectional_switches
        && (signal_id >= switch_endpoint_adjacency.size()
            || signal_id >= switch_control_adjacency.size());
    const bool switch_connected = has_bidirectional_switches
        && !switch_adjacency_unknown
        && (!switch_endpoint_adjacency[signal_id].empty()
            || !switch_control_adjacency[signal_id].empty());
    if (switch_adjacency_unknown || switch_connected
        || !identity_single_driver_resolution
        || values.size() != 1U
        || signal.has_implicit_driver
        || signal.has_charge_strength
        || external_driver_values.at(signal_id)
        || forced_values.at(signal_id)
        || forced_driver_values.at(signal_id)) {
        return;
    }
    const auto* record = values.sole();
    if (record == nullptr || record->strength != DriveStrength { }) {
        return;
    }
    route.process = record->process;
    route.active = true;
    direct_single_driver_processes[signal_id] = record->process;
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
    const auto* sole_record = values.sole();
    if (identity_single_driver_resolution
        && sole_record != nullptr
        && !external_driver_values.at(signal_id)
        && !signal.has_implicit_driver
        && !signal.has_charge_strength
        && sole_record->strength == DriveStrength { }) {
        return apply_driver_force(
            signal_id, sole_record->process, sole_record->value);
    }
    const auto resolution = signal.resolution;
    const auto try_resolve_narrow_logic4 =
        [&](const bool require_default_strength)
        -> std::optional<PackedLogic4> {
        const auto width = signal.initial_value.width();
        if (signal.value_kind != ValueKind::logic4
            || width == 0U || width > 64U) {
            return std::nullopt;
        }
        auto accumulator = runtime::Logic4ResolutionAccumulator { width };
        bool incompatible_driver { };
        values.for_each_in_process_order(
            [&](const DriverRecord& record) {
                if (incompatible_driver) {
                    return;
                }
                if (record.value.is_logic9()
                    || record.value.width() != width
                    || (require_default_strength
                        && record.strength != DriveStrength { })) {
                    incompatible_driver = true;
                    return;
                }
                const auto effective = apply_driver_force(
                    signal_id, record.process, record.value);
                if (effective.is_logic9() || effective.width() != width) {
                    incompatible_driver = true;
                    return;
                }
                accumulator.add(effective.unchecked_low_word());
            });
        if (incompatible_driver) {
            return std::nullopt;
        }
        if (external_driver_values.at(signal_id)) {
            const auto& external = *external_driver_values.at(signal_id);
            if (external.is_logic9() || external.width() != width) {
                return std::nullopt;
            }
            accumulator.add(external.unchecked_low_word());
        }
        const auto resolved = accumulator.result();
        return PackedLogic4::from_aval_bval(
            width, resolved.aval, resolved.bval);
    };
    if (resolution == ResolutionKind::sv_wire
        && !signal.has_implicit_driver && !signal.has_charge_strength) {
        if (auto resolved = try_resolve_narrow_logic4(true)) {
            return std::move(*resolved);
        }
    } else if (resolution == ResolutionKind::none
        || resolution == ResolutionKind::std_logic) {
        if (auto resolved = try_resolve_narrow_logic4(false)) {
            return std::move(*resolved);
        }
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
    values.for_each_in_process_order([&](const DriverRecord& record) {
        drivers.push_back(apply_driver_force(
            signal_id, record.process, record.value));
        strength_drivers.push_back({ &drivers.back(), record.strength });
    });
    if (external_driver_values.at(signal_id)) {
        drivers.push_back(
            *external_driver_values.at(signal_id));
        strength_drivers.push_back(
            { &*external_driver_values.at(signal_id), { } });
    }
    if (resolution == ResolutionKind::sv_user_first) {
        return drivers.front();
    }
    if (resolution == ResolutionKind::sv_wire) {
        const auto equal_strength_logic4 = !signal.has_implicit_driver
            && !signal.has_charge_strength
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
        const auto& cold = get_signal_cold(signal_id);
        auto result = PackedLogic4 {
            signal.initial_value.width(), Logic4::z
        };
        const auto rank = [](const StrengthRank strength) {
            return static_cast<std::underlying_type_t<StrengthRank>>(strength);
        };
        for (std::size_t bit = 0; bit < result.width(); ++bit) {
            auto zero = StrengthRank::highz;
            auto one = StrengthRank::highz;
            if (cold.implicit_driver == Logic4::zero) {
                zero = cold.implicit_drive_strength.zero;
            } else if (cold.implicit_driver == Logic4::one) {
                one = cold.implicit_drive_strength.one;
            } else if (cold.implicit_driver == Logic4::x) {
                zero = cold.implicit_drive_strength.zero;
                one = cold.implicit_drive_strength.one;
            }
            if (cold.charge_strength
                && charge_values.at(signal_id)) {
                const auto charge = charge_values.at(signal_id)->get(bit);
                if (charge == Logic4::zero || charge == Logic4::x) {
                    zero = std::max(zero, *cold.charge_strength);
                }
                if (charge == Logic4::one || charge == Logic4::x) {
                    one = std::max(one, *cold.charge_strength);
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
        || get_signal(signal_id).resolution != ResolutionKind::sv_wire
        || signal_id >= switch_endpoint_adjacency.size()
        || switch_endpoint_adjacency[signal_id].empty()) {
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
            for (const auto connection_index
                : switch_endpoint_adjacency[node.signal]) {
                const auto& edge = switch_connections[connection_index];
                SignalId other { };
                std::vector<std::pair<std::size_t, std::size_t>>
                    selected_bits;
                if (edge.width != 0) {
                    const auto width = static_cast<std::size_t>(
                        edge.width);
                    const auto source_offset = static_cast<std::size_t>(
                        edge.source_offset);
                    const auto target_offset = static_cast<std::size_t>(
                        edge.target_offset);
                    if (edge.source == node.signal
                        && node.bit >= source_offset
                        && node.bit - source_offset < width) {
                        const auto lane = node.bit - source_offset;
                        other = edge.target;
                        selected_bits.emplace_back(
                            target_offset + lane, lane);
                    }
                    if (edge.target == node.signal
                        && node.bit >= target_offset
                        && node.bit - target_offset < width) {
                        const auto lane = node.bit - target_offset;
                        other = edge.source;
                        selected_bits.emplace_back(
                            source_offset + lane, lane);
                    }
                    if (selected_bits.empty())
                        continue;
                } else if (edge.source == node.signal) {
                    other = edge.target;
                } else if (edge.target == node.signal) {
                    other = edge.source;
                } else {
                    continue;
                }
                const auto other_width = get_signal(other).initial_value.width();
                const auto next_resistance = static_cast<std::uint8_t>(
                    std::min<unsigned>(
                        8, node.resistance + (edge.resistive ? 1 : 0)));
                const auto enqueue = [&](const std::size_t other_bit,
                                         const std::size_t selected_lane) {
                    auto uncertain = node.uncertain;
                    if (edge.control) {
                        const auto& control_signal = get_signal(*edge.control);
                        const auto control_width = control_signal.initial_value.width();
                        const auto lane = edge.width == 0
                            ? std::max(node.bit, other_bit)
                            : selected_lane;
                        const auto control_bit = control_width == 1 ? 0 : lane;
                        if (control_bit >= control_width)
                            return;
                        const auto control = control_signal.initial_value.get(control_bit);
                        const auto active = edge.active_high
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
    const auto& cold = get_signal_cold(signal_id);
    if (cold.implicit_driver) {
        include(*cold.implicit_driver, cold.implicit_drive_strength);
    }
    if (cold.charge_strength && charge_values.at(signal_id)) {
        const auto strength = DriveStrength {
            *cold.charge_strength, *cold.charge_strength
        };
        const auto& value = *charge_values.at(signal_id);
        for (std::size_t bit = 0; bit < value.width(); ++bit) {
            include(value.get(bit), strength);
        }
    }
    driver_values.at(signal_id).for_each_in_process_order(
        [&](const DriverRecord& record) {
            const auto effective = apply_driver_force(
                signal_id, record.process, record.value);
            for (std::size_t bit = 0; bit < effective.width(); ++bit) {
                include(effective.get(bit), record.strength);
            }
        });
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
        && processes[process].program().switch_bidirectional;
}

void Interpreter::Impl::reset_switch_drivers(
    const std::size_t component)
{
    if (component >= switch_components.size()) {
        return;
    }
    for (const auto signal : switch_components[component].signals) {
        driver_values[signal].for_each_in_process_order(
            [&](DriverRecord& record) {
                if (switch_process(record.process)) {
                    record.value = initial_driver_value(signal);
                }
            });
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
    const bool inserted = values.insert_if_absent(DriverRecord {
        process, std::move(initial), strength
    });
    if (!inserted) {
        return;
    }
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
    const bool driver_already_registered
        = driver_values.at(signal_id).find(process) != nullptr;
    std::optional<DriveStrength> switch_strength;
    if (const auto source = get_process(process).program().switch_source) {
        auto strength = resolved_signal_strength(*source);
        if (get_process(process).program().switch_resistive) {
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
        switch_strength = strength;
    }
    auto& slot = driver_slot(process, signal_id);
    if (switch_strength && driver_already_registered) {
        auto* record = driver_values.at(signal_id).find(process);
        if (record != nullptr && record->strength != *switch_strength) {
            record->strength = *switch_strength;
            refresh_direct_single_driver_route(signal_id);
        }
    }
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
    if (!signal.has_charge_strength) {
        commit(signal_id, std::move(value));
        return;
    }
    const auto& cold = get_signal_cold(signal_id);

    const auto actively_driven = [&](const std::size_t bit) {
        if (cold.implicit_driver
            && *cold.implicit_driver != Logic4::z) {
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
        bool active_driver { };
        driver_values.at(signal_id).for_each_in_process_order(
            [&](const DriverRecord& record) {
                if (!active_driver && contributes(
                        driver_force_logic4_at(
                            signal_id, record.process, record.value, bit),
                        record.strength)) {
                    active_driver = true;
                }
            });
        if (active_driver) {
            return true;
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
    if (!any_released || cold.charge_decay == 0) {
        if (cold.charge_decay == 0) {
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
    if (!cold.charge_decay) {
        return;
    }
    handle = scheduler.schedule_after_cancelable(
        *cold.charge_decay,
        SchedulerPhase::update,
        signal_id,
        [this, signal_id](Scheduler&) {
            charge_decay_handles.at(signal_id).reset();
            auto& decaying_charge = *charge_values.at(signal_id);
            const auto active = [&](const std::size_t bit) {
                bool active_driver { };
                driver_values.at(signal_id).for_each_in_process_order(
                    [&](const DriverRecord& record) {
                        if (active_driver) {
                            return;
                        }
                        const auto logic = driver_force_logic4_at(
                            signal_id, record.process, record.value, bit);
                        const auto strength = record.strength;
                        active_driver
                            = (logic == Logic4::zero
                                  && strength.zero != StrengthRank::highz)
                            || (logic == Logic4::one
                                  && strength.one != StrengthRank::highz)
                            || (logic == Logic4::x
                                  && (strength.zero
                                          != StrengthRank::highz
                                      || strength.one
                                          != StrengthRank::highz));
                    });
                if (active_driver) {
                    return true;
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
    const auto* record = values.find(process);
    if (record == nullptr) {
        throw std::out_of_range(
            "process has no driver slot for SimIR signal");
    }
    return record->value;
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

} // namespace fsim::runtime::simir
