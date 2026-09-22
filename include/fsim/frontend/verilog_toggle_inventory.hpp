// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/elaboration/verilog_toggle_inventory.hpp"
#include "fsim/frontend/design.hpp"

#include <span>

namespace fsim::frontend {

[[nodiscard]] bool is_verilog_toggle_type(const Type& type) noexcept;

[[nodiscard]] elaboration::VerilogToggleInventoryResult
make_verilog_toggle_inventory(
    const DesignUnit& unit,
    const elaboration::CoverageInventoryOwner& owner,
    std::span<const elaboration::VerilogCoverageSource> sources,
    elaboration::VerilogToggleInventoryLimits limits = { }) noexcept;

} // namespace fsim::frontend
