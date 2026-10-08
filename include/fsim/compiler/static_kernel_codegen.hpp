// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <filesystem>
#include <memory>

namespace fsim::runtime::simir {
class StaticKernelCodegen;
}

namespace fsim::compiler {

struct StaticKernelCodegenOptions {
    /// Object cache for code built ahead of time (empty: none).
    std::filesystem::path cache_root;
    /// Compile every template now and store it in the cache (elaborate
    /// --aot); otherwise templates found in the cache are loaded instead of
    /// compiled.
    bool ahead_of_time { };
};

/// LLVM native code generator for engine v4 static kernel templates.
[[nodiscard]] std::shared_ptr<runtime::simir::StaticKernelCodegen>
make_static_kernel_codegen(StaticKernelCodegenOptions options = { });

/// While one exists, simulations set up on this thread build their static
/// kernel code ahead of time (elaborate --aot).
class StaticKernelAheadOfTimeScope final {
public:
    StaticKernelAheadOfTimeScope() noexcept;
    ~StaticKernelAheadOfTimeScope();
    StaticKernelAheadOfTimeScope(const StaticKernelAheadOfTimeScope&) = delete;
    StaticKernelAheadOfTimeScope& operator=(
        const StaticKernelAheadOfTimeScope&) = delete;

    [[nodiscard]] static bool active() noexcept;
};

} // namespace fsim::compiler
