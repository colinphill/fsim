// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/simir_region_activation.hpp"

#include <vector>

namespace fsim::runtime::simir {

/// Checked C++ reference evaluator used to compare experimental region
/// executors. It supports the same wide and Logic9 values as the runtime
/// fallback and does not schedule or publish process state.
[[nodiscard]] std::vector<PackedLogic4>
evaluate_region_activation_kernel_reference(
    const RegionConeActivationKernel& kernel,
    const RegionKernelActivationImage& image);

} // namespace fsim::runtime::simir
