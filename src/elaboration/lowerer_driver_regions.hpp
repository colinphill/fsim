// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/simir.hpp"

#include <span>
#include <vector>

namespace fsim::elaboration::elaboration_detail {
std::vector<runtime::simir::Process::DriverRegion>
collect_driver_regions(
    const runtime::simir::Process& process,
    std::span<const std::size_t> register_widths);

}  // namespace fsim::elaboration::elaboration_detail
