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
