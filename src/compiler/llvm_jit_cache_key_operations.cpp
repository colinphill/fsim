// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_cache_key_internal.hpp"

namespace fsim::compiler::llvm_detail {

void add_operation_cache_keys(
    CacheKeyBuilder& builder,
    const runtime::simir::Process& process,
    const std::span<const std::uint32_t> signal_widths)
{
    add_key_u64(builder, "operation-count", process.operations.size());
    for (const auto& operation : process.operations) {
        add_primary_operation_cache_key(builder, operation, signal_widths);
        add_secondary_operation_cache_key(
            builder, process, operation, signal_widths);
    }
}

} // namespace fsim::compiler::llvm_detail
