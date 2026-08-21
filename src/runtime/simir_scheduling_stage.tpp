// SPDX-License-Identifier: Apache-2.0

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
            auto& staged = unresolved_update_scratch[update.signal];
            const auto& current = staged
                ? *staged
                : *direct_route.value;
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
                auto& staged = unresolved_update_scratch[signal];
                const auto& current = staged ? *staged : *direct_route.value;
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
                    "fsim-profile: logic9-batch calls=%llu active_slots=%llu "
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
            // Resolved drivers must enter the ordinary staged-update path so
            // their signal is marked before the resolution phase begins.
            // Adding it from inside the direct-word commit is one phase late.
            || signals[slot.signal].resolution != ResolutionKind::none
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
        if (!signal_transaction_observed[slot.signal]
            && (changed & mask) == 0U && staged.active == 0U) {
            if (profile.enabled) {
                ++profile.unchanged_current;
            }
            *slot.mask = 0U;
            continue;
        }
        if (!signal_transaction_observed[slot.signal]
            && (changed & mask) == 0U) {
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
