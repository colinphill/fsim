// SPDX-License-Identifier: Apache-2.0
#include "application_simulation_internal.hpp"

#include <algorithm>
#include <memory>

namespace fsim::app {
// Included by application_simulation.cpp after Simulation::Impl is complete.

void Simulation::Impl::rebuild_signal_observer_snapshot()
{
    auto snapshot
        = std::make_shared<ObserverSnapshot<SignalChangeHook>>();
    snapshot->reserve(signal_observers.size());
    for (const auto& [token, observer] : signal_observers) {
        (void)token;
        snapshot->push_back(observer);
    }
    signal_observer_snapshot = std::move(snapshot);
    signal_observer_snapshot_dirty = false;
}

void Simulation::Impl::compact_signal_observer_snapshot() noexcept
{
    if (!signal_observer_snapshot_dirty
        || signal_observer_snapshot.use_count() != 1) {
        return;
    }
    std::erase_if(
        *signal_observer_snapshot,
        [&](const auto& observer) {
            return !signal_observers.contains(observer->token);
        });
    signal_observer_snapshot_dirty = false;
}

void Simulation::Impl::remove_signal_observer(
    const std::uint64_t token) noexcept
{
    const auto found = signal_observers.find(token);
    if (found == signal_observers.end()) {
        return;
    }
    found->second->visible_through = signal_observer_generation;
    signal_observers.erase(found);
    signal_observer_snapshot_dirty = true;
    compact_signal_observer_snapshot();
    try {
        refresh_observation_hooks();
    } catch (...) {
        // Observer removal stays noexcept; a later boundary retries refresh.
    }
}

void Simulation::Impl::rebuild_scalar_signal_observer_snapshot()
{
    auto snapshot
        = std::make_shared<ObserverSnapshot<ScalarSignalChangeHook>>();
    snapshot->reserve(scalar_signal_observers.size());
    for (const auto& [token, observer] : scalar_signal_observers) {
        (void)token;
        snapshot->push_back(observer);
    }
    scalar_signal_observer_snapshot = std::move(snapshot);
    scalar_signal_observer_snapshot_dirty = false;
}

void Simulation::Impl::compact_scalar_signal_observer_snapshot() noexcept
{
    if (!scalar_signal_observer_snapshot_dirty
        || scalar_signal_observer_snapshot.use_count() != 1) {
        return;
    }
    std::erase_if(
        *scalar_signal_observer_snapshot,
        [&](const auto& observer) {
            return !scalar_signal_observers.contains(observer->token);
        });
    scalar_signal_observer_snapshot_dirty = false;
}

const PackedLogic4& Simulation::read_signal(const SignalId signal) const
{
    return impl_->interpreter->signal_value(signal);
}

runtime::SystemVerilogScalarValue Simulation::read_scalar_signal(
    const SignalId signal) const
{
    return impl_->interpreter->scalar_signal_value(signal);
}

std::vector<runtime::simir::SystemVerilogScalarSignalSnapshot>
Simulation::scalar_signal_snapshots() const
{
    return impl_->interpreter->scalar_signal_snapshots();
}

PackedLogic4 Simulation::read_process_local(
    const runtime::simir::ProcessId process,
    const std::size_t local_index) const
{
    return impl_->interpreter->read_debug_local(process, local_index);
}

runtime::SystemVerilogScalarValue Simulation::read_process_scalar_local(
    const runtime::simir::ProcessId process,
    const std::size_t local_index) const
{
    return impl_->interpreter->read_debug_scalar_local(process, local_index);
}

void Simulation::deposit_signal(
    const SignalId signal,
    PackedLogic4 value)
{
    impl_->validate_external_value(signal, value, "deposit");
    impl_->interpreter->deposit_signal(signal, std::move(value));
    impl_->refresh_observation_hooks();
}

void Simulation::deposit_scalar_signal(
    const SignalId signal,
    runtime::SystemVerilogScalarValue value)
{
    if (value.kind == runtime::SystemVerilogScalarKind::Chandle
        && value.bits != 0) {
        value.bits = impl_->chandle_registry.alias(value.bits);
    }
    impl_->interpreter->deposit_scalar_signal(signal, value);
    impl_->refresh_observation_hooks();
}

void Simulation::force_signal(
    const SignalId signal,
    PackedLogic4 value)
{
    impl_->validate_external_value(signal, value, "force");
    impl_->interpreter->force_signal(signal, std::move(value));
    impl_->refresh_observation_hooks();
}

void Simulation::force_scalar_signal(
    const SignalId signal,
    runtime::SystemVerilogScalarValue value)
{
    if (value.kind == runtime::SystemVerilogScalarKind::Chandle
        && value.bits != 0) {
        value.bits = impl_->chandle_registry.alias(value.bits);
    }
    impl_->interpreter->force_scalar_signal(signal, value);
    impl_->refresh_observation_hooks();
}

void Simulation::set_signal_change_hook(SignalChangeHook hook)
{
    impl_->signal_change_hook = std::move(hook);
    impl_->refresh_observation_hooks();
}

std::uint64_t Simulation::add_signal_change_hook(SignalChangeHook hook)
{
    if (!hook) {
        throw std::invalid_argument("signal change observer cannot be empty");
    }
    if (impl_->next_signal_observer == 0) {
        throw std::overflow_error("signal change observer token space exhausted");
    }
    const auto token = impl_->next_signal_observer++;
    auto observer
        = std::make_shared<Impl::ObserverEntry<SignalChangeHook>>();
    observer->token = token;
    observer->callback = std::move(hook);
    observer->visible_from = impl_->signal_observer_generation.next();
    impl_->signal_observers.emplace(token, std::move(observer));
    try {
        impl_->rebuild_signal_observer_snapshot();
    } catch (...) {
        impl_->signal_observers.erase(token);
        throw;
    }
    impl_->refresh_observation_hooks();
    return token;
}

void Simulation::remove_signal_change_hook(const std::uint64_t token) noexcept
{
    impl_->remove_signal_observer(token);
}

void Simulation::set_scalar_signal_change_hook(
    ScalarSignalChangeHook hook)
{
    impl_->scalar_signal_change_hook = std::move(hook);
    impl_->refresh_observation_hooks();
}

std::uint64_t Simulation::add_scalar_signal_change_hook(
    ScalarSignalChangeHook hook)
{
    if (!hook) {
        throw std::invalid_argument { "scalar signal observer cannot be empty" };
    }
    if (impl_->next_scalar_signal_observer == 0) {
        throw std::overflow_error {
            "scalar signal observer token space exhausted"
        };
    }
    const auto token = impl_->next_scalar_signal_observer++;
    auto observer
        = std::make_shared<Impl::ObserverEntry<ScalarSignalChangeHook>>();
    observer->token = token;
    observer->callback = std::move(hook);
    observer->visible_from = impl_->scalar_signal_observer_generation.next();
    impl_->scalar_signal_observers.emplace(token, std::move(observer));
    try {
        impl_->rebuild_scalar_signal_observer_snapshot();
    } catch (...) {
        impl_->scalar_signal_observers.erase(token);
        throw;
    }
    impl_->refresh_observation_hooks();
    return token;
}

void Simulation::remove_scalar_signal_change_hook(
    const std::uint64_t token) noexcept
{
    const auto found = impl_->scalar_signal_observers.find(token);
    if (found == impl_->scalar_signal_observers.end()) {
        return;
    }
    found->second->visible_through = impl_->scalar_signal_observer_generation;
    impl_->scalar_signal_observers.erase(found);
    impl_->scalar_signal_observer_snapshot_dirty = true;
    impl_->compact_scalar_signal_observer_snapshot();
    try {
        impl_->refresh_observation_hooks();
    } catch (...) {
        // Observer removal stays noexcept; a later boundary retries refresh.
    }
}

runtime::SystemVerilogChandleRegistry&
Simulation::chandle_registry() noexcept
{
    return impl_->chandle_registry;
}

const runtime::SystemVerilogChandleRegistry&
Simulation::chandle_registry() const noexcept
{
    return impl_->chandle_registry;
}

} // namespace fsim::app
