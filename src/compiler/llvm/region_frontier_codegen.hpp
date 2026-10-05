// SPDX-License-Identifier: Apache-2.0
// Shared compile-time emitter hooks for the scratch native frontier.
#pragma once

#include "fsim/runtime/simir_region_frontier.hpp"
#include "fsim/runtime/simir_region_graph.hpp"

#include <llvm/IR/IRBuilder.h>

#include <cstddef>
#include <functional>
#include <string>

namespace llvm {
class Function;
class Module;
} // namespace llvm

namespace fsim::runtime::simir::scratch {

using EmitCertifiedMemberBody = std::function<void(
    llvm::IRBuilder<>&, std::size_t member_index, llvm::Value* frame)>;

using EmitCertifiedInternalCommit = std::function<void(
    llvm::IRBuilder<>&, llvm::Value* pending_write_index,
    llvm::Value* frame)>;

[[nodiscard]] EmitCertifiedInternalCommit
make_region_frontier_internal_commit_emitter(
    const RegionFrontierLayoutV1& layout);

[[nodiscard]] llvm::Function* emit_region_frontier_loop(
    llvm::Module& module, const std::string& symbol,
    const RegionFrontierLayoutV1& layout,
    const EmitCertifiedMemberBody& emit_member,
    const EmitCertifiedInternalCommit& emit_internal_commit);

} // namespace fsim::runtime::simir::scratch
