// SPDX-License-Identifier: Apache-2.0
#pragma once

namespace llvm {
class Module;
}

namespace fsim::compiler::llvm_detail {

// Run after IR optimization and backend-tier proof validation, only for None.
// No optimizing pass may recreate the expanded intrinsics afterward.
[[nodiscard]] bool prepare_fast_isel_module(llvm::Module&);

} // namespace fsim::compiler::llvm_detail
