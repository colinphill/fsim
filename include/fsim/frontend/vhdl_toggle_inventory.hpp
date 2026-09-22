// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/elaboration/vhdl_toggle_inventory.hpp"
#include "fsim/frontend/design.hpp"

#include <span>

namespace fsim::frontend {

[[nodiscard]] bool is_vhdl_toggle_type(const Type& type) noexcept;

[[nodiscard]] elaboration::VhdlToggleInventoryResult
make_vhdl_toggle_inventory(
    const DesignUnit& architecture,
    std::span<const SignalDeclaration> ports,
    const elaboration::CoverageInventoryOwner& owner,
    std::span<const elaboration::VerilogCoverageSource> sources,
    elaboration::VhdlToggleInventoryLimits limits = { }) noexcept;

} // namespace fsim::frontend
