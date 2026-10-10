// SPDX-License-Identifier: Apache-2.0

#include "simir_internal.hpp"

#include <algorithm>
#include <array>
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

void Interpreter::Impl::commit(
    const SignalId signal_id,
    PackedLogic4 value,
    const SignalChangeOrigin origin)
{
    require_region_forwarding_role_journal_flushed_for_signal(signal_id);
    require_writable_aggregate_signal(signal_id);
    if (signal_id < signal_container_aggregate_aliases.size()
        && signal_container_aggregate_aliases[signal_id]) {
        const auto object
            = *signal_container_aggregate_aliases[signal_id];
        auto replacement = read_container_object_value(object);
        if (replacement.type.element_kind
                != ContainerElementKind::Packed
            || replacement.type.dimensions.empty()
            || replacement.type.element_width == 0U) {
            throw std::logic_error {
                "aggregate signal has no packed element projection"
            };
        }
        auto normalized = normalize_signal_value(
            signal_id, std::move(value));
        unpack_container_signal_value(replacement, normalized);
        write_container_object_value(
            object, replacement, std::nullopt, origin);
        return;
    }
    const auto& signal = get_signal(signal_id);
    if (signal.event_variable) {
        require_all_region_forwarding_role_journals_flushed();
    }
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
        prepare_region_authoritative_write(
            std::span<const SignalId> { members });
        for (const auto member : members) {
            const bool stored_changed = driven_values[member] != value;
            driven_values[member] = value;
            if (stored_changed && stored_signal_change_hook) {
                stored_signal_change_hook(member, scheduler.now());
            }
            publish_normalized(
                member, apply_force(member, value), false, origin);
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
                queue_static_next_delta(sensitivity.process, origin);
            }
        }
        const auto dynamic = dynamic_fanout.at(*identity);
        for (const auto sensitivity : dynamic) {
            auto& process = get_process(sensitivity.process);
            if (dynamic_wait_satisfied(
                    process, *identity, sensitivity)) {
                mark_dynamic_event_resume(process);
                queue_next_delta(sensitivity.process, origin);
            }
        }
        refresh_switch_network();
        return;
    }
    prepare_region_authoritative_write(signal_id);
    const bool stored_changed = driven_values[signal_id] != value;
    driven_values[signal_id] = value;
    mirror_region_stored(signal_id);
    if (stored_changed) {
        note_aggregate_leaf_stored_change(signal_id);
    }
    if (stored_changed && stored_signal_change_hook) {
        stored_signal_change_hook(signal_id, scheduler.now());
    }
    publish_normalized(
        signal_id, apply_force(signal_id, std::move(value)), true, origin);
    if (stored_changed) {
        publish_container_signal_aliases(signal_id, origin);
    }
    mark_switch_network_dirty(signal_id);
    refresh_switch_network();
}
void Interpreter::Impl::commit_direct_single_driver(
    const SignalId signal_id,
    PackedLogic4 value)
{
    require_region_forwarding_role_journal_flushed_for_signal(signal_id);
    require_writable_aggregate_signal(signal_id);
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
    prepare_region_authoritative_write(signal_id);
    const bool stored_changed = driven_values[signal_id] != value;
    driven_values[signal_id] = value;
    mirror_region_stored(signal_id);
    if (stored_changed) {
        note_aggregate_leaf_stored_change(signal_id);
    }
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
    const SignalId signal_id,
    const SignalChangeOrigin origin,
    const bool stored_changed)
{
    for (const auto object : signal_container_aliases.at(signal_id)) {
        for (const auto process : container_dynamic_fanout.at(object)) {
            queue_next_delta(process, origin);
        }
        if (container_object_change_hook) {
            container_object_change_hook(object, scheduler.now());
        }
    }
    if (const auto& alias = signal_container_element_aliases.at(signal_id);
        alias) {
        const auto [object, ordinal] = *alias;
        auto batch_active = false;
        if (object < container_aggregate_signal_aliases.size()
            && container_aggregate_signal_aliases[object]) {
            const auto proxy
                = container_aggregate_signal_aliases[object]->signal;
            auto& batch = aggregate_signal_batches[proxy];
            batch_active = batch.depth != 0U;
            if (batch_active && stored_changed) {
                batch.stored_changed = true;
                batch.origin = origin;
            }
        }
        if (!batch_active) {
            for (const auto process : container_dynamic_fanout.at(object)) {
                queue_next_delta(process, origin);
            }
        }
        if (auto* observer = container_alias_leaf_observer(signal_id)) {
            observer->element_changed = true;
            container_alias_write_batches[object].frames.back().changed = true;
        } else if (container_element_change_hook) {
            container_element_change_hook(
                object, ordinal, get_signal(signal_id).initial_value,
                scheduler.now());
        }
        const bool alias_write_batch_active
            = object < container_alias_write_batches.size()
            && !container_alias_write_batches[object].frames.empty();
        if (!batch_active && !alias_write_batch_active
            && container_object_change_hook) {
            container_object_change_hook(object, scheduler.now());
        }
        if (stored_changed && !batch_active
            && object < container_aggregate_signal_aliases.size()
            && container_aggregate_signal_aliases[object]
            && stored_signal_change_hook) {
            stored_signal_change_hook(
                container_aggregate_signal_aliases[object]->signal,
                scheduler.now());
        }
    }
}

void Interpreter::Impl::publish_aggregate_leaf_driver_change(
    const ProcessId process,
    const SignalId signal_id)
{
    if (!driver_change_hook
        || signal_id >= signal_container_element_aliases.size()
        || !signal_container_element_aliases[signal_id]) {
        return;
    }
    const auto object
        = signal_container_element_aliases[signal_id]->first;
    if (object >= container_aggregate_signal_aliases.size()
        || !container_aggregate_signal_aliases[object]) {
        return;
    }
    const auto proxy = container_aggregate_signal_aliases[object]->signal;
    auto& batch = aggregate_signal_batches[proxy];
    if (batch.depth != 0U) {
        batch.driver_changed = true;
        batch.driver = process;
        return;
    }
    driver_change_hook(
        process, container_aggregate_signal_aliases[object]->signal,
        scheduler.now());
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
    for (const auto signal : endpoints) {
        require_region_forwarding_role_journal_flushed_for_signal(signal);
    }
    std::vector<std::pair<SignalId, PackedLogic4>> values;
    values.reserve(endpoints.size());
    for (const auto signal : endpoints) {
        values.emplace_back(signal, resolved_driver_value(signal));
    }

    for (const auto& [signal, value] : values) {
        (void)value;
        prepare_region_authoritative_write(signal);
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
            mirror_region_stored(signal);
            if (stored_changed) {
                note_aggregate_leaf_stored_change(signal);
            }
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

void Interpreter::Impl::update_container_alias_force(
    const ContainerObjectId object,
    const PackedLogic4* value,
    const std::size_t offset,
    const std::size_t width)
{
    const auto& aliases = container_element_signal_aliases.at(object);
    const auto element_width
        = get_container_object(object).initial_value.type.element_width;
    std::vector<ContainerAliasForceUpdate> updates(aliases.size());
    std::vector<PackedLogic4> current;
    current.reserve(aliases.size());
    const auto end = offset + width;
    bool any_update { };
    invalidate_fused_static_cohorts();

    for (std::size_t ordinal = 0U; ordinal < aliases.size(); ++ordinal) {
        const auto signal = aliases[ordinal]->signal;
        materialize_direct_signal(signal);
        const auto base = (aliases.size() - ordinal - 1U) * element_width;
        const auto begin = std::max(offset, base);
        const auto finish = std::min(end, base + element_width);
        if (begin >= finish || (value == nullptr && !forced_values[signal])) {
            current.push_back(signals[signal].initial_value);
            continue;
        }

        demote_owned_driver(signal);
        auto& update = updates[ordinal];
        update.active = true;
        any_update = true;
        update.value = std::make_unique<PackedLogic4>(
            forced_values[signal]
                ? *forced_values[signal] : driven_values[signal]);
        update.mask = forced_masks[signal]
            ? std::make_unique<PackedLogic4>(*forced_masks[signal])
            : std::make_unique<PackedLogic4>(element_width, Logic4::zero);
        if (value != nullptr) {
            auto fragment = coerce_value_kind(
                value->extract_bits(begin - offset, finish - begin),
                signals[signal].value_kind);
            insert_force_value(*update.value, fragment, begin - base);
        }
        update_force_mask(
            *update.mask, begin - base, finish - begin, value != nullptr);
        if (value == nullptr && !has_forced_bits(*update.mask)) {
            update.value.reset();
            update.mask.reset();
        }

        auto effective = driven_values[signal];
        if (update.mask) {
            for (std::size_t bit = 0U; bit < effective.width(); ++bit) {
                if (update.mask->get(bit) == Logic4::one) {
                    effective.set(bit, update.value->get(bit));
                }
            }
        }
        current.push_back(std::move(effective));
    }
    if (!any_update) {
        return;
    }
    // No force mask has changed yet. The shared publication path stages all
    // current values and projections before installing these masks together.
    publish_container_alias_family(object, current, { }, updates);
}

void Interpreter::Impl::force_slice(
    const SignalId signal_id,
    PackedLogic4 value,
    const std::size_t offset)
{
    require_region_forwarding_role_journal_flushed_for_signal(signal_id);
    require_writable_aggregate_signal(signal_id);
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
    if (signal_id < signal_container_aggregate_aliases.size()
        && signal_container_aggregate_aliases[signal_id]) {
        const auto object
            = *signal_container_aggregate_aliases[signal_id];
        if (can_stage_container_alias_deposit(object)) {
            update_container_alias_force(object, &value, offset, value.width());
            return;
        }
        const auto& aliases = container_element_signal_aliases.at(object);
        const auto element_width
            = get_container_object(object).initial_value.type.element_width;
        const auto end = offset + value.width();
        prepare_region_authoritative_family_write(object);
        begin_aggregate_signal_batch(signal_id);
        try {
            for (std::size_t ordinal = 0U;
                ordinal < aliases.size(); ++ordinal) {
                const auto& alias = *aliases[ordinal];
                const auto base = (aliases.size() - ordinal - 1U)
                    * element_width;
                const auto leaf_end = base + element_width;
                const auto begin = std::max(offset, base);
                const auto finish = std::min(end, leaf_end);
                if (begin >= finish) {
                    continue;
                }
                force_slice(
                    alias.signal,
                    value.extract_bits(begin - offset, finish - begin),
                    begin - base);
            }
        } catch (...) {
            finish_aggregate_signal_batch(signal_id);
            throw;
        }
        finish_aggregate_signal_batch(signal_id);
        return;
    }
    prepare_region_authoritative_write(signal_id);
    if (signal_id < region_authoritative_component_by_signal.size()) {
        demote_region_authoritative_slots(
            region_authoritative_component_by_signal[signal_id], false);
    }
    invalidate_fused_static_cohorts();
    demote_owned_driver(signal_id);
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
    auto effective = apply_force(signal_id, driven_values[signal_id]);
    const bool current_changed = signal.initial_value != effective;
    publish(signal_id, std::move(effective));
    if (current_changed && signal_container_element_aliases[signal_id]) {
        // Force changes the logical element without changing its stored driver.
        publish_container_signal_aliases(signal_id, { }, false);
    }
    mark_switch_network_dirty(signal_id);
    refresh_switch_network();
}

void Interpreter::Impl::release_slice(
    const SignalId signal_id,
    const std::size_t offset,
    const std::size_t width)
{
    require_region_forwarding_role_journal_flushed_for_signal(signal_id);
    require_writable_aggregate_signal(signal_id);
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
    if (signal_id < signal_container_aggregate_aliases.size()
        && signal_container_aggregate_aliases[signal_id]) {
        const auto object
            = *signal_container_aggregate_aliases[signal_id];
        if (can_stage_container_alias_deposit(object)) {
            update_container_alias_force(object, nullptr, offset, width);
            return;
        }
        const auto& aliases = container_element_signal_aliases.at(object);
        const auto element_width
            = get_container_object(object).initial_value.type.element_width;
        const auto end = offset + width;
        prepare_region_authoritative_family_write(object);
        begin_aggregate_signal_batch(signal_id);
        try {
            for (std::size_t ordinal = 0U;
                ordinal < aliases.size(); ++ordinal) {
                const auto& alias = *aliases[ordinal];
                const auto base = (aliases.size() - ordinal - 1U)
                    * element_width;
                const auto leaf_end = base + element_width;
                const auto begin = std::max(offset, base);
                const auto finish = std::min(end, leaf_end);
                if (begin >= finish) {
                    continue;
                }
                release_slice(alias.signal, begin - base, finish - begin);
            }
        } catch (...) {
            finish_aggregate_signal_batch(signal_id);
            throw;
        }
        finish_aggregate_signal_batch(signal_id);
        return;
    }
    if (!forced_values[signal_id]) {
        return;
    }
    prepare_region_authoritative_write(signal_id);
    if (signal_id < region_authoritative_component_by_signal.size()) {
        demote_region_authoritative_slots(
            region_authoritative_component_by_signal[signal_id], false);
    }
    invalidate_fused_static_cohorts();
    update_force_mask(*forced_masks[signal_id], offset, width, false);
    const bool any_forced = has_forced_bits(*forced_masks[signal_id]);
    if (!any_forced) {
        forced_values[signal_id].reset();
        forced_masks[signal_id].reset();
    }
    refresh_direct_single_driver_route(signal_id);
    auto effective = apply_force(signal_id, driven_values[signal_id]);
    const bool current_changed = signal.initial_value != effective;
    publish(signal_id, std::move(effective));
    if (current_changed && signal_container_element_aliases[signal_id]) {
        // Force changes the logical element without changing its stored driver.
        publish_container_signal_aliases(signal_id, { }, false);
    }
    mark_switch_network_dirty(signal_id);
    refresh_switch_network();
}

PackedLogic4 Interpreter::Impl::apply_driver_force(
    const SignalId signal_id,
    const ProcessId process,
    PackedLogic4 value,
    const ForcedDriverMapView* const override_maps) const
{
    const auto* const values = override_maps == nullptr
        ? forced_driver_values.at(signal_id).get()
        : override_maps->values;
    if (values == nullptr) {
        if (override_maps != nullptr && override_maps->masks != nullptr) {
            throw std::logic_error {
                "driver force masks have no prepared value map"
            };
        }
        return value;
    }
    const auto* const masks = override_maps == nullptr
        ? forced_driver_masks.at(signal_id).get()
        : override_maps->masks;
    if (masks == nullptr) {
        throw std::logic_error { "forced drivers have no force-mask map" };
    }
    const auto forced = values->find(process);
    if (forced == values->end()) {
        return value;
    }
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
    require_region_forwarding_role_journal_flushed_for_signal(signal_id);
    require_writable_aggregate_signal(signal_id);
    const auto& signal = get_signal(signal_id);
    if (offset > signal.initial_value.width()
        || value.width() > signal.initial_value.width() - offset) {
        throw std::invalid_argument {
            "SimIR driver force slice is out of range"
        };
    }
    if (signal_id < signal_container_aggregate_aliases.size()
        && signal_container_aggregate_aliases[signal_id]) {
        const auto object
            = *signal_container_aggregate_aliases[signal_id];
        if (update_container_alias_driver_force(
                object, process, &value, offset, value.width())) {
            return;
        }
        const auto& aliases = container_element_signal_aliases.at(object);
        const auto element_width
            = get_container_object(object).initial_value.type.element_width;
        const auto end = offset + value.width();
        prepare_region_authoritative_family_write(object);
        begin_aggregate_signal_batch(
            signal_id, capture_signal_change_origin(process));
        try {
            for (std::size_t ordinal = 0U;
                ordinal < aliases.size(); ++ordinal) {
                const auto& alias = *aliases[ordinal];
                const auto base = (aliases.size() - ordinal - 1U)
                    * element_width;
                const auto leaf_end = base + element_width;
                const auto begin = std::max(offset, base);
                const auto finish = std::min(end, leaf_end);
                if (begin >= finish) {
                    continue;
                }
                force_driver_slice(
                    process, alias.signal,
                    value.extract_bits(begin - offset, finish - begin),
                    begin - base);
            }
        } catch (...) {
            finish_aggregate_signal_batch(signal_id);
            throw;
        }
        finish_aggregate_signal_batch(signal_id);
        return;
    }
    if (signal.resolution == ResolutionKind::none) {
        force_slice(signal_id, std::move(value), offset);
        return;
    }
    if (started && systemverilog_region_kernel_enabled) {
        // A forced-driver map changes the certified writer set even when the
        // currently resolved value happens to stay equal. Do not let this
        // writer-policy mutation inherit the value-only recertification latch.
        request_full_region_recertification();
    }
    prepare_region_authoritative_write(signal_id);
    if (signal_id < region_authoritative_component_by_signal.size()) {
        demote_region_authoritative_slots(
            region_authoritative_component_by_signal[signal_id], false);
    }
    invalidate_fused_static_cohorts();
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
    const bool stored_changed = driven_values[signal_id] != resolved;
    driven_values[signal_id] = resolved;
    mirror_region_stored(signal_id);
    if (stored_changed) {
        note_aggregate_leaf_stored_change(signal_id);
    }
    publish(signal_id, apply_force(signal_id, std::move(resolved)));
    if (stored_changed) {
        publish_container_signal_aliases(signal_id);
    }
    mark_switch_network_dirty(signal_id);
    refresh_switch_network();
}

void Interpreter::Impl::release_driver_slice(
    const ProcessId process,
    const SignalId signal_id,
    const std::size_t offset,
    const std::size_t width)
{
    require_region_forwarding_role_journal_flushed_for_signal(signal_id);
    require_writable_aggregate_signal(signal_id);
    const auto& signal = get_signal(signal_id);
    if (offset > signal.initial_value.width()
        || width > signal.initial_value.width() - offset) {
        throw std::invalid_argument {
            "SimIR driver release slice is out of range"
        };
    }
    if (signal_id < signal_container_aggregate_aliases.size()
        && signal_container_aggregate_aliases[signal_id]) {
        const auto object
            = *signal_container_aggregate_aliases[signal_id];
        if (update_container_alias_driver_force(
                object, process, nullptr, offset, width)) {
            return;
        }
        const auto& aliases = container_element_signal_aliases.at(object);
        const auto element_width
            = get_container_object(object).initial_value.type.element_width;
        const auto end = offset + width;
        prepare_region_authoritative_family_write(object);
        begin_aggregate_signal_batch(
            signal_id, capture_signal_change_origin(process));
        try {
            for (std::size_t ordinal = 0U;
                ordinal < aliases.size(); ++ordinal) {
                const auto& alias = *aliases[ordinal];
                const auto base = (aliases.size() - ordinal - 1U)
                    * element_width;
                const auto leaf_end = base + element_width;
                const auto begin = std::max(offset, base);
                const auto finish = std::min(end, leaf_end);
                if (begin >= finish) {
                    continue;
                }
                release_driver_slice(
                    process, alias.signal, begin - base, finish - begin);
            }
        } catch (...) {
            finish_aggregate_signal_batch(signal_id);
            throw;
        }
        finish_aggregate_signal_batch(signal_id);
        return;
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
    if (started && systemverilog_region_kernel_enabled) {
        // Releasing a force changes the certified writer set too; require a
        // full graph snapshot before the new driver policy can be admitted.
        request_full_region_recertification();
    }
    prepare_region_authoritative_write(signal_id);
    if (signal_id < region_authoritative_component_by_signal.size()) {
        demote_region_authoritative_slots(
            region_authoritative_component_by_signal[signal_id], false);
    }
    invalidate_fused_static_cohorts();
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
    const bool stored_changed = driven_values[signal_id] != resolved;
    driven_values[signal_id] = resolved;
    mirror_region_stored(signal_id);
    if (stored_changed) {
        note_aggregate_leaf_stored_change(signal_id);
    }
    publish(signal_id, apply_force(signal_id, std::move(resolved)));
    if (stored_changed) {
        publish_container_signal_aliases(signal_id);
    }
    mark_switch_network_dirty(signal_id);
    refresh_switch_network();
}

bool Interpreter::Impl::update_container_alias_driver_force(
    const ContainerObjectId object,
    const ProcessId process,
    const PackedLogic4* value,
    const std::size_t offset,
    const std::size_t width)
{
    if (!can_stage_container_alias_deposit(object)
        || process >= processes.size()
        || has_bidirectional_switches) {
        return false;
    }

    const auto& container = get_container_object(object).initial_value;
    const auto& type = container.type;
    const auto& aliases = container_element_signal_aliases.at(object);
    const auto proxy_signal
        = container_aggregate_signal_aliases.at(object)->signal;
    const auto proxy_width = static_cast<std::size_t>(
        get_signal(proxy_signal).initial_value.width());
    if (!type.fixed || type.dimensions.empty()
        || type.element_kind != ContainerElementKind::Packed
        || type.element_width == 0U || aliases.empty()
        || aliases.size() != container.elements.size()
        || offset > proxy_width
        || width > proxy_width - offset) {
        return false;
    }

    const auto& program = processes.program_view(process);
    if (program.switch_source() || program.switch_bidirectional()
        || program.switch_target()) {
        return false;
    }

    const auto end = offset + width;
    const auto element_width = static_cast<std::size_t>(type.element_width);
    if (proxy_width % element_width != 0U
        || proxy_width / element_width != aliases.size()) {
        return false;
    }

    // Decide admission for the entire family before allocating replacements or
    // changing any driver-force state. Resolution reuses the ordinary packed
    // resolver with a view of the prepared force maps. Implicit, charge, owned,
    // and switch-connected drivers remain on the conservative fallback.
    std::vector<std::uint8_t> selected(aliases.size());
    for (std::size_t ordinal = 0U; ordinal < aliases.size(); ++ordinal) {
        const auto& alias = *aliases[ordinal];
        const auto signal_id = alias.signal;
        const auto base = (aliases.size() - ordinal - 1U) * element_width;
        const auto begin = std::max(offset, base);
        const auto finish = std::min(end, base + element_width);
        const auto& signal = get_signal(signal_id);
        const auto& records = driver_values.at(signal_id);
        if (signal.resolution != ResolutionKind::sv_wire
            || signal.value_kind != ValueKind::logic4
            || signal.initial_value.width() != type.element_width
            || signal.has_implicit_driver || signal.has_charge_strength
            || owned_driver_active(signal_id)) {
            return false;
        }
        const auto& external = external_driver_values.at(signal_id);
        if (external
            && (external->is_logic9()
                || external->width() != signal.initial_value.width())) {
            return false;
        }
        bool compatible_records = true;
        records.for_each_in_process_order(
            [&](const DriverRecord& record) {
                compatible_records = compatible_records
                    && !record.value.is_logic9()
                    && record.value.width() == signal.initial_value.width();
            });
        if (!compatible_records) {
            return false;
        }
        if (begin >= finish) {
            continue;
        }
        const auto* owner_record = records.find(process);
        if (owner_record == nullptr) {
            return false;
        }
        const auto has_values = static_cast<bool>(
            forced_driver_values.at(signal_id));
        const auto has_masks = static_cast<bool>(
            forced_driver_masks.at(signal_id));
        if (has_values != has_masks) {
            return false;
        }
        if (has_values
            && forced_driver_values.at(signal_id)->contains(process)
                != forced_driver_masks.at(signal_id)->contains(process)) {
            return false;
        }
        if (value != nullptr
            || (has_values
                && forced_driver_values.at(signal_id)->contains(process))) {
            selected[ordinal] = 1U;
        }
    }

    std::vector<ContainerAliasDriverForceUpdate> updates(aliases.size());
    std::vector<PackedLogic4> resolved_values;
    resolved_values.reserve(aliases.size());
    for (const auto& alias : aliases) {
        resolved_values.push_back(driven_values.at(alias->signal));
    }

    bool any_update { };
    for (std::size_t ordinal = 0U; ordinal < aliases.size(); ++ordinal) {
        if (selected[ordinal] == 0U) {
            continue;
        }
        const auto signal_id = aliases[ordinal]->signal;
        const auto base = (aliases.size() - ordinal - 1U) * element_width;
        const auto begin = std::max(offset, base);
        const auto finish = std::min(end, base + element_width);
        const auto local_offset = begin - base;
        const auto local_width = finish - begin;
        const auto& records = driver_values.at(signal_id);
        const auto* owner_record = records.find(process);
        if (owner_record == nullptr) {
            return false;
        }

        auto& update = updates[ordinal];
        const auto& old_values = forced_driver_values.at(signal_id);
        const auto& old_masks = forced_driver_masks.at(signal_id);
        if (old_values) {
            update.values = std::make_unique<ForcedDriverMap>(*old_values);
            update.masks = std::make_unique<ForcedDriverMap>(*old_masks);
        } else if (value != nullptr) {
            update.values = std::make_unique<ForcedDriverMap>();
            update.masks = std::make_unique<ForcedDriverMap>();
        }

        if (value != nullptr) {
            auto forced = update.values->find(process);
            if (forced == update.values->end()) {
                update.values->emplace(process, owner_record->value);
                forced = update.values->find(process);
                update.masks->emplace(
                    process,
                    PackedLogic4 {
                        get_signal(signal_id).initial_value.width(),
                        Logic4::zero
                    });
            }
            const auto mask = update.masks->find(process);
            if (mask == update.masks->end()) {
                return false;
            }
            auto fragment = coerce_value_kind(
                value->extract_bits(begin - offset, local_width),
                ValueKind::logic4);
            insert_force_value(forced->second, fragment, local_offset);
            update_force_mask(
                mask->second, local_offset, local_width, true);
        } else {
            auto forced = update.values->find(process);
            auto mask = update.masks->find(process);
            if (forced == update.values->end()
                || mask == update.masks->end()) {
                continue;
            }
            update_force_mask(
                mask->second, local_offset, local_width, false);
            if (!has_forced_bits(mask->second)) {
                update.values->erase(forced);
                update.masks->erase(mask);
                if (update.values->empty()) {
                    update.values.reset();
                    update.masks.reset();
                }
            }
        }

        update.active = true;
        any_update = true;

        const auto override_maps = ForcedDriverMapView {
            update.values.get(), update.masks.get()
        };
        resolved_values[ordinal] = resolved_local_driver_value(
            signal_id, &override_maps);
        update.effective_current = apply_force(
            signal_id, resolved_values[ordinal]);
    }

    if (!any_update) {
        return true;
    }
    publish_container_alias_family(
        object, resolved_values, capture_signal_change_origin(process), { },
        selected, updates);
    return true;
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
    demote_owned_driver(signal_id);
    auto& values = driver_values.at(signal_id);
    if (auto* record = values.find(process)) {
        return record->value;
    }
    if (started && region_graph) {
        // Adding an unregistered owner changes the writer topology. Revoke
        // graph/A4 certificates and detach every borrowed value slot before
        // DriverTable insertion can change its record storage.
        prepare_signal_observation(signal_id);
    }
    const bool inserted = values.insert_if_absent(DriverRecord {
        process,
        initial_driver_value(signal_id),
        processes.program_view(process).drive_strength()
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
    if (has_container_signal_alias(signal_id)
        || switch_adjacency_unknown || switch_connected
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
    if (record == nullptr || record->strength != DriveStrength { }
        || record->scalar_regions) {
        return;
    }
    route.process = record->process;
    route.active = true;
    direct_single_driver_processes[signal_id] = record->process;
}

[[nodiscard]] PackedLogic4 Interpreter::Impl::resolved_local_driver_value(
    const SignalId signal_id,
    const ForcedDriverMapView* const override_maps) const
{
    if (owned_driver_active(signal_id)) {
        return owned_driver_composites[signal_id].committed;
    }
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
        && sole_record != nullptr && !sole_record->scalar_regions
        && !external_driver_values.at(signal_id)
        && !signal.has_implicit_driver
        && !signal.has_charge_strength
        && sole_record->strength == DriveStrength { }) {
        return apply_driver_force(
            signal_id, sole_record->process, sole_record->value,
            override_maps);
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
                    signal_id, record.process, record.value,
                    override_maps);
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
        const DriverRecord* record { };
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
            signal_id, record.process, record.value, override_maps));
        strength_drivers.push_back({ &drivers.back(), record.strength, &record });
    });
    if (external_driver_values.at(signal_id)) {
        drivers.push_back(
            *external_driver_values.at(signal_id));
        strength_drivers.push_back(
            { &*external_driver_values.at(signal_id), { } });
    }
    if (resolution == ResolutionKind::std_logic
        && signal.value_kind == ValueKind::logic9
        && std::ranges::any_of(strength_drivers,
            [](const DriverContribution& driver) {
                return driver.record != nullptr && driver.record->scalar_regions;
            })) {
        // Scalar subelements without a source retain their unforced stored
        // value. Reading current here could persist a temporary signal force.
        auto result = driven_values.at(signal_id);
        for (std::size_t bit = 0U; bit < result.width(); ++bit) {
            bool has_source { };
            auto resolved = Logic9::z;
            for (const auto& driver : strength_drivers) {
                if (driver.record != nullptr && driver.record->scalar_regions
                    && std::ranges::none_of(*driver.record->scalar_regions,
                        [bit](const Process::DriverRegion& region) {
                            return bit >= region.offset
                                && bit - region.offset < region.width;
                        })) {
                    continue;
                }
                const auto value = driver.value->get_logic9(bit);
                // std_logic_1164 retains every state, including '-', for
                // one source. Only actual overlapping sources are resolved.
                resolved = has_source ? runtime::resolve(resolved, value) : value;
                has_source = true;
            }
            if (has_source) {
                result.set_logic9(bit, resolved);
            }
        }
        return result;
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
    require_region_forwarding_role_journal_flushed_for_signal(signal_id);
    demote_owned_driver(signal_id);
    auto& value = external_driver_values.at(signal_id);
    if (!value) {
        auto replacement = std::make_unique<PackedLogic4>(
            initial_driver_value(signal_id));
        note_region_graph_policy_change();
        value = std::move(replacement);
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
    if (owned_driver_active(signal_id)) {
        const auto& value = owned_driver_composites[signal_id].committed;
        for (std::size_t bit = 0U; bit < value.width(); ++bit) {
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
    impl_->require_region_forwarding_role_journal_flushed_for_signal(signal);
    return impl_->resolved_signal_strength(signal);
}

bool Interpreter::Impl::switch_process(const ProcessId process) const
{
    return process < processes.size()
        && processes.program_view(process).switch_bidirectional();
}

void Interpreter::Impl::reset_switch_drivers(
    const std::size_t component)
{
    if (component >= switch_components.size()) {
        return;
    }
    for (const auto signal : switch_components[component].signals) {
        require_region_forwarding_role_journal_flushed_for_signal(signal);
    }
    for (const auto signal : switch_components[component].signals) {
        prepare_region_authoritative_write(signal);
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
    const DriveStrength strength,
    std::shared_ptr<const std::vector<Process::DriverRegion>> scalar_regions,
    const bool delayed_writer)
{
    require_region_forwarding_role_journal_flushed_for_signal(signal_id);
    demote_owned_driver(signal_id);
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
        if (signal.value_kind == ValueKind::logic9) {
            selected.fill(Logic9::z);
        }
        for (const auto& region : regions) {
            selected = insert_value(
                std::move(selected),
                extract_value(initial, region.offset, region.width),
                region.offset);
        }
        initial = std::move(selected);
    }
    if (const auto delayed = delayed_net_initial_values.find(signal_id);
        delayed != delayed_net_initial_values.end() && delayed_writer) {
        // The unknown bits this driver covers start X (see add_signal).
        for (const auto& region : regions) {
            const auto first = region.whole ? std::size_t { } : region.offset;
            const auto last = region.whole ? initial.width()
                                           : region.offset + region.width;
            for (auto bit = first; bit < last && bit < initial.width();
                ++bit) {
                if (delayed->second.get(bit) == Logic4::x) {
                    initial.set(bit, Logic4::x);
                }
            }
        }
    }
    const std::array changed_signals { signal_id };
    auto container_reference_refresh
        = prepare_container_value_reference_refresh(changed_signals);
    auto& values = driver_values.at(signal_id);
    const bool adds_driver_owner = values.find(process) == nullptr;
    if (!adds_driver_owner) {
        return;
    }
    prepare_region_authoritative_write(signal_id);
    if (started && adds_driver_owner) {
        note_region_graph_policy_change();
    }
    const bool inserted = values.insert_if_absent(DriverRecord {
        process, std::move(initial), strength, std::move(scalar_regions)
    });
    if (!inserted) {
        return;
    }
    refresh_direct_single_driver_route(signal_id);
    auto resolved = resolved_driver_value(signal_id);
    const bool stored_changed = driven_values[signal_id] != resolved;
    const bool current_changed
        = signals[signal_id].initial_value != resolved;
    driven_values[signal_id] = resolved;
    signals[signal_id].initial_value = resolved;
    if (stored_changed) {
        note_aggregate_leaf_stored_change(signal_id);
    }
    if (current_changed) {
        note_aggregate_leaf_current_change(signal_id);
    }
    refresh_direct_signal_planes(signal_id);
    signal_last_values[signal_id] = std::move(resolved);
    ActiveContainerReferenceRefresh active_refresh;
    begin_container_value_reference_refresh(
        active_refresh, changed_signals, container_reference_refresh);
    synchronize_container_value_references(
        changed_signals, container_reference_refresh);
    end_container_value_reference_refresh(active_refresh);
}

void Interpreter::Impl::set_driver(
    const ProcessId process,
    const SignalId signal_id,
    PackedLogic4 value,
    const bool preserve_wide_authority)
{
    require_region_forwarding_role_journal_flushed_for_signal(signal_id);
    require_writable_aggregate_signal(signal_id);
    bool can_preserve_wide_authority
        = preserve_wide_authority && !owned_driver_active(signal_id);
    if (can_preserve_wide_authority) {
        const auto program = processes.program_view(process);
        const bool has_switch_behavior
            = program.switch_source() || program.switch_target()
            || program.switch_bidirectional();
        can_preserve_wide_authority = !has_switch_behavior;
    }
    can_preserve_wide_authority
        = can_preserve_wide_authority
        && (can_try_wide_single_owner_commit(process, signal_id)
            || can_try_wide_disjoint_owner_commit(process, signal_id));
    if (!can_preserve_wide_authority) {
        demote_owned_driver(signal_id);
    }
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
    if (const auto source = processes.program_view(process).switch_source()) {
        auto strength = resolved_signal_strength(*source);
        if (processes.program_view(process).switch_resistive()) {
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
    can_preserve_wide_authority
        = can_preserve_wide_authority && !switch_strength;
    if (can_preserve_wide_authority && driver_already_registered) {
        // A registered wide record may be the bound owner-role facade itself.
        // Publish its new raw value through the versioned A4 plane so a
        // retained owner snapshot stays immutable and the later value phase
        // can still observe the raw-before-value ordering.
        const auto* const record
            = driver_values.at(signal_id).find(process);
        if (record == nullptr) {
            throw std::logic_error {
                "wide owner route lost its registered driver record"
            };
        }
        if (record->value == value) {
            return;
        }
        if (try_publish_wide_owner_raw(process, signal_id, value)) {
            return;
        }
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
    mirror_region_owner(signal_id, process, slot);
    if (driver_change_hook) {
        driver_change_hook(process, signal_id, scheduler.now());
        publish_aggregate_leaf_driver_change(process, signal_id);
    }
}

bool Interpreter::Impl::try_publish_wide_owner_raw(
    const ProcessId process,
    const SignalId signal_id,
    const PackedLogic4& value)
{
    const bool single_owner_route
        = can_try_wide_single_owner_commit(process, signal_id);
    const bool disjoint_owner_route
        = !single_owner_route
        && can_try_wide_disjoint_owner_commit(process, signal_id);
    if (!single_owner_route && !disjoint_owner_route) {
        if (signal_id < region_authoritative_component_by_signal.size()) {
            demote_region_authoritative_slots(
                region_authoritative_component_by_signal[signal_id], false);
        }
        return false;
    }
    const auto component
        = region_authoritative_component_by_signal[signal_id];
    auto* const state = region_authoritative_state_for_signal(signal_id);
    if (state == nullptr
        || !state->values().packed_owner_slot_bound(signal_id, process)) {
        demote_region_authoritative_slots(component, false);
        return false;
    }

    auto& mutation = state->wide_mutation_scratch();
    try {
        state->values().prepare_owner_change_into(mutation, signal_id,
            process, value, signals[signal_id].initial_value,
            driven_values[signal_id]);
        if (!state->values().begin_prepared_publication(mutation)) {
            state->values().cancel_prepared_publication(mutation);
            demote_region_authoritative_slots(component, false);
            return false;
        }
    } catch (const std::bad_alloc&) {
        state->values().cancel_prepared_publication(mutation);
        demote_region_authoritative_slots(component, false);
        return false;
    } catch (const std::invalid_argument&) {
        state->values().cancel_prepared_publication(mutation);
        if (disjoint_owner_route) {
            demote_region_authoritative_slots(component, false);
            return false;
        }
        throw;
    } catch (...) {
        state->values().cancel_prepared_publication(mutation);
        throw;
    }

    const bool owner_changed = mutation.any_owner_changed;
    state->values().publish(std::move(mutation));
    if (!state->valid()) {
        demote_region_authoritative_slots(component, false);
        return false;
    }
    if (owner_changed) {
        note_region_authoritative_mirror();
        if (systemverilog_wave_profile_enabled) {
            ++systemverilog_wave_profile_a4_authoritative_slot_writes;
            ++systemverilog_wave_profile_a4_owner_mirrors;
        }
    }
    return true;
}

bool Interpreter::Impl::try_publish_wide_owner_slice_raw(
    const ProcessId process,
    const SignalId signal_id,
    const PackedLogic4& slice_value,
    const std::size_t offset,
    const WideDisjointOwnerCommitContext& context)
{
    if (context.process != process || context.signal != signal_id
        || context.state == nullptr) {
        return false;
    }
    const auto component = context.component;
    auto* const state = context.state;

    auto& mutation = state->wide_mutation_scratch();
    try {
        state->values().prepare_owner_slice_change_into(
            mutation, signal_id, process, slice_value, offset,
            signals[signal_id].initial_value, driven_values[signal_id]);
        if (!state->values().begin_prepared_publication(mutation)) {
            state->values().cancel_prepared_publication(mutation);
            demote_region_authoritative_slots(component, false);
            return false;
        }
    } catch (const std::bad_alloc&) {
        state->values().cancel_prepared_publication(mutation);
        demote_region_authoritative_slots(component, false);
        return false;
    } catch (const std::invalid_argument&) {
        state->values().cancel_prepared_publication(mutation);
        demote_region_authoritative_slots(component, false);
        return false;
    } catch (...) {
        state->values().cancel_prepared_publication(mutation);
        throw;
    }

    const bool owner_changed = mutation.any_owner_changed;
    state->values().publish(std::move(mutation));
    if (!state->valid()) {
        demote_region_authoritative_slots(component, false);
        return false;
    }
    if (owner_changed) {
        note_region_authoritative_mirror();
        if (systemverilog_wave_profile_enabled) {
            ++systemverilog_wave_profile_a4_authoritative_slot_writes;
            ++systemverilog_wave_profile_a4_owner_mirrors;
        }
    }
    return true;
}

void Interpreter::Impl::commit_driver(
    const ProcessId process,
    const SignalId signal_id,
    PackedLogic4 value,
    const std::optional<SignalChangeOrigin> origin,
    const bool route_path,
    const bool allow_wide_single_owner_commit)
{
    require_region_forwarding_role_journal_flushed_for_signal(signal_id);
    require_writable_aggregate_signal(signal_id);
    const auto change_origin = origin.value_or(
        capture_signal_change_origin(process));
    if (route_path && route_module_path_update(
            process, signal_id, value, std::nullopt,
            nullptr, 0, change_origin)) {
        return;
    }
    if (signal_id < signal_container_aggregate_aliases.size()
        && signal_container_aggregate_aliases[signal_id]) {
        const auto object
            = *signal_container_aggregate_aliases[signal_id];
        auto replacement = read_container_object_value(object);
        auto normalized = normalize_signal_value(
            signal_id, std::move(value));
        unpack_container_signal_value(replacement, normalized);
        write_container_object_value(
            object, replacement, process, change_origin);
        return;
    }
    if (allow_wide_single_owner_commit
        && try_commit_wide_single_owner(
            process, signal_id, value, change_origin, false)) {
        return;
    }
    if (get_signal(signal_id).resolution
        == ResolutionKind::none) {
        if (change_origin.process_domain
                == ProcessSchedulingDomain::systemverilog
            && change_origin.phase == SchedulerPhase::active
            && try_commit_wide_unresolved_owner_alias(
                process, signal_id, value, change_origin)) {
            return;
        }
        commit(signal_id, std::move(value), change_origin);
        return;
    }
    // The versioned wide-slot specialization may decline because tracing,
    // observation, or an unsupported runtime effect became active. Detach
    // those slots before the ordinary path changes its original raw driver;
    // set_driver itself mirrors the raw value before commit_resolved begins.
    prepare_region_authoritative_write(signal_id);
    set_driver(process, signal_id, std::move(value));
    commit_resolved(
        signal_id, resolved_driver_value(signal_id), change_origin);
}

bool Interpreter::Impl::try_commit_wide_single_owner(
    const ProcessId process,
    const SignalId signal_id,
    PackedLogic4 value,
    const SignalChangeOrigin origin,
    const bool raw_owner_already_mirrored)
{
    if (!can_try_wide_single_owner_commit(process, signal_id)
        || value.width() != signals[signal_id].initial_value.width()) {
        return false;
    }

    const auto component
        = region_authoritative_component_by_signal[signal_id];
    auto* const state = region_authoritative_state_for_signal(signal_id);
    if (state == nullptr) {
        return false;
    }

    auto& mutation = state->wide_mutation_scratch();
    try {
        value = normalize_signal_value(signal_id, std::move(value));
        if (raw_owner_already_mirrored) {
            state->values().prepare_owner_value_change_into(
                mutation, signal_id, process, value);
        } else {
            state->values().prepare_owner_change_into(mutation, signal_id,
                process, value, value, value);
        }
        if (!state->values().begin_prepared_publication(mutation)) {
            state->values().cancel_prepared_publication(mutation);
            demote_region_authoritative_slots(component, false);
            return false;
        }
    } catch (const std::bad_alloc&) {
        state->values().cancel_prepared_publication(mutation);
        demote_region_authoritative_slots(component, false);
        return false;
    }

    const bool current_changed = mutation.any_current_changed;
    try {
        // This path admits no trace hook, transaction observer, alias, or
        // other callback. Queueing transaction waiters therefore remains
        // internal bookkeeping while every versioned role is locked.
        note_signal_transaction(signal_id, true, origin, true);
    } catch (...) {
        state->values().cancel_prepared_publication(mutation);
        demote_region_authoritative_slots(component, false);
        throw;
    }

    state->values().publish(std::move(mutation));
    if (!state->valid()) {
        demote_region_authoritative_slots(component, false);
        throw std::logic_error {
            "preflighted wide A4 publication became invalid"
        };
    }
    if (current_changed) {
        refresh_direct_signal_planes(signal_id);
        publish_value_change(signal_id, true, origin);
    }
    return true;
}

bool Interpreter::Impl::try_commit_wide_unresolved_owner_alias(
    const ProcessId process,
    const SignalId signal_id,
    const PackedLogic4& value,
    const SignalChangeOrigin origin)
{
    // A same-kind non-scalar Logic4 or Logic9 payload is already normalized
    // for this route. Kind coercions decline and retain ordinary commit's
    // normalization behavior.
    if (!can_try_wide_unresolved_owner_alias(process, signal_id, origin)
        || value.is_logic9()
            != (signals[signal_id].value_kind == ValueKind::logic9)
        || value.width() != signals[signal_id].initial_value.width()) {
        return false;
    }
    const auto component
        = region_authoritative_component_by_signal[signal_id];
    auto* const state = region_authoritative_state_for_signal(signal_id);
    if (state == nullptr) {
        return false;
    }

    auto& mutation = state->wide_mutation_scratch();
    try {
        state->values().prepare_owner_change_into(
            mutation, signal_id, process, value, value, value);
        if (!state->values().begin_prepared_publication(mutation)) {
            state->values().cancel_prepared_publication(mutation);
            demote_region_authoritative_slots(component, false);
            return false;
        }
    } catch (const std::bad_alloc&) {
        state->values().cancel_prepared_publication(mutation);
        demote_region_authoritative_slots(component, false);
        return false;
    } catch (const std::invalid_argument&) {
        state->values().cancel_prepared_publication(mutation);
        demote_region_authoritative_slots(component, false);
        return false;
    } catch (...) {
        state->values().cancel_prepared_publication(mutation);
        throw;
    }

    const bool current_changed = mutation.any_current_changed;
    try {
        // The no-resolution owner aliases the stored/raw role, so this is a
        // single prepared publication with the ordinary transaction-before-
        // current-change ordering and no fabricated DriverRecord.
        note_signal_transaction(signal_id, true, origin, true);
    } catch (...) {
        state->values().cancel_prepared_publication(mutation);
        demote_region_authoritative_slots(component, false);
        throw;
    }

    state->values().publish(std::move(mutation));
    if (!state->valid()) {
        demote_region_authoritative_slots(component, false);
        throw std::logic_error {
            "preflighted unresolved wide A4 publication became invalid"
        };
    }
    if (systemverilog_wave_profile_enabled) {
        ++systemverilog_wave_profile_a4_unresolved_owner_alias_commits;
    }
    if (current_changed) {
        refresh_direct_signal_planes(signal_id);
        publish_value_change(signal_id, true, origin);
    }
    return true;
}

bool Interpreter::Impl::can_try_wide_unresolved_owner_alias(
    const ProcessId process,
    const SignalId signal_id,
    const SignalChangeOrigin origin)
{
    if (!a4_wide_single_owner_commit_enabled || !started
        || !region_graph || scheduler.trace_hook_installed()
        || signal_id >= signals.size()
        || signal_id >= driven_values.size()
        || signal_id >= driver_values.size()
        || signal_id >= external_driver_values.size()
        || signal_id >= forced_values.size()
        || signal_id >= forced_driver_values.size()
        || signal_id >= signal_transaction_observed.size()
        || signal_id >= signal_value_revisions.size()
        || signal_id >= direct_signal_materialization_pending.size()
        || !process_signal_access_inventory_complete
        || !process_signal_access_is_complete(process)
        || has_bidirectional_switches || !module_timing_checks.empty()
        || signal_change_hook || stored_signal_change_hook
        || driver_change_hook || scalar_signal_change_hook
        || container_object_change_hook || container_element_change_hook
        || native_signal_observation_required_hook
        || native_signal_observation_any_hook || execution_point_hook) {
        return false;
    }

    const auto& signal = signals[signal_id];
    if (signal.initial_value.width() <= 64U
        || (signal.value_kind != ValueKind::logic4
            && signal.value_kind != ValueKind::logic9)
        || signal.resolution != ResolutionKind::none
        || signal.event_variable || signal.has_implicit_driver
        || signal.has_charge_strength
        || signal.systemverilog_scalar != SystemVerilogScalarKind::None
        || owned_driver_active(signal_id)
        || external_driver_values[signal_id] || forced_values[signal_id]
        || forced_driver_values[signal_id]
        || direct_signal_materialization_pending[signal_id] != 0U
        || native_signal_has_runtime_dependency(signal_id)
        || has_dynamic_waits(signal_id) || monitor_watches(signal_id)
        || !signal_container_aliases[signal_id].empty()
        || signal_transaction_observed[signal_id]
        || !driver_values[signal_id].empty()) {
        return false;
    }

    if (process >= region_graph->processes().size()
        || signal_id >= region_graph->signals().size()
        || signal_id >= region_authoritative_component_by_signal.size()) {
        return false;
    }
    const auto& process_node = region_graph->processes()[process];
    const bool vhdl_projected_owner
        = process_node.scheduling_domain == ProcessSchedulingDomain::generic
        && process_node.update_kind == RegionUpdateKind::vhdl_projected;
    const bool systemverilog_active_owner
        = process_node.scheduling_domain
            == ProcessSchedulingDomain::systemverilog
        && process_node.update_kind
            == RegionUpdateKind::systemverilog_active;
    const bool original_vhdl_origin
        = vhdl_projected_owner
        && origin.process_domain == ProcessSchedulingDomain::generic;
    const bool original_active_origin
        = systemverilog_active_owner
        && origin.process_domain == ProcessSchedulingDomain::systemverilog
        && origin.phase == SchedulerPhase::active;
    if (!original_vhdl_origin && !original_active_origin) {
        return false;
    }
    const auto& signal_node = region_graph->signals()[signal_id];
    if (signal_node.drivers != RegionDriverClass::single_whole
        || signal_node.writers.size() != 1U
        || signal_node.writers.front().process != process
        || signal_node.writers.front().offset != 0U
        || (signal_node.writers.front().width != 0U
            && signal_node.writers.front().width != signal.initial_value.width())
        || signal_node.writers_unknown
        || signal_node.dynamic_fork_writers
        || signal_node.partial_projected_transactions
        || signal_node.observations != RegionObservation::none) {
        return false;
    }

    const auto component
        = region_authoritative_component_by_signal[signal_id];
    if (component >= region_authoritative_state_by_component.size()
        || component >= region_graph->certificate_inventory()
                            .components.size()) {
        return false;
    }
    const auto* const state
        = region_authoritative_state_for_signal(signal_id);
    if (state == nullptr || !state->valid()
        || state->generation() != region_runtime_generation
        || !state->values().packed_slots_bound()
        || !state->values().packed_signal_slots_bound(signal_id)
        || !state->values().packed_owner_slot_bound(signal_id, process)
        || !region_graph->component_epochs_current(component)) {
        return false;
    }
    const auto& certificate = region_graph->certificate_inventory()
        .components[component];
    if (!region_graph->certificate_inventory().access_inventory_complete
        || certificate.status
            == RegionComponentCertificateStatus::incomplete_access_inventory
        || !std::ranges::binary_search(certificate.members, process)) {
        return false;
    }
    const auto& layout_signal = state->values().layout().signal(signal_id);
    const auto owners = state->values().layout().owners(signal_id);
    return layout_signal.storage_class
            == SignalDriverStorageClass::single_owner
        && owners.size() == 1U && owners.front().process == process
        && owners.front().aliases_stored;
}

bool Interpreter::Impl::can_try_wide_single_owner_commit(
    const ProcessId process,
    const SignalId signal_id)
{
    if (signal_id >= signals.size()
        || signals[signal_id].initial_value.width() <= 64U) {
        return false;
    }
    if (!a4_wide_single_owner_commit_enabled || !started
        || !region_graph || scheduler.trace_hook_installed()
        || signal_id >= signals.size()
        || signal_id >= driven_values.size()
        || signal_id >= driver_values.size()
        || signal_id >= direct_single_driver_routes.size()
        || signal_id >= external_driver_values.size()
        || signal_id >= forced_values.size()
        || signal_id >= forced_driver_values.size()
        || signal_id >= signal_transactions.size()
        || signal_id >= signal_value_revisions.size()
        || signal_id >= direct_signal_materialization_pending.size()
        || !process_signal_access_inventory_complete
        || !process_signal_access_is_complete(process)
        || has_bidirectional_switches || !module_timing_checks.empty()
        || signal_change_hook || stored_signal_change_hook
        || driver_change_hook || scalar_signal_change_hook
        || container_object_change_hook || container_element_change_hook
        || native_signal_observation_required_hook
        || native_signal_observation_any_hook || execution_point_hook) {
        return false;
    }

    const auto& signal = signals[signal_id];
    if (signal.event_variable || signal.has_implicit_driver
        || signal.has_charge_strength
        || signal.systemverilog_scalar != SystemVerilogScalarKind::None
        || owned_driver_active(signal_id)
        || external_driver_values[signal_id] || forced_values[signal_id]
        || forced_driver_values[signal_id]
        || direct_signal_materialization_pending[signal_id] != 0U
        || native_signal_has_runtime_dependency(signal_id)
        || has_dynamic_waits(signal_id) || monitor_watches(signal_id)
        || !signal_container_aliases[signal_id].empty()
        || (signal_id < signal_transaction_observed.size()
            && signal_transaction_observed[signal_id])) {
        return false;
    }
    if ((signal.value_kind == ValueKind::logic4
            && signal.resolution != ResolutionKind::sv_wire)
        || (signal.value_kind == ValueKind::logic9
            && signal.resolution != ResolutionKind::std_logic)
        || (signal.value_kind != ValueKind::logic4
            && signal.value_kind != ValueKind::logic9)) {
        return false;
    }

    if (signal_id >= region_authoritative_component_by_signal.size()) {
        return false;
    }
    const auto component
        = region_authoritative_component_by_signal[signal_id];
    if (component >= region_authoritative_state_by_component.size()
        || component >= region_graph->certificate_inventory()
                            .components.size()) {
        return false;
    }
    const auto& route = direct_single_driver_routes[signal_id];
    const auto* const record = direct_single_driver_record(signal_id);
    auto* const state = region_authoritative_state_for_signal(signal_id);
    if (!route.active || route.process != process || record == nullptr
        || record->strength != DriveStrength { }
        || driver_values[signal_id].size() != 1U || state == nullptr
        || !state->valid() || state->generation() != region_runtime_generation
        || !state->values().packed_slots_bound()
        || !state->values().packed_signal_slots_bound(signal_id)
        || !state->values().packed_owner_slot_bound(signal_id, process)
        || !region_graph->component_epochs_current(component)) {
        return false;
    }
    const auto& certificate = region_graph->certificate_inventory()
        .components[component];
    if (!region_graph->certificate_inventory().access_inventory_complete
        || certificate.status
            == RegionComponentCertificateStatus::incomplete_access_inventory) {
        return false;
    }
    // The mapped single-owner layout is an independent storage proof. A
    // public std_logic boundary need not have an executable cone program.
    const auto& layout_signal = state->values().layout().signal(signal_id);
    const auto owners = state->values().layout().owners(signal_id);
    if (layout_signal.storage_class != SignalDriverStorageClass::single_owner
        || owners.size() != 1U || owners.front().process != process) {
        return false;
    }
    return true;
}

bool Interpreter::Impl::can_try_wide_disjoint_signal_commit(
    const SignalId signal_id,
    RegionAuthoritativeComponentState** const eligible_state)
{
    if (eligible_state != nullptr) {
        *eligible_state = nullptr;
    }
    if (!a4_wide_disjoint_owner_commit_enabled || !started
        || !region_graph || scheduler.trace_hook_installed()
        || signal_id >= signals.size()
        || signal_id >= driven_values.size()
        || signal_id >= driver_values.size()
        || signal_id >= direct_single_driver_routes.size()
        || signal_id >= external_driver_values.size()
        || signal_id >= forced_values.size()
        || signal_id >= forced_driver_values.size()
        || signal_id >= signal_transactions.size()
        || signal_id >= signal_value_revisions.size()
        || signal_id >= direct_signal_materialization_pending.size()
        || !process_signal_access_inventory_complete
        || !region_graph->certificate_inventory().access_inventory_complete
        || has_bidirectional_switches || !module_timing_checks.empty()
        || signal_change_hook || stored_signal_change_hook
        || driver_change_hook || scalar_signal_change_hook
        || container_object_change_hook || container_element_change_hook
        || native_signal_observation_required_hook
        || native_signal_observation_any_hook || execution_point_hook) {
        return false;
    }

    const auto& signal = signals[signal_id];
    if (signal.initial_value.width() == 0U
        || signal.event_variable || signal.has_implicit_driver
        || signal.has_charge_strength
        || signal.systemverilog_scalar != SystemVerilogScalarKind::None
        || owned_driver_active(signal_id)
        || external_driver_values[signal_id] || forced_values[signal_id]
        || forced_driver_values[signal_id]
        || direct_signal_materialization_pending[signal_id] != 0U
        || native_signal_has_runtime_dependency(signal_id)
        || has_dynamic_waits(signal_id) || monitor_watches(signal_id)
        || !signal_container_aliases[signal_id].empty()
        || (signal_id < signal_transaction_observed.size()
            && signal_transaction_observed[signal_id])) {
        return false;
    }
    if ((signal.value_kind == ValueKind::logic4
            && signal.resolution != ResolutionKind::sv_wire)
        || (signal.value_kind == ValueKind::logic9
            && signal.resolution != ResolutionKind::std_logic)
        || (signal.value_kind != ValueKind::logic4
            && signal.value_kind != ValueKind::logic9)) {
        return false;
    }

    if (signal_id >= region_authoritative_component_by_signal.size()) {
        return false;
    }
    const auto component
        = region_authoritative_component_by_signal[signal_id];
    if (component >= region_authoritative_state_by_component.size()
        || component >= region_graph->certificate_inventory()
                            .components.size()
        || direct_single_driver_routes[signal_id].active) {
        return false;
    }
    auto* const state
        = region_authoritative_state_for_signal(signal_id);
    const auto component_status
        = region_graph->certificate_inventory().components[component].status;
    const bool status_allows_disjoint_boundary_state
        = a4_wide_disjoint_owner_commit_enabled
        && component_status
            != RegionComponentCertificateStatus::incomplete_access_inventory;
    if (state == nullptr || !state->valid()
        || state->generation() != region_runtime_generation
        || !state->values().packed_slots_bound()
        || !state->values().packed_signal_slots_bound(signal_id)
        || !region_graph->component_epochs_current(component)
        || (component_status
                != RegionComponentCertificateStatus::structural_candidate
            && !status_allows_disjoint_boundary_state)) {
        return false;
    }

    const auto& layout = state->values().layout();
    if (!layout.contains(signal_id)) {
        return false;
    }
    const auto& layout_signal = layout.signal(signal_id);
    const auto owners = layout.owners(signal_id);
    if (layout_signal.storage_class
            != SignalDriverStorageClass::disjoint_owner
        || layout_signal.width != signal.initial_value.width()
        || layout_signal.value_kind != signal.value_kind
        || owners.size() < 2U
        || driver_values[signal_id].size() != owners.size()) {
        return false;
    }
    for (const auto& owner : owners) {
        const auto* const record
            = driver_values[signal_id].find(owner.process);
        if (record == nullptr || record->strength != DriveStrength { }) {
            return false;
        }
    }
    if (eligible_state != nullptr) {
        *eligible_state = state;
    }
    return true;
}

bool Interpreter::Impl::can_try_wide_disjoint_owner_commit(
    const ProcessId process,
    const SignalId signal_id)
{
    if (!can_try_wide_disjoint_signal_commit(signal_id)
        || !process_signal_access_is_complete(process)) {
        return false;
    }
    const auto* const state
        = region_authoritative_state_for_signal(signal_id);
    if (state == nullptr) {
        return false;
    }
    const auto owners = state->values().layout().owners(signal_id);
    return std::ranges::find(owners, process,
               &SignalDriverOwnerLayout::process)
        != owners.end();
}

bool Interpreter::Impl::prepare_wide_disjoint_owner_commit_context(
    const ProcessId process,
    const SignalId signal_id,
    WideDisjointOwnerCommitContext& context)
{
    context = { };
    // Preserve the ordinary fast rejection before consulting executor code.
    if (!can_try_wide_disjoint_signal_commit(signal_id)
        || !process_signal_access_is_complete(process)) {
        return false;
    }
    // The binding accessor is virtual. Revalidate after it returns so a
    // reentrant executor cannot leave this context holding a stale graph or
    // A4 state view.
    RegionAuthoritativeComponentState* state { };
    if (!can_try_wide_disjoint_signal_commit(signal_id, &state)
        || state == nullptr) {
        return false;
    }
    const auto component
        = region_authoritative_component_by_signal[signal_id];
    const auto owners = state->values().layout().owners(signal_id);
    if (std::ranges::find(owners, process,
            &SignalDriverOwnerLayout::process)
        == owners.end()) {
        return false;
    }
    context = { process, signal_id, component, state };
    return true;
}

bool Interpreter::Impl::try_commit_wide_disjoint_value(
    const SignalId signal_id,
    PackedLogic4 value,
    const SignalChangeOrigin origin,
    const WideDisjointOwnerCommitContext* const context)
{
    if (context != nullptr) {
        if (context->signal != signal_id || context->state == nullptr) {
            return false;
        }
    } else if (!can_try_wide_disjoint_signal_commit(signal_id)) {
        return false;
    }
    if (value.width() != signals[signal_id].initial_value.width()
        || value.is_logic9()
            != (signals[signal_id].value_kind == ValueKind::logic9)) {
        return false;
    }

    const auto component = context != nullptr
        ? context->component
        : region_authoritative_component_by_signal[signal_id];
    auto* const state = context != nullptr
        ? context->state
        : region_authoritative_state_for_signal(signal_id);
    if (state == nullptr) {
        return false;
    }

    auto& mutation = state->wide_mutation_scratch();
    try {
        state->values().prepare_value_change_into(
            mutation, signal_id, value, value);
        if (!state->values().begin_prepared_publication(mutation)) {
            state->values().cancel_prepared_publication(mutation);
            demote_region_authoritative_slots(component, false);
            return false;
        }
    } catch (const std::bad_alloc&) {
        state->values().cancel_prepared_publication(mutation);
        demote_region_authoritative_slots(component, false);
        return false;
    } catch (...) {
        state->values().cancel_prepared_publication(mutation);
        throw;
    }

    const bool current_changed = mutation.any_current_changed;
    try {
        note_signal_transaction(signal_id, true, origin, true);
    } catch (...) {
        state->values().cancel_prepared_publication(mutation);
        demote_region_authoritative_slots(component, false);
        throw;
    }

    state->values().publish(std::move(mutation));
    if (!state->valid()) {
        demote_region_authoritative_slots(component, false);
        throw std::logic_error {
            "preflighted disjoint-wide A4 publication became invalid"
        };
    }
    if (current_changed) {
        refresh_direct_signal_planes(signal_id);
        publish_value_change(signal_id, true, origin);
    }
    return true;
}

void Interpreter::Impl::commit_resolved(
    const SignalId signal_id,
    PackedLogic4 value,
    const SignalChangeOrigin origin)
{
    require_region_forwarding_role_journal_flushed_for_signal(signal_id);
    require_writable_aggregate_signal(signal_id);
    const auto& signal = get_signal(signal_id);
    auto& handle = charge_decay_handles.at(signal_id);
    if (!signal.has_charge_strength) {
        commit(signal_id, std::move(value), origin);
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
        commit(signal_id, std::move(value), origin);
        return;
    }

    commit(signal_id, std::move(value), origin);
    if (!cold.charge_decay) {
        return;
    }
    auto decay = [this, signal_id, origin](Scheduler&) {
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
            commit(signal_id, resolved_driver_value(signal_id), origin);
        };
    if (origin.process_domain
        == ProcessSchedulingDomain::systemverilog) {
        handle = scheduler.schedule_systemverilog_after_cancelable(
            *cold.charge_decay, SchedulerPhase::active, signal_id,
            std::move(decay));
    } else {
        handle = scheduler.schedule_after_cancelable(
            *cold.charge_decay, SchedulerPhase::update, signal_id,
            std::move(decay));
    }
}

[[nodiscard]] PackedLogic4 Interpreter::Impl::underlying_driver_value(
    const ProcessId process,
    const SignalId signal_id) const
{
    if (get_signal(signal_id).resolution
        == ResolutionKind::none) {
        return driven_values.at(signal_id);
    }
    if (owned_driver_active(signal_id)) {
        if (process >= owned_driver_spans.size()
            || owned_driver_spans[process].signal != signal_id) {
            throw std::out_of_range(
                "process has no driver slot for SimIR signal");
        }
        return owned_driver_value(process, signal_id);
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
    const std::size_t offset,
    const SignalChangeOrigin origin)
{
    require_region_forwarding_role_journal_flushed_for_signal(signal_id);
    require_writable_aggregate_signal(signal_id);
    if (get_signal(signal_id).systemverilog_scalar
        != SystemVerilogScalarKind::None) {
        throw std::invalid_argument {
            "SimIR scalar signals do not support partial assignment"
        };
    }
    commit(
        signal_id,
        insert_value(
            driven_values[signal_id], value, offset),
        origin);
}

void Interpreter::Impl::commit_driver_slice(
    const ProcessId process,
    const SignalId signal_id,
    PackedLogic4 value,
    const std::size_t offset,
    const std::optional<SignalChangeOrigin> origin,
    const bool route_path)
{
    require_region_forwarding_role_journal_flushed_for_signal(signal_id);
    require_writable_aggregate_signal(signal_id);
    const auto change_origin = origin.value_or(
        capture_signal_change_origin(process));
    if (route_path && route_module_path_update(
            process, signal_id, value, offset,
            nullptr, 0, change_origin)) {
        return;
    }
    if (signal_id < signal_container_aggregate_aliases.size()
        && signal_container_aggregate_aliases[signal_id]) {
        const auto object
            = *signal_container_aggregate_aliases[signal_id];
        const auto& aliases = container_element_signal_aliases.at(object);
        const auto element_width
            = get_container_object(object).initial_value.type.element_width;
        if (offset > get_signal(signal_id).initial_value.width()
            || value.width()
                > get_signal(signal_id).initial_value.width() - offset) {
            throw std::invalid_argument {
                "SimIR driver slice is outside its aggregate signal"
            };
        }
        const auto end = offset + value.width();
        const auto& container = get_container_object(object).initial_value;
        const auto& type = container.type;
        const auto proxy_width
            = static_cast<std::size_t>(get_signal(signal_id).initial_value.width());
        const auto& program = processes.program_view(process);
        const bool supported_family_shape
            = type.fixed && !type.dimensions.empty()
            && type.element_kind == ContainerElementKind::Packed
            && type.element_width != 0U && !aliases.empty()
            && aliases.size() == container.elements.size()
            && proxy_width % type.element_width == 0U
            && proxy_width / type.element_width == aliases.size()
            && std::ranges::all_of(aliases, [&](const auto& alias) {
                   return alias && alias->readable && alias->writable
                       && alias->signal < signals.size()
                       && signals[alias->signal].initial_value.width()
                           == type.element_width;
               });
        if (supported_family_shape
            && can_stage_container_alias_deposit(object)
            && !program.switch_source() && !program.switch_bidirectional()
            && !program.switch_target()) {
            std::vector<std::uint8_t> selected_leaves(aliases.size());
            std::vector<PackedLogic4> leaf_values;
            leaf_values.reserve(aliases.size());
            for (const auto& alias : aliases) {
                leaf_values.push_back(driven_values.at(alias->signal));
            }

            for (std::size_t ordinal = 0U;
                ordinal < aliases.size(); ++ordinal) {
                const auto& alias = *aliases[ordinal];
                const auto base = (aliases.size() - ordinal - 1U)
                    * static_cast<std::size_t>(type.element_width);
                const auto leaf_end
                    = base + static_cast<std::size_t>(type.element_width);
                const auto begin = std::max(offset, base);
                const auto finish = std::min(end, leaf_end);
                if (begin >= finish) {
                    continue;
                }

                auto driver_value = initial_driver_value(alias.signal);
                if (owned_driver_active(alias.signal)
                    && process < owned_driver_spans.size()
                    && owned_driver_spans[process].signal == alias.signal) {
                    driver_value = owned_driver_value(
                        process, alias.signal);
                } else if (const auto* record
                    = driver_values.at(alias.signal).find(process)) {
                    driver_value = record->value;
                }
                auto fragment
                    = value.extract_bits(begin - offset, finish - begin);
                leaf_values[ordinal] = insert_value(
                    std::move(driver_value), fragment, begin - base);
                selected_leaves[ordinal] = 1U;
            }

            if (std::ranges::any_of(
                    selected_leaves, [](const std::uint8_t selected) {
                        return selected != 0U;
                    })) {
                auto prepared = prepare_container_alias_driver_family(
                    object, process, leaf_values, change_origin,
                    selected_leaves);
                begin_container_alias_driver_family(prepared);
                install_container_alias_driver_family(prepared);
                notify_container_alias_driver_family_raw(prepared);
                finish_container_alias_driver_family_raw(prepared);
                finalize_container_alias_driver_family(prepared);
                return;
            }
        }

        prepare_region_authoritative_family_write(object);
        begin_aggregate_signal_batch(signal_id, change_origin);
        try {
            for (std::size_t ordinal = 0U;
                ordinal < aliases.size(); ++ordinal) {
                const auto& alias = *aliases[ordinal];
                const auto base = (aliases.size() - ordinal - 1U)
                    * element_width;
                const auto leaf_end = base + element_width;
                const auto begin = std::max(offset, base);
                const auto finish = std::min(end, leaf_end);
                if (begin >= finish) {
                    continue;
                }
                commit_driver_slice(
                    process, alias.signal,
                    value.extract_bits(begin - offset, finish - begin),
                    begin - base, change_origin, false);
            }
        } catch (...) {
            finish_aggregate_signal_batch(signal_id);
            throw;
        }
        finish_aggregate_signal_batch(signal_id);
        return;
    }
    if (get_signal(signal_id).resolution
        == ResolutionKind::none) {
        commit_slice(
            signal_id, std::move(value), offset, change_origin);
        return;
    }
    WideDisjointOwnerCommitContext wide_disjoint_context;
    if (prepare_wide_disjoint_owner_commit_context(
            process, signal_id, wide_disjoint_context)) {
        if (try_publish_wide_owner_slice_raw(
                process, signal_id, value, offset,
                wide_disjoint_context)) {
            auto resolved = resolved_driver_value(signal_id);
            if (try_commit_wide_disjoint_value(
                    signal_id, resolved, change_origin,
                    &wide_disjoint_context)) {
                return;
            }
            prepare_region_authoritative_write(signal_id);
            commit_resolved(signal_id, std::move(resolved), change_origin);
            return;
        }
    }
    prepare_region_authoritative_write(signal_id);
    const auto updated = insert_value(
        driver_slot(process, signal_id),
        value,
        offset);
    commit_driver(
        process, signal_id, std::move(updated), change_origin, false, false);
}

} // namespace fsim::runtime::simir
