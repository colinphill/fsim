// SPDX-License-Identifier: Apache-2.0

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
    for (const auto object : signal_container_aliases.at(signal_id)) {
        for (const auto process : container_dynamic_fanout.at(object)) {
            queue_next_delta(process);
        }
        if (container_object_change_hook) {
            container_object_change_hook(object, scheduler.now());
        }
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
