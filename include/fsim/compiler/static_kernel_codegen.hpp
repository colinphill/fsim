// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

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
    /// Threads a batch of templates compiles on (0: one per hardware
    /// thread).
    std::uint32_t jobs { 1U };
};

/// LLVM native code generator for engine v4 static kernel templates.
[[nodiscard]] std::shared_ptr<runtime::simir::StaticKernelCodegen>
make_static_kernel_codegen(StaticKernelCodegenOptions options = { });

/// A stable fingerprint of the host the kernel's native code targets (LLVM
/// version, triple, CPU and features). Cache and library keys include it so
/// entries built for another host are never reused.
[[nodiscard]] std::string native_host_fingerprint();

/// While one exists, simulations set up on this thread build their static
/// kernel code ahead of time (elaborate --aot), on `jobs` threads (0: one
/// per hardware thread).
class StaticKernelAheadOfTimeScope final {
public:
    explicit StaticKernelAheadOfTimeScope(std::uint32_t jobs = 1U) noexcept;
    ~StaticKernelAheadOfTimeScope();
    StaticKernelAheadOfTimeScope(const StaticKernelAheadOfTimeScope&) = delete;
    StaticKernelAheadOfTimeScope& operator=(
        const StaticKernelAheadOfTimeScope&) = delete;

    [[nodiscard]] static bool active() noexcept;
    /// The innermost scope's job count (1 outside any).
    [[nodiscard]] static std::uint32_t jobs() noexcept;

private:
    std::uint32_t previous_jobs_;
};

} // namespace fsim::compiler
