// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "../../src/runtime/simir_a4_signal_state.hpp"
#include "../../src/runtime/simir_internal.hpp"

#include <array>
#include <stdexcept>

namespace fsim::runtime::simir {

struct OwnedDriverDemotionTestAccess {
    using Implementation = Interpreter::Impl;
    using PreparedOutputBatchState
        = Interpreter::Impl::RegionPreparedOutputBatchState;

    static auto& implementation(Interpreter& interpreter)
    {
        return *interpreter.impl_;
    }

    static bool packed_a4_slots_bound(
        Interpreter& interpreter, const SignalId signal,
        const ProcessId owner)
    {
        auto* const state
            = implementation(interpreter).region_authoritative_state_for_signal(
                signal);
        return state != nullptr
            && state->values().packed_signal_slots_bound(signal)
            && state->values().packed_owner_slot_bound(signal, owner);
    }

    static std::array<PackedLogic4, 4U> packed_a4_values(
        Interpreter& interpreter, const SignalId signal,
        const ProcessId owner)
    {
        auto* const state
            = implementation(interpreter).region_authoritative_state_for_signal(
                signal);
        if (state == nullptr) {
            throw std::runtime_error {
                "Logic9 runtime has no authoritative component state"
            };
        }
        const auto& impl = implementation(interpreter);
        const auto* const driver = impl.driver_values.at(signal).find(owner);
        if (driver == nullptr) {
            throw std::runtime_error {
                "authoritative runtime signal has no original owner"
            };
        }
        // Copy the runtime slots so wide snapshots pin their actual role blocks.
        // Materialized plane values would not exercise publication detachment.
        return { impl.signals.at(signal).initial_value,
            impl.signal_last_values.at(signal), impl.driven_values.at(signal),
            driver->value };
    }
};

} // namespace fsim::runtime::simir
