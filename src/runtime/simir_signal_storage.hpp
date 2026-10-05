// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/simir.hpp"

#include <optional>
#include <string>

namespace fsim::runtime::simir {

// Keep one value of each record per SignalId, appended to its corresponding
// vector in the same order. Public Signal remains the add_signal descriptor.
struct SignalHot {
    PackedLogic4 initial_value;
    ResolutionKind resolution { ResolutionKind::none };
    ValueKind value_kind { ValueKind::logic4 };
    SystemVerilogScalarKind systemverilog_scalar {
        SystemVerilogScalarKind::None };
    bool event_variable { };
    bool has_implicit_driver { };
    bool has_charge_strength { };
    // Public const-reference getters can escape beyond the next callback or
    // scheduler boundary. Keep any such value on ordinary public storage.
    bool public_value_reference_exposed { };
    bool public_value_alias_exposure_in_progress { };
    bool public_value_aliases_exposed { };
};

struct SignalCold {
    std::string name;
    std::optional<Logic4> implicit_driver;
    DriveStrength implicit_drive_strength {
        StrengthRank::pull, StrengthRank::pull };
    std::optional<StrengthRank> charge_strength;
    std::optional<SimulationTick> charge_decay;
};

} // namespace fsim::runtime::simir
