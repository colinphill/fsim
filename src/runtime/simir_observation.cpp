// SPDX-License-Identifier: Apache-2.0
#include "simir_internal.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace fsim::runtime::simir {

void Interpreter::Impl::advance_process_executor_generation(
    ProcessState& process) noexcept
{
    auto& generation = process.executor_lifecycle_generation;
    if (generation == std::numeric_limits<std::uint64_t>::max()) {
        // Zero is permanently reserved as an exhausted generation. A cached
        // optional interface can never become valid again after wraparound.
        generation = 0U;
    } else if (generation != 0U) {
        ++generation;
    }
}

bool Interpreter::Impl::process_signal_access_is_complete(
    const ProcessId id) const noexcept
{
    if (id >= processes.size()) {
        return false;
    }
    if (processes.is_compact_constant(id)) {
        return true;
    }
    const auto& process = processes[id];
    const auto& program = process.program();
    if (process.executor) {
        const auto* binding
            = process.executor->program_access_binding();
        if (process.cold().fork_parent) {
            const auto* const fork_state = process.cold().fork_state.get();
            if (binding == nullptr || fork_state == nullptr
                || !fork_state->access_binding_attestation) {
                return false;
            }
            const auto& attestation
                = *fork_state->access_binding_attestation;
            return binding->same_execution_binding(attestation)
                && program.matches_forked_binding(id, attestation);
        }
        return binding != nullptr
            && program.matches_registered_binding(id, *binding);
    }
    const auto& deferred = process.cold().deferred_executor;
    if (!deferred) {
        return true;
    }
    return !deferred->access_binding_rejected
        && deferred->contract.expected_access
        && program.matches_registered_binding(
            id, *deferred->contract.expected_access);
}

void Interpreter::Impl::prepare_signal_observation(const SignalId signal)
{
    if (signal >= signals.size()) {
        throw std::out_of_range("invalid SimIR signal ID");
    }
    // Initial state is already public. A registration installed before start
    // is included in the ordinary startup observation inventory.
    if (!started) {
        return;
    }
    if (!fused_cone_materializing && signal < fused_cone_by_signal.size()
        && fused_cone_by_signal[signal]
            != std::numeric_limits<std::uint32_t>::max()) {
        // A hidden net of a fused cone becomes visible only through this
        // barrier; publish its current value before any observer reads it.
        materialize_fused_cone(fused_cone_by_signal[signal]);
    }
    if (!static_kernel_materializing
        && signal < static_kernel_owned_signal.size()
        && static_kernel_owned_signal[signal] != 0U) {
        // Kernel-owned signals are hidden until observed.
        materialize_static_kernel_signal(signal);
    }
    callback_observation_complete = false;
    promote_container_alias_authority(signal);
    materialize_direct_signal(signal);
}

void Interpreter::Impl::expose_signal_value_reference(
    const SignalId signal)
{
    auto& state = get_signal(signal);
    if (!state.public_value_reference_exposed) {
        if (started) {
            // Install the durable pin only after the observation barrier has
            // completed. A failed getter can then retry the same preparation.
            prepare_signal_observation(signal);
        }
        state.public_value_reference_exposed = true;
        advance_direct_signal_read_capability_epoch();
    }
    if (state.public_value_aliases_exposed
        || state.public_value_alias_exposure_in_progress) {
        return;
    }
    state.public_value_alias_exposure_in_progress = true;
    try {
        if (signal < signal_container_aliases.size()) {
            for (const auto object : signal_container_aliases[signal]) {
                expose_container_value_reference(object);
            }
        }
        if (signal < signal_container_element_aliases.size()
            && signal_container_element_aliases[signal]) {
            expose_container_value_reference(
                signal_container_element_aliases[signal]->first);
        }
        if (signal < signal_container_aggregate_aliases.size()
            && signal_container_aggregate_aliases[signal]) {
            expose_container_value_reference(
                *signal_container_aggregate_aliases[signal]);
        }
    } catch (...) {
        state.public_value_alias_exposure_in_progress = false;
        throw;
    }
    state.public_value_alias_exposure_in_progress = false;
    state.public_value_aliases_exposed = true;
}

void Interpreter::Impl::expose_container_value_reference(
    const ContainerObjectId object)
{
    (void)get_container_object(object);
    if (container_value_reference_exposed[object] != 0U
        || container_value_reference_exposure_in_progress[object] != 0U) {
        return;
    }
    container_value_reference_exposure_in_progress[object] = 1U;
    try {
        if (started) {
            prepare_container_value_observation(object);
        }
        // Materialize the initial projection before pinning it. Once exposed,
        // reads return the same backing without lazily replacing its elements.
        (void)read_container_object_value(object);

        // A callback may first expose this backing while one or more nested
        // publications have already preflighted their current phase. Give
        // every such frame an independent scratch projection before returning
        // the reference; each frame will consume its own reservation in LIFO
        // order.
        prepare_active_container_value_reference_refresh(object);

        const auto& container = get_container_object(object);
        if (object < container_signal_aliases.size()
            && container_signal_aliases[object]) {
            expose_signal_value_reference(
                container_signal_aliases[object]->signal);
        }
        if (object < container_element_signal_aliases.size()) {
            for (const auto& alias : container_element_signal_aliases[object]) {
                if (alias) {
                    expose_signal_value_reference(alias->signal);
                }
            }
        }
        if (object < container_aggregate_signal_aliases.size()
            && container_aggregate_signal_aliases[object]) {
            expose_signal_value_reference(
                container_aggregate_signal_aliases[object]->signal);
        }
        if (container.slice_alias) {
            expose_container_value_reference(
                container.slice_alias->object);
        }

        auto& exposed = exposed_container_value_references;
        const auto position = std::ranges::lower_bound(exposed, object);
        if (position == exposed.end() || *position != object) {
            exposed.insert(position, object);
        }
        container_value_reference_exposed[object] = 1U;
    } catch (...) {
        container_value_reference_exposure_in_progress[object] = 0U;
        throw;
    }
    container_value_reference_exposure_in_progress[object] = 0U;
}

void Interpreter::Impl::prepare_container_value_observation(
    const ContainerObjectId object)
{
    auto selected = object;
    for (std::size_t depth = 0U; depth < container_objects.size(); ++depth) {
        const auto& container = get_container_object(selected);
        if (selected < container_signal_aliases.size()
            && container_signal_aliases[selected]) {
            prepare_signal_observation(
                container_signal_aliases[selected]->signal);
        }
        if (selected < container_element_signal_aliases.size()) {
            for (const auto& alias : container_element_signal_aliases[selected]) {
                if (alias) {
                    prepare_signal_observation(alias->signal);
                }
            }
        }
        if (selected < container_aggregate_signal_aliases.size()
            && container_aggregate_signal_aliases[selected]) {
            prepare_signal_observation(
                container_aggregate_signal_aliases[selected]->signal);
        }
        if (!container.slice_alias) {
            return;
        }
        selected = container.slice_alias->object;
    }
    throw std::logic_error("container slice alias chain is cyclic");
}

void Interpreter::Impl::prepare_callback_observation()
{
    if (!started) {
        return;
    }
    if (callback_observation_complete) {
        return;
    }
    // Leave the flag clear until every signal has completed successfully; a
    // failed preparation must retry the complete barrier next time.
    callback_observation_complete = false;
    // A user callback can inspect or mutate any signal through the public
    // interpreter API. Prepare each signal before entering it, preserving the
    // ordinary pending scheduler queue instead of publishing early. The
    // completion flag stays set until a later single-signal observation
    // clears it.
    for (SignalId signal = 0U; signal < signals.size(); ++signal) {
        prepare_signal_observation(signal);
    }
    callback_observation_complete = true;
}

void Interpreter::Impl::OutputCallback::operator()(
    const ProcessId process, const std::string_view text,
    const bool newline, const SimulationTick time,
    const std::uint64_t delta) const
{
    const auto selected = callback;
    if (!selected) {
        return;
    }
    if (owner == nullptr) {
        throw std::logic_error("output callback has no interpreter owner");
    }
    if (!trusted_text_only) {
        owner->prepare_callback_observation();
    }
    (*selected)(process, text, newline, time, delta);
}

void Interpreter::Impl::ReportCallback::operator()(
    const ProcessId process, const std::string_view message,
    const AssertionSeverity severity, const SourceLocation& source,
    const SimulationTick time, const std::uint64_t delta) const
{
    const auto selected = callback;
    if (!selected) {
        return;
    }
    if (owner == nullptr) {
        throw std::logic_error("report callback has no interpreter owner");
    }
    if (!trusted_text_only) {
        owner->prepare_callback_observation();
    }
    (*selected)(process, message, severity, source, time, delta);
}

void Interpreter::prepare_signal_observation(const SignalId signal)
{
    impl_->prepare_signal_observation(signal);
}

} // namespace fsim::runtime::simir
