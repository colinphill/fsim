// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/simir_region_frontier_v2.hpp"
#include "fsim/runtime/simir_region_graph.hpp"

#include <llvm/IR/IRBuilder.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>

namespace llvm {
class Function;
class LLVMContext;
class Module;
class StructType;
class Value;
} // namespace llvm

namespace fsim::runtime::simir::scratch {

/// Internal compile-time hook for an admitted, closed member lowering. Every
/// emitted value-plane write must remain canonical for its declared kind
/// (including Logic9 reserved-code and tail-bit rules); the entry preflights
/// external inputs and initial scratch before body execution, so it does not
/// add a fallible post-mutation check before each certified commit.
using EmitCertifiedMemberBodyV2 = std::function<void(
    llvm::IRBuilder<>&, std::size_t member_index, llvm::Value* frame)>;

/// Compile-time member body for a shared native loop. Physical ProcessIds are
/// read from the immutable per-plan binding passed to the shared body.
using EmitBoundCertifiedMemberBodyV2 = std::function<void(
    llvm::IRBuilder<>&, std::size_t member_index, llvm::Value* frame,
    llvm::Value* physical_binding)>;

using EmitCertifiedInternalCommitV2 = std::function<void(
    llvm::IRBuilder<>&, llvm::Value* pending_write_index,
    llvm::Value* frame)>;

/// Construct the literal LLVM record used by private shared-body bindings:
/// { i64 certificate_generation, i64 component_generation,
///   [member_count x i32] member_process_ids,
///   [signal_slot_count x i32] signal_ids,
///   [signal_slot_count x i32] owner_process_ids }.
/// The literal type is sized by the certified structural layout and does not
/// alter the public V2 frame ABI.
[[nodiscard]] llvm::StructType* region_frontier_physical_binding_type_v2(
    llvm::LLVMContext& context, std::size_t member_count,
    std::size_t signal_slot_count);

/// Load one member's native ProcessId from a shared body's immutable binding.
/// The caller must prove that member_index is in [0, member_count).
[[nodiscard]] llvm::Value* load_region_frontier_member_process_id_v2(
    llvm::IRBuilder<>& builder, llvm::StructType* binding_type,
    llvm::Value* physical_binding, llvm::Value* member_index);

/// One exact any-change interval which can activate a private internal reader.
/// These records are compiler-private metadata and are not part of the V2 ABI.
struct RegionFrontierFanoutSensitivityRange {
    std::uint32_t offset { };
    std::uint32_t width { };
};

/// Flattened range span for one layout fanout edge, in layout edge order.
struct RegionFrontierFanoutRangeSpan {
    std::uint32_t first_range { };
    std::uint32_t range_count { };
};

/// Validate the public layout contract without constructing an emitter.
void validate_region_frontier_internal_commit_layout_v2(
    const RegionFrontierLayoutV2& layout);

[[nodiscard]] EmitCertifiedInternalCommitV2
make_region_frontier_internal_commit_emitter_v2(
    const RegionFrontierLayoutV2& layout,
    std::span<const RegionFrontierFanoutRangeSpan> edge_range_spans,
    std::span<const RegionFrontierFanoutSensitivityRange> ranges);

[[nodiscard]] llvm::Function* emit_region_frontier_loop_v2(
    llvm::Module& module, const std::string& symbol,
    const RegionFrontierLayoutV2& layout,
    const EmitCertifiedMemberBodyV2& emit_member,
    const EmitCertifiedInternalCommitV2& emit_internal_commit);

/// Emit the shared private loop body. Its second argument points to the
/// literal immutable binding described above; per-plan wrappers own that
/// binding and retain the unchanged public Status(Frame*) entry signature.
[[nodiscard]] llvm::Function* emit_region_frontier_shared_body_v2(
    llvm::Module& module, const std::string& body_symbol,
    const RegionFrontierLayoutV2& structural_layout,
    const EmitBoundCertifiedMemberBodyV2& emit_member,
    const EmitCertifiedInternalCommitV2& emit_internal_commit);

/// Emit an exact-plan Status(Frame*) wrapper which supplies its own immutable
/// native physical binding to the shared body symbol. The trusted form is an
/// in-tree SystemVerilog-only entry and skips only nested range geometry after
/// the shared body has validated its plane map; callers must authenticate the
/// current frame ranges before selecting it.
[[nodiscard]] llvm::Function* emit_region_frontier_entry_thunk_v2(
    llvm::Module& module, const std::string& wrapper_symbol,
    const std::string& body_symbol,
    const RegionFrontierLayoutV2& exact_layout,
    bool alias_prevalidated = false);

} // namespace fsim::runtime::simir::scratch
