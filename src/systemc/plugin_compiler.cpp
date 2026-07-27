// SPDX-License-Identifier: Apache-2.0
#include "fsim/systemc/plugin_compiler.hpp"

#include "fsim/compiler/cache_support.hpp"
#include "fsim/compiler/object_cache.hpp"
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

namespace fsim::systemc {
namespace {

constexpr std::size_t kMaximumCompilerOutput = 1024U * 1024U;
std::atomic_uint64_t checksum_temporary_counter{};
std::atomic_uint64_t uncached_plan_counter{};
std::atomic_uint64_t dependency_scan_counter{};
#if defined(_WIN32)
std::atomic_uint64_t response_temporary_counter{};
#endif

struct ProcessResult {
    bool started{};
    int exit_code{-1};
    std::string output;
    std::string start_error;
};

[[nodiscard]] diagnostic::SourceSpan path_span(const std::filesystem::path& path) {
    diagnostic::SourceSpan span;
    span.path = path.generic_string();
    return span;
}

void report_error(
    diagnostic::Engine& diagnostics,
    std::string code,
    std::string message,
    const std::filesystem::path& path = {}) {
    diagnostics.error(std::move(code), std::move(message), path_span(path));
}

[[nodiscard]] std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

[[nodiscard]] std::string path_argument(const std::filesystem::path& path) {
#if defined(_WIN32)
    const auto encoded = path.u8string();
    return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
#else
    return path.string();
#endif
}

[[nodiscard]] std::filesystem::path make_absolute(
    const std::filesystem::path& path,
    const std::filesystem::path& working_directory) {
    if (path.is_absolute()) {
        return path.lexically_normal();
    }
    return (working_directory / path).lexically_normal();
}

[[nodiscard]] std::filesystem::path effective_working_directory(
    const PluginCompileRequest& request,
    std::error_code& error) {
    error.clear();
    if (!request.working_directory.empty()) {
        auto result = request.working_directory;
        if (!result.is_absolute()) {
            result = std::filesystem::absolute(result, error);
            if (error) {
                return {};
            }
        }
        return result.lexically_normal();
    }
    return std::filesystem::current_path(error).lexically_normal();
}

[[nodiscard]] bool contains_directory_separator(const std::string_view value) noexcept {
    return value.find('/') != std::string_view::npos
        || value.find('\\') != std::string_view::npos;
}

[[nodiscard]] std::vector<std::filesystem::path> executable_candidates(
    const std::string& executable) {
    std::vector<std::filesystem::path> result;
    if (contains_directory_separator(executable)) {
        result.emplace_back(executable);
        return result;
    }

    const char* raw_path = std::getenv("PATH");
    if (raw_path == nullptr) {
        return result;
    }
#if defined(_WIN32)
    constexpr char separator = ';';
#else
    constexpr char separator = ':';
#endif
    std::string_view search_path{raw_path};
    std::size_t begin = 0;
    while (begin <= search_path.size()) {
        const auto end = search_path.find(separator, begin);
        auto directory = search_path.substr(
            begin, end == std::string_view::npos ? search_path.size() - begin : end - begin);
        if (directory.empty()) {
            directory = ".";
        }
        auto candidate = std::filesystem::path{std::string{directory}} / executable;
        result.push_back(candidate);
#if defined(_WIN32)
        if (!candidate.has_extension()) {
            result.push_back(candidate.string() + ".exe");
        }
#endif
        if (end == std::string_view::npos) {
            break;
        }
        begin = end + 1;
    }
    return result;
}

[[nodiscard]] std::filesystem::path resolve_executable(
    const std::string& executable,
    const std::filesystem::path& working_directory) {
    std::error_code error;
    for (auto candidate : executable_candidates(executable)) {
        if (!candidate.is_absolute()) {
            candidate = make_absolute(candidate, working_directory);
        }
        if (std::filesystem::is_regular_file(candidate, error)) {
#if !defined(_WIN32)
            if (::access(candidate.c_str(), X_OK) != 0) {
                error.clear();
                continue;
            }
#endif
            // Preserve the executable spelling used by the caller. Compiler
            // drivers such as clang and clang++ may be symlinks to one binary
            // but select different link behavior from argv[0].
            return candidate.lexically_normal();
        }
        error.clear();
    }
    return {};
}

[[nodiscard]] HostToolchain infer_toolchain(const std::string& compiler) {
    const auto filename = lowercase(std::filesystem::path{compiler}.filename().string());
    if (filename == "cl" || filename == "cl.exe" || filename == "clang-cl"
        || filename == "clang-cl.exe") {
        return HostToolchain::msvc;
    }
    return HostToolchain::gcc_like;
}

[[nodiscard]] std::string default_compiler() {
#if defined(_WIN32)
    return "cl.exe";
#else
    return "c++";
#endif
}

[[nodiscard]] std::string shared_library_filename() {
#if defined(_WIN32)
    return "plugin.dll";
#else
    return "plugin.so";
#endif
}

[[nodiscard]] bool source_extension_supported(const std::filesystem::path& path) {
    const auto extension = lowercase(path.extension().string());
    return extension == ".cpp" || extension == ".cc" || extension == ".cxx";
}

void add_sequence_to_key(
    compiler::CacheKeyBuilder& builder,
    const std::string_view label,
    const std::vector<std::string>& values) {
    builder.add(std::string{label} + ".count", std::to_string(values.size()));
    for (std::size_t index = 0; index < values.size(); ++index) {
        builder.add(
            std::string{label} + "." + std::to_string(index),
            values[index]);
    }
}

void add_paths_to_key(
    compiler::CacheKeyBuilder& builder,
    const std::string_view label,
    const std::vector<std::filesystem::path>& values) {
    builder.add(std::string{label} + ".count", std::to_string(values.size()));
    for (std::size_t index = 0; index < values.size(); ++index) {
        builder.add(
            std::string{label} + "." + std::to_string(index),
            values[index].generic_string());
    }
}

void add_compiler_identity(
    compiler::CacheKeyBuilder& builder,
    const std::string& compiler_name,
    const std::filesystem::path& resolved_compiler) {
    builder.add("compiler.requested", compiler_name);
    if (resolved_compiler.empty()) {
        builder.add("compiler.resolved", "<unresolved>");
        return;
    }

    builder.add("compiler.resolved", resolved_compiler.generic_string());
    std::error_code error;
    if (builder.add_file("compiler.binary", resolved_compiler, error)) {
        return;
    }
    error.clear();
    const auto size = std::filesystem::file_size(resolved_compiler, error);
    if (!error) {
        builder.add("compiler.size", std::to_string(size));
    }
    error.clear();
    const auto stamp = std::filesystem::last_write_time(resolved_compiler, error);
    if (!error) {
        builder.add("compiler.mtime", std::to_string(stamp.time_since_epoch().count()));
    }
}

struct IncludeDirective {
    std::string name;
    bool quoted{};
};

[[nodiscard]] std::optional<std::string> read_text_file(
    const std::filesystem::path& path,
    std::error_code& error) {
    error.clear();
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        error = std::make_error_code(std::errc::io_error);
        return std::nullopt;
    }
    std::ostringstream contents;
    contents << stream.rdbuf();
    if (!stream.eof() && stream.fail()) {
        error = std::make_error_code(std::errc::io_error);
        return std::nullopt;
    }
    return contents.str();
}

[[nodiscard]] std::string remove_cpp_comments(const std::string_view source) {
    enum class State : std::uint8_t {
        normal,
        line_comment,
        block_comment,
        string_literal,
        character_literal,
    };
    State state = State::normal;
    bool escaped = false;
    std::string result;
    result.reserve(source.size());
    for (std::size_t index = 0; index < source.size(); ++index) {
        const char character = source[index];
        const char next = index + 1 < source.size() ? source[index + 1] : '\0';
        switch (state) {
        case State::normal:
            if (character == '/' && next == '/') {
                state = State::line_comment;
                result += "  ";
                ++index;
            } else if (character == '/' && next == '*') {
                state = State::block_comment;
                result += "  ";
                ++index;
            } else {
                result.push_back(character);
                if (character == '"') {
                    state = State::string_literal;
                    escaped = false;
                } else if (character == '\'') {
                    state = State::character_literal;
                    escaped = false;
                }
            }
            break;
        case State::line_comment:
            if (character == '\n') {
                result.push_back(character);
                state = State::normal;
            } else {
                result.push_back(' ');
            }
            break;
        case State::block_comment:
            if (character == '*' && next == '/') {
                result += "  ";
                ++index;
                state = State::normal;
            } else {
                result.push_back(character == '\n' ? '\n' : ' ');
            }
            break;
        case State::string_literal:
        case State::character_literal: {
            result.push_back(character);
            const auto delimiter =
                state == State::string_literal ? '"' : '\'';
            if (!escaped && character == delimiter) {
                state = State::normal;
            }
            if (character == '\\' && !escaped) {
                escaped = true;
            } else {
                escaped = false;
            }
            break;
        }
        }
    }
    return result;
}

struct IncludeScan {
    std::vector<IncludeDirective> includes;
    bool has_unresolved_macro_include{};
};

[[nodiscard]] std::optional<IncludeDirective> parse_include_replacement(
    const std::string_view replacement) {
    std::size_t begin = 0;
    while (begin < replacement.size()
           && std::isspace(static_cast<unsigned char>(replacement[begin])) != 0) {
        ++begin;
    }
    if (begin >= replacement.size()
        || (replacement[begin] != '"' && replacement[begin] != '<')) {
        return std::nullopt;
    }
    const bool quoted = replacement[begin] == '"';
    const char close = quoted ? '"' : '>';
    const auto end = replacement.find(close, begin + 1);
    if (end == std::string_view::npos) {
        return std::nullopt;
    }
    return IncludeDirective{
        std::string{replacement.substr(begin + 1, end - begin - 1)}, quoted};
}

[[nodiscard]] IncludeScan find_includes(
    const std::string_view source,
    const std::vector<std::string>& command_defines) {
    const auto without_comments = remove_cpp_comments(source);
    std::unordered_map<std::string, std::string> macros;
    bool ambiguous_macro_state = false;
    for (const auto& define : command_defines) {
        auto name_end = define.find('=');
        if (name_end == std::string::npos) {
            name_end = define.size();
        }
        auto name = define.substr(0, name_end);
        if (name.rfind("-D", 0) == 0 || name.rfind("/D", 0) == 0) {
            name.erase(0, 2);
        }
        if (!name.empty()) {
            const auto replacement =
                name_end == define.size() ? std::string{"1"} : define.substr(name_end + 1);
            if (const auto found = macros.find(name);
                found != macros.end() && found->second != replacement) {
                ambiguous_macro_state = true;
            }
            macros[name] = replacement;
        }
    }

    std::vector<std::string> include_replacements;
    std::size_t line_begin = 0;
    while (line_begin <= without_comments.size()) {
        const auto line_end = without_comments.find('\n', line_begin);
        const auto line = std::string_view{without_comments}.substr(
            line_begin,
            line_end == std::string::npos
                ? without_comments.size() - line_begin
                : line_end - line_begin);
        std::size_t position = 0;
        const auto skip_space = [&]() {
            while (position < line.size()
                   && std::isspace(static_cast<unsigned char>(line[position])) != 0) {
                ++position;
            }
        };
        skip_space();
        if (position < line.size() && line[position] == '#') {
            ++position;
            skip_space();
            const auto directive_begin = position;
            while (position < line.size()
                   && (std::isalnum(static_cast<unsigned char>(line[position])) != 0
                       || line[position] == '_')) {
                ++position;
            }
            const auto directive = line.substr(
                directive_begin, position - directive_begin);
            skip_space();
            if (directive == "define") {
                const auto name_begin = position;
                while (position < line.size()
                       && (std::isalnum(static_cast<unsigned char>(line[position])) != 0
                           || line[position] == '_')) {
                    ++position;
                }
                if (position > name_begin
                    && (position == line.size() || line[position] != '(')) {
                    const auto name =
                        std::string{line.substr(name_begin, position - name_begin)};
                    skip_space();
                    const auto replacement = std::string{line.substr(position)};
                    if (const auto found = macros.find(name);
                        found != macros.end() && found->second != replacement) {
                        ambiguous_macro_state = true;
                    }
                    macros[name] = replacement;
                }
            } else if (directive == "undef") {
                const auto name_begin = position;
                while (position < line.size()
                       && (std::isalnum(static_cast<unsigned char>(line[position])) != 0
                           || line[position] == '_')) {
                    ++position;
                }
                const auto name =
                    std::string{line.substr(name_begin, position - name_begin)};
                ambiguous_macro_state =
                    ambiguous_macro_state || macros.contains(name);
                macros.erase(name);
            } else if (directive == "include") {
                include_replacements.emplace_back(line.substr(position));
            } else if (directive == "include_next") {
                // include_next search starts after the directory that supplied
                // this file. Hash all candidate roots rather than pretending a
                // normal include has equivalent resolution.
                include_replacements.emplace_back();
                include_replacements.emplace_back(line.substr(position));
            }
        }
        if (line_end == std::string::npos) {
            break;
        }
        line_begin = line_end + 1;
    }

    IncludeScan result;
    result.has_unresolved_macro_include = ambiguous_macro_state;
    for (auto replacement : include_replacements) {
        if (replacement.empty()) {
            result.has_unresolved_macro_include = true;
            continue;
        }
        std::unordered_set<std::string> expanded_names;
        for (std::size_t expansion = 0; expansion < 64; ++expansion) {
            if (const auto include = parse_include_replacement(replacement)) {
                result.includes.push_back(*include);
                replacement.clear();
                break;
            }
            std::size_t begin = 0;
            while (begin < replacement.size()
                   && std::isspace(static_cast<unsigned char>(replacement[begin])) != 0) {
                ++begin;
            }
            std::size_t end = begin;
            while (end < replacement.size()
                   && (std::isalnum(static_cast<unsigned char>(replacement[end])) != 0
                       || replacement[end] == '_')) {
                ++end;
            }
            const auto name = replacement.substr(begin, end - begin);
            const auto found = macros.find(name);
            if (name.empty() || found == macros.end()
                || !expanded_names.insert(name).second) {
                result.has_unresolved_macro_include = true;
                replacement.clear();
                break;
            }
            replacement = found->second;
        }
        if (!replacement.empty()) {
            result.has_unresolved_macro_include = true;
        }
    }
    return result;
}

[[nodiscard]] bool plausible_cpp_dependency(
    const std::filesystem::path& path) {
    const auto extension = lowercase(path.extension().string());
    return extension.empty() || extension == ".h" || extension == ".hh"
        || extension == ".hpp" || extension == ".hxx" || extension == ".inc"
        || extension == ".ipp" || extension == ".tpp" || extension == ".c"
        || extension == ".cc" || extension == ".cpp" || extension == ".cxx";
}

[[nodiscard]] bool collect_conservative_root_dependencies(
    const std::vector<std::filesystem::path>& roots,
    std::vector<std::filesystem::path>& dependencies,
    diagnostic::Engine& diagnostics) {
    std::error_code error;
    for (const auto& root : roots) {
        if (!std::filesystem::is_directory(root, error)) {
            error.clear();
            continue;
        }
        std::filesystem::recursive_directory_iterator iterator{
            root, std::filesystem::directory_options::skip_permission_denied, error};
        const std::filesystem::recursive_directory_iterator end;
        while (!error && iterator != end) {
            if (iterator->is_regular_file(error)
                && plausible_cpp_dependency(iterator->path())) {
                auto normalized =
                    std::filesystem::weakly_canonical(iterator->path(), error);
                if (error) {
                    error.clear();
                    normalized = iterator->path().lexically_normal();
                }
                dependencies.push_back(std::move(normalized));
            }
            iterator.increment(error);
        }
        if (error) {
            report_error(
                diagnostics,
                "FSIM-SC-C003",
                "cannot scan SystemC include directory: " + error.message(),
                root);
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::filesystem::path normalized_existing_path(
    const std::filesystem::path& path,
    std::error_code& error) {
    error.clear();
    if (!std::filesystem::is_regular_file(path, error)) {
        error.clear();
        return {};
    }
    auto canonical = std::filesystem::weakly_canonical(path, error);
    if (error) {
        error.clear();
        return path.lexically_normal();
    }
    return canonical;
}

[[nodiscard]] std::filesystem::path resolve_include(
    const IncludeDirective& include,
    const std::filesystem::path& including_file,
    const std::vector<std::filesystem::path>& roots) {
    std::error_code error;
    if (include.quoted) {
        auto candidate =
            normalized_existing_path(including_file.parent_path() / include.name, error);
        if (!candidate.empty()) {
            return candidate;
        }
    }
    for (const auto& root : roots) {
        auto candidate = normalized_existing_path(root / include.name, error);
        if (!candidate.empty()) {
            return candidate;
        }
    }
    return {};
}

[[nodiscard]] bool add_transitive_dependencies_to_key(
    compiler::CacheKeyBuilder& builder,
    const std::vector<std::filesystem::path>& sources,
    const std::vector<std::filesystem::path>& includes,
    const std::vector<std::string>& command_defines,
    bool& cacheable,
    diagnostic::Engine& diagnostics) {
    std::vector<std::filesystem::path> include_roots;
    std::unordered_set<std::string> include_root_names;
    const auto append_include_root = [&](const std::filesystem::path& root) {
        const auto normalized = root.lexically_normal();
        if (include_root_names.insert(normalized.generic_string()).second) {
            include_roots.push_back(normalized);
        }
    };
    for (const auto& include : includes) {
        append_include_root(include);
    }
    std::vector<std::filesystem::path> conservative_scan_roots =
        include_roots;
    std::unordered_set<std::string> scan_root_names =
        include_root_names;
    const auto append_scan_root = [&](const std::filesystem::path& root) {
        const auto normalized = root.lexically_normal();
        if (scan_root_names.insert(normalized.generic_string()).second) {
            conservative_scan_roots.push_back(normalized);
        }
    };
    for (const auto& source : sources) {
        append_scan_root(source.parent_path());
    }

    std::unordered_set<std::string> source_names;
    std::vector<std::filesystem::path> pending;
    pending.reserve(sources.size());
    for (const auto& source : sources) {
        std::error_code ignored;
        auto normalized = std::filesystem::weakly_canonical(source, ignored);
        if (ignored) {
            normalized = source.lexically_normal();
        }
        source_names.insert(normalized.generic_string());
        pending.push_back(std::move(normalized));
    }

    std::set<std::string> visited;
    std::vector<std::filesystem::path> dependencies;
    bool needs_conservative_scan = false;
    while (!pending.empty()) {
        auto current = std::move(pending.back());
        pending.pop_back();
        if (!visited.insert(current.generic_string()).second) {
            continue;
        }
        std::error_code error;
        const auto text = read_text_file(current, error);
        if (!text) {
            report_error(
                diagnostics,
                "FSIM-SC-C003",
                "cannot read SystemC source dependency: " + error.message(),
                current);
            return false;
        }
        const auto scan = find_includes(*text, command_defines);
        needs_conservative_scan =
            needs_conservative_scan || scan.has_unresolved_macro_include;
        cacheable = cacheable && !scan.has_unresolved_macro_include;
        for (const auto& include : scan.includes) {
            auto resolved =
                resolve_include(include, current, include_roots);
            if (resolved.empty()) {
                // The selected compiler may find this in an implicit directory,
                // which this portable scanner cannot fingerprint soundly.
                cacheable = false;
                continue;
            }
            if (visited.contains(resolved.generic_string())) {
                continue;
            }
            if (!source_names.contains(resolved.generic_string())) {
                dependencies.push_back(resolved);
            }
            pending.push_back(std::move(resolved));
        }
    }
    if (needs_conservative_scan
        && !collect_conservative_root_dependencies(
            conservative_scan_roots, dependencies, diagnostics)) {
        return false;
    }

    std::sort(
        dependencies.begin(), dependencies.end(), [](const auto& left, const auto& right) {
            return left.generic_string() < right.generic_string();
        });
    dependencies.erase(
        std::unique(
            dependencies.begin(), dependencies.end(), [](const auto& left, const auto& right) {
                return left == right;
            }),
        dependencies.end());
    builder.add("dependency.count", std::to_string(dependencies.size()));
    std::error_code error;
    for (std::size_t index = 0; index < dependencies.size(); ++index) {
        if (!builder.add_file(
                "dependency." + std::to_string(index), dependencies[index], error)) {
            report_error(
                diagnostics,
                "FSIM-SC-C003",
                "cannot hash SystemC source dependency: " + error.message(),
                dependencies[index]);
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool add_linked_library_contents_to_key(
    compiler::CacheKeyBuilder& builder,
    const std::vector<std::string>& libraries,
    const std::filesystem::path& working_directory,
    bool& cacheable,
    diagnostic::Engine& diagnostics) {
    std::error_code error;
    std::size_t content_index = 0;
    for (const auto& library : libraries) {
        if (library.empty() || library.front() == '-') {
            cacheable = false;
            continue;
        }
#if defined(_WIN32)
        if (library.front() == '/' && !contains_directory_separator(library.substr(1))) {
            cacheable = false;
            continue;
        }
#endif
        const auto candidate =
            make_absolute(std::filesystem::path{library}, working_directory);
        const auto exists = std::filesystem::is_regular_file(candidate, error);
        error.clear();
        const bool explicit_path =
            std::filesystem::path{library}.is_absolute()
            || contains_directory_separator(library);
        if (!exists) {
            if (explicit_path) {
                report_error(
                    diagnostics,
                    "FSIM-SC-C010",
                    "explicit SystemC linked library does not exist",
                    candidate);
                return false;
            }
            cacheable = false;
            continue;
        }
        if (!builder.add_file(
                "linked-library." + std::to_string(content_index++),
                candidate,
                error)) {
            report_error(
                diagnostics,
                "FSIM-SC-C010",
                "cannot hash explicit SystemC linked library: " + error.message(),
                candidate);
            return false;
        }
    }
    builder.add("linked-library.content-count", std::to_string(content_index));
    return true;
}

[[nodiscard]] bool option_changes_output(
    const HostToolchain toolchain,
    const std::string& option) {
    const auto folded = lowercase(option);
    if (toolchain == HostToolchain::msvc) {
        return folded == "/c" || folded == "/e" || folded == "/ep" || folded == "/p"
            || folded == "/link"
            || folded.rfind("/fe", 0) == 0 || folded.rfind("/fo", 0) == 0
            || folded.rfind("/fd", 0) == 0 || folded.rfind("/fa", 0) == 0
            || folded.rfind("/fr", 0) == 0 || folded.rfind("/out:", 0) == 0;
    }
    return option == "-c" || option == "-E" || option == "-S" || option == "-o"
        || folded == "--output" || folded.rfind("--output=", 0) == 0
        || folded.rfind("-wl,-o", 0) == 0;
}

[[nodiscard]] bool option_hides_cache_dependencies(
    const HostToolchain toolchain,
    const std::string& option) {
    const auto folded = lowercase(option);
    if (toolchain == HostToolchain::msvc) {
        return folded == "/i" || folded.rfind("/i", 0) == 0
            || folded == "/d" || folded.rfind("/d", 0) == 0
            || folded == "/u" || folded.rfind("/u", 0) == 0
            || folded == "/fi" || folded.rfind("/fi", 0) == 0
            || folded.rfind("/yu", 0) == 0 || folded.rfind("/yc", 0) == 0
            || folded.rfind("/fp", 0) == 0
            || folded.rfind("/external:i", 0) == 0;
    }
    return option == "-I" || option.rfind("-I", 0) == 0
        || option == "-D" || option.rfind("-D", 0) == 0
        || option == "-U" || option.rfind("-U", 0) == 0
        || option == "-include" || option.rfind("-include", 0) == 0
        || option == "-imacros" || option.rfind("-imacros", 0) == 0
        || option == "-isystem" || option.rfind("-isystem", 0) == 0
        || option == "-iquote" || option.rfind("-iquote", 0) == 0
        || option == "-idirafter" || option.rfind("-idirafter", 0) == 0
        || option == "-include-pch" || option.rfind("-include-pch", 0) == 0;
}

[[nodiscard]] bool validate_options(
    const project::SystemCSection& settings,
    const HostToolchain toolchain,
    diagnostic::Engine& diagnostics) {
    const auto validate = [&](
                              const std::vector<std::string>& options,
                              const bool compile_options) {
        for (const auto& option : options) {
            if (option_changes_output(toolchain, option)) {
                report_error(
                    diagnostics,
                    "FSIM-SC-C004",
                    "SystemC compiler option '" + option
                        + "' conflicts with fsim's shared-library output");
                return false;
            }
            if (compile_options
                && option_hides_cache_dependencies(toolchain, option)) {
                report_error(
                    diagnostics,
                    "FSIM-SC-C011",
                    "SystemC compiler option '" + option
                        + "' hides inputs from the persistent cache; use the "
                          "manifest include/define fields and ordinary includes");
                return false;
            }
        }
        return true;
    };
    return validate(settings.compile_options, true)
        && validate(settings.link_options, false);
}

[[nodiscard]] std::string prefixed_define(
    const HostToolchain toolchain,
    const std::string& define) {
    if (toolchain == HostToolchain::msvc) {
        if (define.rfind("/D", 0) == 0 || define.rfind("-D", 0) == 0) {
            return define;
        }
        return "/D" + define;
    }
    if (define.rfind("-D", 0) == 0) {
        return define;
    }
    return "-D" + define;
}

[[nodiscard]] bool is_path_like_library(const std::string& library) {
    if (contains_directory_separator(library)) {
        return true;
    }
    const auto extension = lowercase(std::filesystem::path{library}.extension().string());
    return extension == ".a" || extension == ".so" || extension == ".dylib"
        || extension == ".lib" || extension == ".dll";
}

[[nodiscard]] std::vector<std::string> common_compile_argv(
    const HostToolchain toolchain,
    const std::string& compiler_name,
    const std::filesystem::path& resolved_compiler,
    const std::vector<std::filesystem::path>& includes,
    const project::SystemCSection& settings) {
    std::vector<std::string> argv;
    argv.push_back(
        path_argument(resolved_compiler.empty() ? std::filesystem::path{compiler_name}
                                                : resolved_compiler));

    if (toolchain == HostToolchain::msvc) {
        argv.emplace_back("/nologo");
        argv.emplace_back("/std:c++20");
        argv.emplace_back("/EHsc");
        argv.emplace_back("/MD");
        for (const auto& include : includes) {
            argv.push_back("/I" + path_argument(include));
        }
        for (const auto& define : settings.defines) {
            argv.push_back(prefixed_define(toolchain, define));
        }
        argv.insert(
            argv.end(), settings.compile_options.begin(), settings.compile_options.end());
        return argv;
    }

    argv.emplace_back("-std=c++20");
#if !defined(_WIN32)
    argv.emplace_back("-fPIC");
    argv.emplace_back("-fvisibility=hidden");
#endif
    for (const auto& include : includes) {
        argv.push_back("-I" + path_argument(include));
    }
    for (const auto& define : settings.defines) {
        argv.push_back(prefixed_define(toolchain, define));
    }
    argv.insert(argv.end(), settings.compile_options.begin(), settings.compile_options.end());
    return argv;
}

[[nodiscard]] std::vector<CompilerCommand> build_commands(
    const HostToolchain toolchain,
    const std::string& compiler_name,
    const std::filesystem::path& resolved_compiler,
    const std::vector<std::filesystem::path>& sources,
    const std::vector<std::filesystem::path>& includes,
    const project::SystemCSection& settings,
    const std::filesystem::path& output,
    const std::filesystem::path& working_directory,
    std::vector<std::filesystem::path>& intermediate_paths) {
    std::vector<CompilerCommand> commands;
    if (toolchain == HostToolchain::msvc) {
        for (std::size_t index = 0; index < sources.size(); ++index) {
            auto object =
                output.parent_path() / ("source-" + std::to_string(index) + ".obj");
            auto program_database =
                output.parent_path() / ("source-" + std::to_string(index) + ".pdb");
            auto argv = common_compile_argv(
                toolchain, compiler_name, resolved_compiler, includes, settings);
            argv.emplace_back("/c");
            argv.push_back(path_argument(sources[index]));
            argv.push_back("/Fo" + path_argument(object));
            argv.push_back("/Fd" + path_argument(program_database));
            commands.push_back({std::move(argv), working_directory, toolchain});
            intermediate_paths.push_back(std::move(object));
            intermediate_paths.push_back(std::move(program_database));
        }

        std::vector<std::string> link_argv;
        link_argv.push_back(path_argument(
            resolved_compiler.empty() ? std::filesystem::path{compiler_name}
                                      : resolved_compiler));
        link_argv.emplace_back("/nologo");
        link_argv.emplace_back("/MD");
        link_argv.emplace_back("/LD");
        for (std::size_t index = 0; index < sources.size(); ++index) {
            link_argv.push_back(path_argument(
                output.parent_path() / ("source-" + std::to_string(index) + ".obj")));
        }
        link_argv.push_back("/Fe" + path_argument(output));
        link_argv.emplace_back("/link");
        link_argv.insert(
            link_argv.end(), settings.link_options.begin(), settings.link_options.end());
        for (const auto& library : settings.libraries) {
            if (library.empty()) {
                continue;
            }
            if (library.front() == '/' || library.front() == '-'
                || is_path_like_library(library)) {
                link_argv.push_back(library);
            } else {
                link_argv.push_back(library + ".lib");
            }
        }
        commands.push_back({std::move(link_argv), working_directory, toolchain});
        return commands;
    }

    auto argv = common_compile_argv(
        toolchain, compiler_name, resolved_compiler, includes, settings);
    argv.emplace_back("-shared");
    for (const auto& source : sources) {
        argv.push_back(path_argument(source));
    }
    argv.emplace_back("-o");
    argv.push_back(path_argument(output));
    argv.insert(argv.end(), settings.link_options.begin(), settings.link_options.end());
    for (const auto& library : settings.libraries) {
        if (library.empty()) {
            continue;
        }
        if (library.front() == '-' || is_path_like_library(library)) {
            argv.push_back(library);
        } else {
            argv.push_back("-l" + library);
        }
    }
    commands.push_back({std::move(argv), working_directory, toolchain});
    return commands;
}

[[nodiscard]] bool hash_file(
    const std::filesystem::path& path,
    std::string& result,
    std::error_code& error) {
    error.clear();
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        error = std::make_error_code(std::errc::io_error);
        return false;
    }

    support::Sha256 hasher;
    std::array<char, 64U * 1024U> buffer{};
    while (stream) {
        stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = stream.gcount();
        if (count > 0) {
            hasher.update(std::as_bytes(std::span{buffer.data(), static_cast<std::size_t>(count)}));
        }
    }
    if (!stream.eof()) {
        error = std::make_error_code(std::errc::io_error);
        return false;
    }
    result = support::Sha256::hex(hasher.finish());
    return true;
}

[[nodiscard]] bool valid_cached_artifact(
    const std::filesystem::path& library,
    std::error_code& error) {
    error.clear();
    const auto checksum_path = library.string() + ".sha256";
    if (!std::filesystem::is_regular_file(library, error)) {
        error.clear();
        return false;
    }
    const auto size = std::filesystem::file_size(library, error);
    if (error || size == 0) {
        error.clear();
        return false;
    }

    std::ifstream checksum_stream(checksum_path, std::ios::binary);
    if (!checksum_stream) {
        error.clear();
        return false;
    }
    std::string expected;
    std::getline(checksum_stream, expected);
    if (expected.size() != 64) {
        return false;
    }
    std::string actual;
    if (!hash_file(library, actual, error)) {
        return false;
    }
    return actual == expected;
}

[[nodiscard]] bool publish_artifact(
    const PluginCompilePlan& plan,
    diagnostic::Engine& diagnostics) {
    std::error_code error;
    std::string checksum;
    if (!hash_file(plan.build_path, checksum, error)) {
        report_error(
            diagnostics,
            "FSIM-SC-C008",
            "cannot checksum compiled SystemC plug-in: " + error.message(),
            plan.build_path);
        return false;
    }

    const auto checksum_path = std::filesystem::path{plan.library_path.string() + ".sha256"};
    const auto suffix =
        checksum_temporary_counter.fetch_add(1, std::memory_order_relaxed);
    const auto temporary_checksum =
        std::filesystem::path{checksum_path.string() + ".tmp." + std::to_string(suffix)};
    {
        std::ofstream stream(temporary_checksum, std::ios::binary | std::ios::trunc);
        stream << checksum << '\n';
        stream.flush();
        if (!stream) {
            report_error(
                diagnostics,
                "FSIM-SC-C008",
                "cannot write SystemC plug-in checksum",
                temporary_checksum);
            std::filesystem::remove(temporary_checksum, error);
            return false;
        }
    }

    if (!compiler::detail::atomic_replace_file(
            plan.build_path, plan.library_path, error)) {
        report_error(
            diagnostics,
            "FSIM-SC-C008",
            "cannot publish compiled SystemC plug-in: " + error.message(),
            plan.library_path);
        std::filesystem::remove(temporary_checksum, error);
        return false;
    }

    if (!compiler::detail::atomic_replace_file(
            temporary_checksum, checksum_path, error)) {
        report_error(
            diagnostics,
            "FSIM-SC-C008",
            "cannot publish SystemC plug-in checksum: " + error.message(),
            checksum_path);
        std::error_code ignored;
        std::filesystem::remove(temporary_checksum, ignored);
        return false;
    }
    return true;
}

#if defined(_WIN32)

[[nodiscard]] std::wstring utf8_to_wide(const std::string_view input) {
    if (input.empty()) {
        return {};
    }
    const auto required = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, input.data(), static_cast<int>(input.size()), nullptr, 0);
    if (required <= 0) {
        return {};
    }
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            input.data(),
            static_cast<int>(input.size()),
            result.data(),
            required)
        <= 0) {
        return {};
    }
    return result;
}

[[nodiscard]] std::wstring quote_windows_argument(const std::wstring& argument) {
    if (!argument.empty()
        && argument.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
        return argument;
    }
    std::wstring result{L'"'};
    std::size_t backslashes = 0;
    for (const wchar_t character : argument) {
        if (character == L'\\') {
            ++backslashes;
            continue;
        }
        if (character == L'"') {
            result.append(backslashes * 2U + 1U, L'\\');
            result.push_back(L'"');
            backslashes = 0;
            continue;
        }
        result.append(backslashes, L'\\');
        backslashes = 0;
        result.push_back(character);
    }
    result.append(backslashes * 2U, L'\\');
    result.push_back(L'"');
    return result;
}

class TemporaryResponseFile final {
public:
    TemporaryResponseFile() = default;
    TemporaryResponseFile(const TemporaryResponseFile&) = delete;
    TemporaryResponseFile& operator=(const TemporaryResponseFile&) = delete;
    ~TemporaryResponseFile() {
        if (!path_.empty()) {
            DeleteFileW(path_.c_str());
        }
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

    bool write(
        const CompilerCommand& command,
        const std::vector<std::wstring>& wide_arguments,
        std::string& error_message) {
        for (std::size_t attempt = 0; attempt < 100; ++attempt) {
            const auto serial =
                response_temporary_counter.fetch_add(1, std::memory_order_relaxed);
            path_ = command.working_directory
                / (".fsim-compiler-arguments-" + std::to_string(GetCurrentProcessId())
                   + "-" + std::to_string(serial) + ".rsp");
            const HANDLE file = CreateFileW(
                path_.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                FILE_ATTRIBUTE_TEMPORARY, nullptr);
            if (file == INVALID_HANDLE_VALUE) {
                if (GetLastError() == ERROR_FILE_EXISTS
                    || GetLastError() == ERROR_ALREADY_EXISTS) {
                    continue;
                }
                error_message =
                    "cannot create compiler response file (error "
                    + std::to_string(GetLastError()) + ")";
                path_.clear();
                return false;
            }

            bool ok = true;
            if (command.toolchain == HostToolchain::msvc) {
                std::wstring contents;
                contents.push_back(static_cast<wchar_t>(0xfeff));
                for (std::size_t index = 1; index < wide_arguments.size(); ++index) {
                    contents += quote_windows_argument(wide_arguments[index]);
                    contents += L"\r\n";
                }
                DWORD written = 0;
                const auto byte_count =
                    static_cast<DWORD>(contents.size() * sizeof(wchar_t));
                ok = WriteFile(
                         file, contents.data(), byte_count, &written, nullptr)
                    && written == byte_count;
            } else {
                std::string contents;
                for (std::size_t index = 1; index < command.argv.size(); ++index) {
                    contents.push_back('"');
                    for (const char character : command.argv[index]) {
                        if (character == '\\' || character == '"') {
                            contents.push_back('\\');
                        }
                        contents.push_back(character);
                    }
                    contents += "\"\r\n";
                }
                DWORD written = 0;
                ok = contents.size() <= std::numeric_limits<DWORD>::max()
                    && WriteFile(
                        file,
                        contents.data(),
                        static_cast<DWORD>(contents.size()),
                        &written,
                        nullptr)
                    && written == static_cast<DWORD>(contents.size());
            }
            ok = ok && FlushFileBuffers(file);
            const auto native_error = ok ? ERROR_SUCCESS : GetLastError();
            CloseHandle(file);
            if (!ok) {
                error_message =
                    "cannot write compiler response file (error "
                    + std::to_string(native_error) + ")";
                DeleteFileW(path_.c_str());
                path_.clear();
                return false;
            }
            return true;
        }
        error_message = "cannot allocate a unique compiler response file";
        path_.clear();
        return false;
    }

private:
    std::filesystem::path path_;
};

[[nodiscard]] ProcessResult run_process(const CompilerCommand& command) {
    ProcessResult result;
    if (command.argv.empty()) {
        result.start_error = "empty compiler argument vector";
        return result;
    }

    std::vector<std::wstring> wide_arguments;
    wide_arguments.reserve(command.argv.size());
    for (const auto& argument : command.argv) {
        auto wide = utf8_to_wide(argument);
        if (wide.empty() && !argument.empty()) {
            result.start_error = "compiler argument is not valid UTF-8";
            return result;
        }
        wide_arguments.push_back(std::move(wide));
    }
    std::wstring command_line;
    for (std::size_t index = 0; index < wide_arguments.size(); ++index) {
        if (index != 0) {
            command_line.push_back(L' ');
        }
        command_line += quote_windows_argument(wide_arguments[index]);
    }
    TemporaryResponseFile response_file;
    if (command_line.size() >= 30'000) {
        if (!response_file.write(command, wide_arguments, result.start_error)) {
            return result;
        }
        command_line = quote_windows_argument(wide_arguments.front());
        command_line.push_back(L' ');
        command_line +=
            quote_windows_argument(L"@" + response_file.path().wstring());
    }

    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    HANDLE read_pipe = nullptr;
    HANDLE write_pipe = nullptr;
    if (!CreatePipe(&read_pipe, &write_pipe, &security, 0)) {
        result.start_error = "CreatePipe failed with error " + std::to_string(GetLastError());
        return result;
    }
    if (!SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0)) {
        result.start_error =
            "SetHandleInformation failed with error " + std::to_string(GetLastError());
        CloseHandle(read_pipe);
        CloseHandle(write_pipe);
        return result;
    }

    HANDLE child_input = nullptr;
    const HANDLE process_handle = GetCurrentProcess();
    const HANDLE standard_input = GetStdHandle(STD_INPUT_HANDLE);
    if (standard_input != nullptr && standard_input != INVALID_HANDLE_VALUE) {
        (void)DuplicateHandle(
            process_handle,
            standard_input,
            process_handle,
            &child_input,
            0,
            TRUE,
            DUPLICATE_SAME_ACCESS);
    }
    if (child_input == nullptr) {
        child_input = CreateFileW(
            L"NUL",
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            &security,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
    }
    if (child_input == INVALID_HANDLE_VALUE || child_input == nullptr) {
        result.start_error =
            "cannot prepare compiler standard input (error "
            + std::to_string(GetLastError()) + ")";
        CloseHandle(read_pipe);
        CloseHandle(write_pipe);
        return result;
    }

    SIZE_T attribute_bytes = 0;
    (void)InitializeProcThreadAttributeList(nullptr, 1, 0, &attribute_bytes);
    std::vector<std::byte> attribute_storage(attribute_bytes);
    auto* attributes = reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(
        attribute_storage.data());
    if (!InitializeProcThreadAttributeList(
            attributes, 1, 0, &attribute_bytes)) {
        result.start_error =
            "InitializeProcThreadAttributeList failed with error "
            + std::to_string(GetLastError());
        CloseHandle(child_input);
        CloseHandle(read_pipe);
        CloseHandle(write_pipe);
        return result;
    }
    const std::array inherited_handles{write_pipe, child_input};
    if (!UpdateProcThreadAttribute(
            attributes,
            0,
            PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
            const_cast<HANDLE*>(inherited_handles.data()),
            sizeof(inherited_handles),
            nullptr,
            nullptr)) {
        result.start_error =
            "UpdateProcThreadAttribute failed with error "
            + std::to_string(GetLastError());
        DeleteProcThreadAttributeList(attributes);
        CloseHandle(child_input);
        CloseHandle(read_pipe);
        CloseHandle(write_pipe);
        return result;
    }

    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = child_input;
    startup.StartupInfo.hStdOutput = write_pipe;
    startup.StartupInfo.hStdError = write_pipe;
    startup.lpAttributeList = attributes;
    PROCESS_INFORMATION process{};
    auto application = wide_arguments.front();
    const auto working_directory = utf8_to_wide(path_argument(command.working_directory));
    const BOOL created = CreateProcessW(
        application.empty() ? nullptr : application.c_str(),
        command_line.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT,
        nullptr,
        working_directory.empty() ? nullptr : working_directory.c_str(),
        &startup.StartupInfo,
        &process);
    DeleteProcThreadAttributeList(attributes);
    CloseHandle(child_input);
    CloseHandle(write_pipe);
    if (!created) {
        result.start_error =
            "CreateProcessW failed with error " + std::to_string(GetLastError());
        CloseHandle(read_pipe);
        return result;
    }
    result.started = true;

    std::array<char, 4096> buffer{};
    DWORD count = 0;
    while (ReadFile(read_pipe, buffer.data(), static_cast<DWORD>(buffer.size()), &count, nullptr)
           && count != 0) {
        if (result.output.size() < kMaximumCompilerOutput) {
            const auto remaining = kMaximumCompilerOutput - result.output.size();
            result.output.append(buffer.data(), std::min<std::size_t>(remaining, count));
        }
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exit_code = 1;
    if (GetExitCodeProcess(process.hProcess, &exit_code)) {
        result.exit_code = static_cast<int>(exit_code);
    }
    CloseHandle(read_pipe);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return result;
}

#else

[[nodiscard]] ProcessResult run_process(const CompilerCommand& command) {
    ProcessResult result;
    if (command.argv.empty()) {
        result.start_error = "empty compiler argument vector";
        return result;
    }

    // All storage required by exec is prepared before fork. The child only
    // calls async-signal-safe POSIX functions, so a multithreaded caller cannot
    // deadlock on inherited allocator or iostream locks.
    std::vector<char*> argv;
    argv.reserve(command.argv.size() + 1);
    for (const auto& argument : command.argv) {
        argv.push_back(const_cast<char*>(argument.c_str()));
    }
    argv.push_back(nullptr);

    std::array<int, 2> pipe_descriptors{};
#if defined(__linux__)
    const auto pipe_result =
        ::pipe2(pipe_descriptors.data(), O_CLOEXEC);
#else
    const auto pipe_result = ::pipe(pipe_descriptors.data());
#endif
    if (pipe_result != 0) {
        result.start_error = "pipe failed: " + std::string{std::strerror(errno)};
        return result;
    }

    const auto child = ::fork();
    if (child < 0) {
        result.start_error = "fork failed: " + std::string{std::strerror(errno)};
        ::close(pipe_descriptors[0]);
        ::close(pipe_descriptors[1]);
        return result;
    }
    if (child == 0) {
        ::close(pipe_descriptors[0]);
#if defined(__linux__)
        if (pipe_descriptors[1] == STDOUT_FILENO
            || pipe_descriptors[1] == STDERR_FILENO) {
            const auto descriptor_flags =
                ::fcntl(pipe_descriptors[1], F_GETFD);
            if (descriptor_flags < 0
                || ::fcntl(
                       pipe_descriptors[1],
                       F_SETFD,
                       descriptor_flags & ~FD_CLOEXEC)
                    < 0) {
                _exit(126);
            }
        }
#endif
        if (pipe_descriptors[1] != STDOUT_FILENO
            && ::dup2(pipe_descriptors[1], STDOUT_FILENO) < 0) {
            _exit(126);
        }
        if (pipe_descriptors[1] != STDERR_FILENO
            && ::dup2(pipe_descriptors[1], STDERR_FILENO) < 0) {
            _exit(126);
        }
        if (pipe_descriptors[1] != STDOUT_FILENO
            && pipe_descriptors[1] != STDERR_FILENO) {
            ::close(pipe_descriptors[1]);
        }
        if (::chdir(command.working_directory.c_str()) != 0) {
            _exit(126);
        }

        ::execv(argv.front(), argv.data());
        _exit(127);
    }

    result.started = true;
    ::close(pipe_descriptors[1]);
    std::array<char, 4096> buffer{};
    while (true) {
        const auto count = ::read(pipe_descriptors[0], buffer.data(), buffer.size());
        if (count > 0) {
            if (result.output.size() < kMaximumCompilerOutput) {
                const auto remaining = kMaximumCompilerOutput - result.output.size();
                result.output.append(
                    buffer.data(),
                    std::min<std::size_t>(remaining, static_cast<std::size_t>(count)));
            }
            continue;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        break;
    }
    ::close(pipe_descriptors[0]);

    int status = 0;
    pid_t waited = -1;
    do {
        waited = ::waitpid(child, &status, 0);
    } while (waited < 0 && errno == EINTR);
    if (waited < 0) {
        result.exit_code = -1;
        return result;
    }
    if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        result.exit_code = 128 + WTERMSIG(status);
    }
    return result;
}

#endif

[[nodiscard]] bool shell_display_safe(const unsigned char character) noexcept {
    return std::isalnum(character) != 0 || character == '_' || character == '-'
        || character == '.' || character == '/' || character == '\\'
        || character == ':' || character == '+' || character == '=';
}

[[nodiscard]] std::string quote_for_display(const std::string& argument) {
    if (!argument.empty()
        && std::all_of(argument.begin(), argument.end(), [](const unsigned char character) {
               return shell_display_safe(character);
           })) {
        return argument;
    }
    std::string result{"'"};
    for (const char character : argument) {
        if (character == '\'') {
            result += "'\\''";
        } else {
            result.push_back(character);
        }
    }
    result.push_back('\'');
    return result;
}

[[nodiscard]] std::uint64_t dependency_process_id() noexcept {
#if defined(_WIN32)
    return static_cast<std::uint64_t>(GetCurrentProcessId());
#else
    return static_cast<std::uint64_t>(::getpid());
#endif
}

class DependencyScratchDirectory final {
public:
    explicit DependencyScratchDirectory(std::filesystem::path path)
        : path_(std::move(path)) {}

    DependencyScratchDirectory(const DependencyScratchDirectory&) = delete;
    DependencyScratchDirectory& operator=(const DependencyScratchDirectory&) = delete;
    DependencyScratchDirectory(DependencyScratchDirectory&& other) noexcept
        : path_(std::exchange(other.path_, {})) {}
    DependencyScratchDirectory& operator=(DependencyScratchDirectory&&) = delete;

    ~DependencyScratchDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
};

[[nodiscard]] std::optional<DependencyScratchDirectory>
create_dependency_scratch_directory(
    const std::filesystem::path& cache_directory,
    std::error_code& error) {
    const auto root = cache_directory / "systemc" / "dependency-scans";
    std::filesystem::create_directories(root, error);
    if (error) {
        return std::nullopt;
    }

    for (std::size_t attempt = 0; attempt < 100; ++attempt) {
        const auto serial =
            dependency_scan_counter.fetch_add(1, std::memory_order_relaxed);
        const auto name =
            std::to_string(dependency_process_id()) + "-"
            + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count())
            + "-" + std::to_string(serial);
        const auto candidate = root / name;
        error.clear();
        if (std::filesystem::create_directory(candidate, error)) {
            return DependencyScratchDirectory{candidate};
        }
        if (error) {
            return std::nullopt;
        }
    }
    error = std::make_error_code(std::errc::file_exists);
    return std::nullopt;
}

void report_dependency_cache_disabled(
    diagnostic::Engine& diagnostics,
    std::string message,
    const std::filesystem::path& source = {},
    const CompilerCommand* command = nullptr,
    const ProcessResult* process = nullptr) {
    diagnostic::Diagnostic diagnostic;
    diagnostic.severity = diagnostic::Severity::warning;
    diagnostic.code = "FSIM-SC-C012";
    diagnostic.message =
        std::move(message) + "; persistent SystemC plug-in cache reuse is disabled";
    diagnostic.span = path_span(source);
    if (command != nullptr) {
        diagnostic.notes.push_back(
            {"dependency argv: " + format_compiler_command(*command), {}});
    }
    if (process != nullptr && !process->start_error.empty()) {
        diagnostic.notes.push_back(
            {"dependency compiler start failure: " + process->start_error, {}});
    }
    if (process != nullptr && !process->output.empty()) {
        diagnostic.notes.push_back(
            {"dependency compiler output:\n" + process->output, {}});
    }
    diagnostics.report(std::move(diagnostic));
}

[[nodiscard]] std::optional<std::string_view>
volatile_predefined_macro(const std::string_view contents) {
    constexpr std::array<std::string_view, 3> volatile_macros{
        "__DATE__",
        "__TIME__",
        "__TIMESTAMP__",
    };
    const auto without_comments = remove_cpp_comments(contents);
    const auto is_identifier_character = [](const char character) {
        const auto byte = static_cast<unsigned char>(character);
        return std::isalnum(byte) != 0 || character == '_';
    };
    for (const auto macro : volatile_macros) {
        std::size_t position = 0;
        while ((position = without_comments.find(macro, position))
               != std::string::npos) {
            const auto end = position + macro.size();
            const bool begins_identifier =
                position != 0
                && is_identifier_character(without_comments[position - 1]);
            const bool ends_identifier =
                end != without_comments.size()
                && is_identifier_character(without_comments[end]);
            if (!begins_identifier && !ends_identifier) {
                return macro;
            }
            position = end;
        }
    }
    return std::nullopt;
}

struct VolatileMacroInput {
    std::filesystem::path path;
    std::string macro;
};

[[nodiscard]] std::optional<VolatileMacroInput>
find_volatile_macro_input(
    const std::vector<std::filesystem::path>& inputs) {
    std::error_code error;
    for (const auto& input : inputs) {
        const auto contents = read_text_file(input, error);
        if (!contents) {
            // The normal dependency hashing path reports an unreadable input.
            continue;
        }
        if (const auto macro = volatile_predefined_macro(*contents)) {
            return VolatileMacroInput{input, std::string{*macro}};
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::vector<std::string>>
parse_makefile_dependencies(const std::string_view contents) {
    const auto colon = contents.find(':');
    if (colon == std::string_view::npos) {
        return std::nullopt;
    }

    std::vector<std::string> result;
    std::string token;
    const auto flush = [&]() {
        if (!token.empty()) {
            result.push_back(std::move(token));
            token.clear();
        }
    };
    for (std::size_t index = colon + 1; index < contents.size(); ++index) {
        const char character = contents[index];
        if (character == '\\') {
            if (index + 1 >= contents.size()) {
                return std::nullopt;
            }
            if (contents[index + 1] == '\n') {
                ++index;
                continue;
            }
            if (contents[index + 1] == '\r'
                && index + 2 < contents.size()
                && contents[index + 2] == '\n') {
                index += 2;
                continue;
            }
            token.push_back(contents[++index]);
            continue;
        }
        if (character == '$' && index + 1 < contents.size()
            && contents[index + 1] == '$') {
            token.push_back('$');
            ++index;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(character)) != 0) {
            flush();
            continue;
        }
        token.push_back(character);
    }
    flush();
    if (result.empty()) {
        return std::nullopt;
    }
    return result;
}

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
    diagnostic::Engine& diagnostics) {
    builder.add("dependency.discovery", "compiler-make-v1");
    if (!cacheable) {
        builder.add("dependency.count", "0");
        return true;
    }
    if (resolved_compiler.empty()) {
        cacheable = false;
        builder.add("dependency.count", "0");
        report_dependency_cache_disabled(
            diagnostics,
            "the selected GCC-like compiler could not be resolved");
        return true;
    }

    std::error_code error;
    auto scratch =
        create_dependency_scratch_directory(cache_directory, error);
    if (!scratch) {
        cacheable = false;
        builder.add("dependency.count", "0");
        report_dependency_cache_disabled(
            diagnostics,
            "cannot create dependency scan directory: " + error.message(),
            cache_directory);
        return true;
    }

    std::vector<std::filesystem::path> dependencies;
    for (std::size_t index = 0; index < sources.size(); ++index) {
        const auto dependency_file =
            scratch->path() / ("source-" + std::to_string(index) + ".d");
        auto argv = common_compile_argv(
            HostToolchain::gcc_like,
            compiler_name,
            resolved_compiler,
            includes,
            settings);
        argv.emplace_back("-M");
        argv.emplace_back("-MF");
        argv.push_back(path_argument(dependency_file));
        argv.emplace_back("-MT");
        argv.push_back("fsim_dependency_target_" + std::to_string(index));
        argv.push_back(path_argument(sources[index]));
        const CompilerCommand command{
            std::move(argv), working_directory, HostToolchain::gcc_like};
        const auto process = run_process(command);
        if (!process.started || process.exit_code != 0) {
            cacheable = false;
            builder.add("dependency.count", "0");
            report_dependency_cache_disabled(
                diagnostics,
                "the compiler could not emit a complete dependency closure",
                sources[index],
                &command,
                &process);
            return true;
        }

        const auto contents = read_text_file(dependency_file, error);
        const auto parsed =
            contents ? parse_makefile_dependencies(*contents) : std::nullopt;
        if (!parsed) {
            cacheable = false;
            builder.add("dependency.count", "0");
            report_dependency_cache_disabled(
                diagnostics,
                contents
                    ? "the compiler emitted an invalid dependency file"
                    : "cannot read compiler dependency file: " + error.message(),
                dependency_file,
                &command,
                &process);
            return true;
        }

        for (const auto& dependency_name : *parsed) {
            auto dependency =
                make_absolute(
                    std::filesystem::path{dependency_name},
                    working_directory);
            dependency = normalized_existing_path(dependency, error);
            if (dependency.empty()) {
                cacheable = false;
                builder.add("dependency.count", "0");
                report_dependency_cache_disabled(
                    diagnostics,
                    "compiler dependency is not a readable regular file",
                    std::filesystem::path{dependency_name});
                return true;
            }
            dependencies.push_back(std::move(dependency));
        }
    }

    std::sort(
        dependencies.begin(), dependencies.end(), [](const auto& left, const auto& right) {
            return left.generic_string() < right.generic_string();
        });
    dependencies.erase(
        std::unique(
            dependencies.begin(), dependencies.end(), [](const auto& left, const auto& right) {
                return left == right;
            }),
        dependencies.end());

    std::vector<std::filesystem::path> volatile_macro_inputs = sources;
    volatile_macro_inputs.insert(
        volatile_macro_inputs.end(),
        dependencies.begin(),
        dependencies.end());
    if (const auto use = find_volatile_macro_input(volatile_macro_inputs)) {
        cacheable = false;
        builder.add("dependency.count", "0");
        report_dependency_cache_disabled(
            diagnostics,
            "source dependency uses volatile predefined macro '"
                + use->macro + "'",
            use->path);
        return true;
    }

    std::unordered_set<std::string> source_names;
    for (const auto& source : sources) {
        auto normalized = std::filesystem::weakly_canonical(source, error);
        if (error) {
            error.clear();
            normalized = source.lexically_normal();
        }
        source_names.insert(normalized.generic_string());
    }
    dependencies.erase(
        std::remove_if(
            dependencies.begin(),
            dependencies.end(),
            [&](const auto& dependency) {
                return source_names.contains(dependency.generic_string());
            }),
        dependencies.end());

    builder.add("dependency.count", std::to_string(dependencies.size()));
    for (std::size_t index = 0; index < dependencies.size(); ++index) {
        if (builder.add_file(
                "dependency." + std::to_string(index),
                dependencies[index],
                error)) {
            continue;
        }
        cacheable = false;
        report_dependency_cache_disabled(
            diagnostics,
            "cannot hash compiler dependency: " + error.message(),
            dependencies[index]);
        return true;
    }
    return true;
}

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
    diagnostic::Engine& diagnostics) {
    if (toolchain == HostToolchain::gcc_like) {
        return add_gcc_like_dependencies_to_key(
            builder,
            compiler_name,
            resolved_compiler,
            sources,
            includes,
            settings,
            working_directory,
            cache_directory,
            cacheable,
            diagnostics);
    }

    builder.add("dependency.discovery", "manifest-conservative-v1");
    if (!cacheable) {
        builder.add("dependency.count", "0");
        return true;
    }
    const bool was_cacheable = cacheable;
    if (!add_transitive_dependencies_to_key(
            builder,
            sources,
            includes,
            settings.defines,
            cacheable,
            diagnostics)) {
        return false;
    }
    if (was_cacheable && !cacheable) {
        report_dependency_cache_disabled(
            diagnostics,
            "MSVC dependency discovery found an include outside manifest roots");
    }
    return true;
}

} // namespace

std::string_view to_string(const HostToolchain toolchain) noexcept {
    switch (toolchain) {
    case HostToolchain::gcc_like:
        return "gcc-like";
    case HostToolchain::msvc:
        return "msvc";
    }
    return "unknown";
}

std::optional<PluginCompilePlan> plan_plugin_compile(
    const PluginCompileRequest& request,
    diagnostic::Engine& diagnostics) {
    std::error_code error;
    const auto working_directory = effective_working_directory(request, error);
    if (error || working_directory.empty()) {
        report_error(
            diagnostics,
            "FSIM-SC-C001",
            "cannot resolve SystemC compiler working directory: " + error.message(),
            request.working_directory);
        return std::nullopt;
    }
    if (request.sources.empty()) {
        report_error(
            diagnostics, "FSIM-SC-C002", "SystemC source set contains no C++ source files");
        return std::nullopt;
    }

    std::vector<std::filesystem::path> sources;
    sources.reserve(request.sources.size());
    for (const auto& source : request.sources) {
        const auto absolute = make_absolute(source, working_directory);
        if (!source_extension_supported(absolute)) {
            report_error(
                diagnostics,
                "FSIM-SC-C003",
                "SystemC source must use a .cpp, .cc, or .cxx extension",
                absolute);
            return std::nullopt;
        }
        if (!std::filesystem::is_regular_file(absolute, error)) {
            const auto detail = error ? ": " + error.message() : std::string{};
            report_error(
                diagnostics,
                "FSIM-SC-C003",
                "SystemC source file does not exist" + detail,
                absolute);
            return std::nullopt;
        }
        error.clear();
        sources.push_back(absolute);
    }

    std::vector<std::filesystem::path> includes;
    includes.reserve(request.settings.include_directories.size());
    for (const auto& include : request.settings.include_directories) {
        includes.push_back(make_absolute(include, working_directory));
    }

    const auto compiler_name =
        request.settings.compiler.empty() ? default_compiler() : request.settings.compiler;
    const auto toolchain = infer_toolchain(compiler_name);
    if (!validate_options(request.settings, toolchain, diagnostics)) {
        return std::nullopt;
    }
    const auto resolved_compiler = resolve_executable(compiler_name, working_directory);
    auto cache_directory =
        request.cache_directory.empty() ? std::filesystem::path{".fsim-cache"}
                                        : request.cache_directory;
    cache_directory = make_absolute(cache_directory, working_directory);

    compiler::CacheKeyBuilder key_builder;
    key_builder.add("kind", "fsim-systemc-shared-library-v1");
    key_builder.add("runtime-abi", std::to_string(runtime_abi_version));
    key_builder.add("systemc-abi", std::to_string(FSIM_SYSTEMC_ABI_VERSION));
    key_builder.add("toolchain", to_string(toolchain));
#if defined(_WIN32)
    key_builder.add("host-format", "windows-pe-x86-64");
#else
    key_builder.add("host-format", "linux-elf-x86-64");
#endif
    add_compiler_identity(key_builder, compiler_name, resolved_compiler);
    add_paths_to_key(key_builder, "include", includes);
    add_sequence_to_key(key_builder, "define", request.settings.defines);
    add_sequence_to_key(key_builder, "compile-option", request.settings.compile_options);
    add_sequence_to_key(key_builder, "link-option", request.settings.link_options);
    add_sequence_to_key(key_builder, "library", request.settings.libraries);
    // Raw compiler/linker options can reference response files, plug-ins,
    // profiles, sysroots, forced includes, or other inputs with
    // toolchain-specific spelling. Keep supporting the literal argv contract,
    // but never claim a persistent hit unless all inputs came through the
    // structured manifest fields.
    bool cacheable =
        request.settings.compile_options.empty()
        && request.settings.link_options.empty();
    key_builder.add("source.count", std::to_string(sources.size()));
    for (std::size_t index = 0; index < sources.size(); ++index) {
        if (!key_builder.add_file(
                "source." + std::to_string(index), sources[index], error)) {
            report_error(
                diagnostics,
                "FSIM-SC-C003",
                "cannot read SystemC source file: " + error.message(),
                sources[index]);
            return std::nullopt;
        }
    }
    if (!add_compiler_dependencies_to_key(
            key_builder,
            toolchain,
            compiler_name,
            resolved_compiler,
            sources,
            includes,
            request.settings,
            working_directory,
            cache_directory,
            cacheable,
            diagnostics)
        || !add_linked_library_contents_to_key(
            key_builder,
            request.settings.libraries,
            working_directory,
            cacheable,
            diagnostics)) {
        return std::nullopt;
    }
    if (!cacheable) {
        const auto serial =
            uncached_plan_counter.fetch_add(1, std::memory_order_relaxed);
        key_builder.add(
            "uncached.nonce",
            std::to_string(
                std::chrono::system_clock::now().time_since_epoch().count())
                + "-" + std::to_string(
                            std::chrono::steady_clock::now().time_since_epoch().count())
                + "-" + std::to_string(serial));
    }
    const auto key = key_builder.finish();

    const auto artifact_directory =
        cache_directory / "systemc" / "artifacts" / key.substr(0, 2) / key;
    PluginCompilePlan plan;
    plan.toolchain = toolchain;
    plan.cacheable = cacheable;
    plan.cache_key = key;
    plan.library_path = artifact_directory / shared_library_filename();
    plan.build_path =
        artifact_directory
        / (std::filesystem::path{shared_library_filename()}.stem().string() + ".build"
           + std::filesystem::path{shared_library_filename()}.extension().string());
    plan.commands = build_commands(
        toolchain,
        compiler_name,
        resolved_compiler,
        sources,
        includes,
        request.settings,
        plan.build_path,
        working_directory,
        plan.intermediate_paths);
    return plan;
}

PluginCompileResult compile_plugin(
    const PluginCompileRequest& request,
    diagnostic::Engine& diagnostics) {
    PluginCompileResult result;
    const auto plan = plan_plugin_compile(request, diagnostics);
    if (!plan) {
        return result;
    }
    result.cache_key = plan->cache_key;
    result.library_path = plan->library_path;

    std::error_code error;
    if (plan->cacheable && valid_cached_artifact(plan->library_path, error)) {
        result.success = true;
        result.cache_hit = true;
        result.compiler_exit_code = 0;
        return result;
    }

    const auto cache_root =
        plan->library_path.parent_path().parent_path().parent_path().parent_path();
    const auto lock_directory = cache_root / "locks";
    std::filesystem::create_directories(lock_directory, error);
    if (error) {
        report_error(
            diagnostics,
            "FSIM-SC-C005",
            "cannot create SystemC plug-in cache lock directory: " + error.message(),
            lock_directory);
        return result;
    }
    compiler::detail::CacheDirectoryLock lock{
        lock_directory / (plan->cache_key + ".lock"), error};
    if (!lock.held()) {
        report_error(
            diagnostics,
            "FSIM-SC-C006",
            "cannot acquire SystemC plug-in cache lock: " + error.message(),
            lock_directory);
        return result;
    }

    if (plan->cacheable && valid_cached_artifact(plan->library_path, error)) {
        result.success = true;
        result.cache_hit = true;
        result.compiler_exit_code = 0;
        return result;
    }

    std::filesystem::create_directories(plan->library_path.parent_path(), error);
    if (error) {
        report_error(
            diagnostics,
            "FSIM-SC-C005",
            "cannot create SystemC plug-in cache directory: " + error.message(),
            plan->library_path.parent_path());
        return result;
    }
    std::filesystem::remove(plan->build_path, error);
    error.clear();
    for (const auto& intermediate : plan->intermediate_paths) {
        std::filesystem::remove(intermediate, error);
        error.clear();
    }

    if (plan->commands.empty() || plan->commands.front().argv.empty()
        || resolve_executable(
               plan->commands.front().argv.front(),
               plan->commands.front().working_directory)
               .empty()) {
        report_error(
            diagnostics,
            "FSIM-SC-C007",
            "cannot find executable SystemC compiler '"
                + (plan->commands.empty() || plan->commands.front().argv.empty()
                       ? std::string{"<empty>"}
                       : plan->commands.front().argv.front())
                + "'");
        return result;
    }

    for (const auto& command : plan->commands) {
        const auto process = run_process(command);
        result.compiler_exit_code = process.exit_code;
        if (!process.output.empty()) {
            if (!result.compiler_output.empty()) {
                result.compiler_output.push_back('\n');
            }
            result.compiler_output += process.output;
        }
        if (!process.started) {
            report_error(
                diagnostics,
                "FSIM-SC-C007",
                "cannot start SystemC compiler '" + command.argv.front()
                    + "': " + process.start_error);
            return result;
        }
        if (process.exit_code == 0) {
            continue;
        }
        diagnostic::Diagnostic diagnostic;
        diagnostic.severity = diagnostic::Severity::error;
        diagnostic.code = "FSIM-SC-C007";
        diagnostic.message =
            "SystemC compiler exited with status " + std::to_string(process.exit_code);
        diagnostic.notes.push_back(
            {"argv: " + format_compiler_command(command), {}});
        if (!process.output.empty()) {
            diagnostic.notes.push_back({"compiler output:\n" + process.output, {}});
        }
        diagnostics.report(std::move(diagnostic));
        std::filesystem::remove(plan->build_path, error);
        return result;
    }
    if (!std::filesystem::is_regular_file(plan->build_path, error)
        || std::filesystem::file_size(plan->build_path, error) == 0) {
        report_error(
            diagnostics,
            "FSIM-SC-C007",
            "SystemC compiler reported success but did not produce a shared library",
            plan->build_path);
        return result;
    }
    if (plan->cacheable) {
        const auto verified_plan =
            plan_plugin_compile(request, diagnostics);
        const bool inputs_unchanged =
            verified_plan && verified_plan->cacheable
            && verified_plan->cache_key == plan->cache_key;
        if (!inputs_unchanged) {
            diagnostic::Diagnostic diagnostic;
            diagnostic.severity = diagnostic::Severity::error;
            diagnostic.code = "FSIM-SC-C013";
            diagnostic.message =
                "SystemC plug-in inputs or compiler identity changed during "
                "compilation; the unpublished output was discarded, retry "
                "the build";
            diagnostic.span = path_span(plan->build_path);
            diagnostic.notes.push_back(
                {"initial cache key: " + plan->cache_key, {}});
            diagnostic.notes.push_back(
                {"post-compile cache key: "
                     + (verified_plan
                            ? verified_plan->cache_key
                            : std::string{"<unavailable>"}),
                 {}});
            diagnostics.report(std::move(diagnostic));

            std::filesystem::remove(plan->build_path, error);
            error.clear();
            for (const auto& intermediate : plan->intermediate_paths) {
                std::filesystem::remove(intermediate, error);
                error.clear();
            }
            return result;
        }
    }
    if (!publish_artifact(*plan, diagnostics)) {
        return result;
    }
    for (const auto& intermediate : plan->intermediate_paths) {
        std::filesystem::remove(intermediate, error);
        error.clear();
    }

    result.success = true;
    result.compiler_exit_code = 0;
    return result;
}

std::string format_compiler_command(const CompilerCommand& command) {
    std::ostringstream result;
    for (std::size_t index = 0; index < command.argv.size(); ++index) {
        if (index != 0) {
            result << ' ';
        }
        result << quote_for_display(command.argv[index]);
    }
    return result.str();
}

} // namespace fsim::systemc
