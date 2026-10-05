// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/compiler/llvm_jit_region_frontier.hpp"

namespace fsim::compiler::llvm_detail {

/// Source-private access to the built-in executor's separately checked
/// trusted entry. This is not part of the installed compiler API.
struct RegionFrontierPrivateAccess final {
    [[nodiscard]] static runtime::simir::RegionFrontierStepEntryV2
    trusted_entry(const LlvmRegionFrontierExecutor& executor) noexcept;
};

} // namespace fsim::compiler::llvm_detail
