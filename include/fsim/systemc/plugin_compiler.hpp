// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/diagnostic/diagnostic.hpp"
#include "fsim/project/project.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::systemc {

enum class HostToolchain : std::uint8_t {
    gcc_like,
    msvc,
};

struct PluginCompileRequest {
    std::vector<std::filesystem::path> sources;
    std::string logical_library;
    project::SystemCSection settings;
    std::filesystem::path working_directory;
    std::filesystem::path cache_directory{".fsim-cache"};
};

struct CompilerCommand {
    std::vector<std::string> argv;
    std::filesystem::path working_directory;
    HostToolchain toolchain{HostToolchain::gcc_like};
};

struct PluginCompilePlan {
    HostToolchain toolchain{HostToolchain::gcc_like};
    // False when the selected compiler inputs cannot be exhaustively
    // discovered. GCC-like compilers are queried for their complete dependency
    // closure, including implicit system headers. MSVC uses compiler-emitted
    // source-dependency JSON and falls back to a conservative scanner that
    // disables reuse when an include cannot be resolved from manifest roots.
    // Raw response/options also disable reuse.
    // Non-cacheable plans use a unique key and are always rebuilt.
    bool cacheable{true};
    std::string cache_key;
    // Source/path-independent identity of the compiler binary, environment,
    // ABI-affecting options, runtime ABI, host format, and fiber backend.
    // This is suitable for exact admission of a precompiled plug-in whose
    // payload is separately content-addressed.
    std::string host_fingerprint;
    std::filesystem::path library_path;
    std::filesystem::path build_path;
    // GCC-like toolchains use one compile/link command. MSVC-compatible
    // toolchains compile every source to a uniquely named object and then
    // invoke one link command, preventing same-basename source collisions.
    std::vector<CompilerCommand> commands;
    std::vector<std::filesystem::path> intermediate_paths;
};

struct PluginCompileResult {
    bool success{};
    bool cache_hit{};
    std::filesystem::path library_path;
    std::string cache_key;
    std::string host_fingerprint;
    int compiler_exit_code{-1};
    std::string compiler_output;
};

// Constructs the compiler invocation without invoking a command shell. For a
// cacheable plan, this may directly invoke the selected compiler to fingerprint
// the complete GNU-style or MSVC transitive dependency closure.
// Relative source/include/cache paths are interpreted from working_directory
// (or the process working directory when it is empty).
[[nodiscard]] std::optional<PluginCompilePlan> plan_plugin_compile(
    const PluginCompileRequest& request,
    diagnostic::Engine& diagnostics);

// Computes the source/path-independent portion of a plug-in plan. This is the
// exact compiler/runtime/host admission identity recorded by .fsimlib native
// variants. Failure to hash the selected compiler returns no identity.
[[nodiscard]] std::optional<std::string> plugin_host_fingerprint(
    const project::SystemCSection& settings,
    const std::filesystem::path& working_directory,
    diagnostic::Engine& diagnostics);

// Compiles all sources into one cached host shared library. Concurrent
// processes compiling the same key synchronize through a per-key directory
// lock. A versioned key/size/checksum commit record prevents reuse of missing,
// incomplete, incompatible, or corrupt artifacts.
[[nodiscard]] PluginCompileResult compile_plugin(
    const PluginCompileRequest& request,
    diagnostic::Engine& diagnostics);

// Human-readable rendering for diagnostics only. Execution always uses the
// argv vector directly and never evaluates this string in a shell.
[[nodiscard]] std::string format_compiler_command(const CompilerCommand& command);

[[nodiscard]] std::string_view to_string(HostToolchain toolchain) noexcept;

} // namespace fsim::systemc
