// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "fsim/systemc/plugin_compiler.hpp"

#include "fsim/compiler/cache_support.hpp"
#include "fsim/compiler/object_cache.hpp"
#include "fsim/support/environment.hpp"
#include "fsim/support/sha256.hpp"
#include "fsim/systemc_abi.h"
#include "fsim/version.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <optional>
#include <set>
#include <span>
#include <sstream>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#if defined(_WIN32)
#  if !defined(NOMINMAX)
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  if defined(__linux__)
#    include <fcntl.h>
#  endif
#  include <sys/types.h>
#  include <sys/wait.h>
#  include <unistd.h>
#endif

namespace fsim::systemc::plugin_detail {

constexpr std::size_t kMaximumCompilerOutput = 1024U * 1024U;

extern std::atomic_uint64_t checksum_temporary_counter;
extern std::atomic_uint64_t uncached_plan_counter;
extern std::atomic_uint64_t dependency_scan_counter;
#if defined(_WIN32)
extern std::atomic_uint64_t response_temporary_counter;
#endif

struct ProcessResult {
    bool started{};
    int exit_code{-1};
    std::string output;
    std::string start_error;
};

[[nodiscard]] diagnostic::SourceSpan path_span(const std::filesystem::path& path);

void report_error(
    diagnostic::Engine& diagnostics,
    std::string code,
    std::string message,
    const std::filesystem::path& path = {});

[[nodiscard]] std::string lowercase(std::string value);

[[nodiscard]] std::string path_argument(const std::filesystem::path& path);

[[nodiscard]] std::filesystem::path make_absolute(
    const std::filesystem::path& path,
    const std::filesystem::path& working_directory);

[[nodiscard]] std::filesystem::path effective_working_directory(
    const PluginCompileRequest& request,
    std::error_code& error);

[[nodiscard]] bool contains_directory_separator(const std::string_view value) noexcept;

[[nodiscard]] std::vector<std::filesystem::path> executable_candidates(
    const std::string& executable);

[[nodiscard]] std::filesystem::path resolve_executable(
    const std::string& executable,
    const std::filesystem::path& working_directory);

[[nodiscard]] HostToolchain infer_toolchain(const std::string& compiler);

[[nodiscard]] std::string default_compiler();

[[nodiscard]] std::string shared_library_filename();

[[nodiscard]] bool source_extension_supported(const std::filesystem::path& path);

void add_sequence_to_key(
    compiler::CacheKeyBuilder& builder,
    const std::string_view label,
    const std::vector<std::string>& values);

void add_paths_to_key(
    compiler::CacheKeyBuilder& builder,
    const std::string_view label,
    const std::vector<std::filesystem::path>& values);

void add_compiler_identity(
    compiler::CacheKeyBuilder& builder,
    const std::string& compiler_name,
    const std::filesystem::path& resolved_compiler);

struct IncludeDirective {
    std::string name;
    bool quoted{};
};

[[nodiscard]] std::optional<std::string> read_text_file(
    const std::filesystem::path& path,
    std::error_code& error);

[[nodiscard]] std::string remove_cpp_comments(const std::string_view source);

struct IncludeScan {
    std::vector<IncludeDirective> includes;
    bool has_unresolved_macro_include{};
};

[[nodiscard]] std::optional<IncludeDirective> parse_include_replacement(
    const std::string_view replacement);

[[nodiscard]] IncludeScan find_includes(
    const std::string_view source,
    const std::vector<std::string>& command_defines);

[[nodiscard]] bool plausible_cpp_dependency(
    const std::filesystem::path& path);

[[nodiscard]] bool collect_conservative_root_dependencies(
    const std::vector<std::filesystem::path>& roots,
    std::vector<std::filesystem::path>& dependencies,
    diagnostic::Engine& diagnostics);

[[nodiscard]] std::filesystem::path normalized_existing_path(
    const std::filesystem::path& path,
    std::error_code& error);

[[nodiscard]] std::filesystem::path resolve_include(
    const IncludeDirective& include,
    const std::filesystem::path& including_file,
    const std::vector<std::filesystem::path>& roots);

[[nodiscard]] bool add_transitive_dependencies_to_key(
    compiler::CacheKeyBuilder& builder,
    const std::vector<std::filesystem::path>& sources,
    const std::vector<std::filesystem::path>& includes,
    const std::vector<std::string>& command_defines,
    bool& cacheable,
    diagnostic::Engine& diagnostics);

[[nodiscard]] bool add_linked_library_contents_to_key(
    compiler::CacheKeyBuilder& builder,
    const std::vector<std::string>& libraries,
    const std::filesystem::path& working_directory,
    bool& cacheable,
    diagnostic::Engine& diagnostics);

[[nodiscard]] bool option_changes_output(
    const HostToolchain toolchain,
    const std::string& option);

[[nodiscard]] bool option_hides_cache_dependencies(
    const HostToolchain toolchain,
    const std::string& option);

[[nodiscard]] bool validate_options(
    const project::SystemCSection& settings,
    const HostToolchain toolchain,
    diagnostic::Engine& diagnostics);

[[nodiscard]] std::string prefixed_define(
    const HostToolchain toolchain,
    const std::string& define);

[[nodiscard]] bool is_path_like_library(const std::string& library);

[[nodiscard]] std::vector<std::string> common_compile_argv(
    const HostToolchain toolchain,
    const std::string& compiler_name,
    const std::filesystem::path& resolved_compiler,
    const std::vector<std::filesystem::path>& includes,
    const project::SystemCSection& settings);

[[nodiscard]] std::vector<CompilerCommand> build_commands(
    const HostToolchain toolchain,
    const std::string& compiler_name,
    const std::filesystem::path& resolved_compiler,
    const std::vector<std::filesystem::path>& sources,
    const std::vector<std::filesystem::path>& includes,
    const project::SystemCSection& settings,
    const std::filesystem::path& output,
    const std::filesystem::path& working_directory,
    std::vector<std::filesystem::path>& intermediate_paths);

[[nodiscard]] bool hash_file(
    const std::filesystem::path& path,
    std::string& result,
    std::error_code& error);

[[nodiscard]] bool valid_cached_artifact(
    const std::filesystem::path& library,
    std::error_code& error);

[[nodiscard]] bool publish_artifact(
    const PluginCompilePlan& plan,
    diagnostic::Engine& diagnostics);

#if defined(_WIN32)

[[nodiscard]] std::wstring utf8_to_wide(const std::string_view input);

[[nodiscard]] std::wstring quote_windows_argument(const std::wstring& argument);

class TemporaryResponseFile final {
public:
    TemporaryResponseFile();
    TemporaryResponseFile(const TemporaryResponseFile&) = delete;
    TemporaryResponseFile& operator=(const TemporaryResponseFile&) = delete;
    ~TemporaryResponseFile();

    [[nodiscard]] const std::filesystem::path& path() const noexcept;

    bool write(
        const CompilerCommand& command,
        const std::vector<std::wstring>& wide_arguments,
        std::string& error_message);

private:
    std::filesystem::path path_;
};

[[nodiscard]] ProcessResult run_process(const CompilerCommand& command);

#else

[[nodiscard]] ProcessResult run_process(const CompilerCommand& command);

#endif

[[nodiscard]] bool shell_display_safe(const unsigned char character) noexcept;

[[nodiscard]] std::string quote_for_display(const std::string& argument);

[[nodiscard]] std::uint64_t dependency_process_id() noexcept;

class DependencyScratchDirectory final {
public:
    explicit DependencyScratchDirectory(std::filesystem::path path);

    DependencyScratchDirectory(const DependencyScratchDirectory&) = delete;
    DependencyScratchDirectory& operator=(const DependencyScratchDirectory&) = delete;
    DependencyScratchDirectory(DependencyScratchDirectory&& other) noexcept;
    DependencyScratchDirectory& operator=(DependencyScratchDirectory&&) = delete;

    ~DependencyScratchDirectory();

    [[nodiscard]] const std::filesystem::path& path() const noexcept;

private:
    std::filesystem::path path_;
};

[[nodiscard]] std::optional<DependencyScratchDirectory>
create_dependency_scratch_directory(
    const std::filesystem::path& cache_directory,
    std::error_code& error);

void report_dependency_cache_disabled(
    diagnostic::Engine& diagnostics,
    std::string message,
    const std::filesystem::path& source = {},
    const CompilerCommand* command = nullptr,
    const ProcessResult* process = nullptr);

[[nodiscard]] std::optional<std::string_view>
volatile_predefined_macro(const std::string_view contents);

struct VolatileMacroInput {
    std::filesystem::path path;
    std::string macro;
};

[[nodiscard]] std::optional<VolatileMacroInput>
find_volatile_macro_input(
    const std::vector<std::filesystem::path>& inputs);

[[nodiscard]] std::optional<std::vector<std::string>>
parse_makefile_dependencies(const std::string_view contents);

[[nodiscard]] bool add_gcc_like_dependencies_to_key(
    compiler::CacheKeyBuilder& builder,
    const std::string& compiler_name,
    const std::filesystem::path& resolved_compiler,
    const std::vector<std::filesystem::path>& sources,
    const std::vector<std::filesystem::path>& includes,
    const project::SystemCSection& settings,
    const std::filesystem::path& working_directory,
    const std::filesystem::path& cache_directory,
    bool& cacheable,
    diagnostic::Engine& diagnostics);

[[nodiscard]] bool add_compiler_dependencies_to_key(
    compiler::CacheKeyBuilder& builder,
    const HostToolchain toolchain,
    const std::string& compiler_name,
    const std::filesystem::path& resolved_compiler,
    const std::vector<std::filesystem::path>& sources,
    const std::vector<std::filesystem::path>& includes,
    const project::SystemCSection& settings,
    const std::filesystem::path& working_directory,
    const std::filesystem::path& cache_directory,
    bool& cacheable,
    diagnostic::Engine& diagnostics);


} // namespace fsim::systemc::plugin_detail
