// SPDX-License-Identifier: Apache-2.0
#include "plugin_compiler_internal.hpp"

#include <charconv>

namespace fsim::systemc::plugin_detail {

std::atomic_uint64_t checksum_temporary_counter { };
std::atomic_uint64_t uncached_plan_counter { };
std::atomic_uint64_t dependency_scan_counter { };
#if defined(_WIN32)
std::atomic_uint64_t response_temporary_counter { };
#endif

[[nodiscard]] diagnostic::SourceSpan path_span(const std::filesystem::path& path)
{
    diagnostic::SourceSpan span;
    span.path = path.generic_string();
    return span;
}

void report_error(
    diagnostic::Engine& diagnostics,
    std::string code,
    std::string message,
    const std::filesystem::path& path)
{
    diagnostics.error(std::move(code), std::move(message), path_span(path));
}

[[nodiscard]] std::string lowercase(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

[[nodiscard]] std::string path_argument(const std::filesystem::path& path)
{
#if defined(_WIN32)
    const auto encoded = path.u8string();
    return { reinterpret_cast<const char*>(encoded.data()), encoded.size() };
#else
    return path.string();
#endif
}

[[nodiscard]] std::filesystem::path make_absolute(
    const std::filesystem::path& path,
    const std::filesystem::path& working_directory)
{
    if (path.is_absolute()) {
        return path.lexically_normal();
    }
    return (working_directory / path).lexically_normal();
}

[[nodiscard]] std::filesystem::path effective_working_directory(
    const PluginCompileRequest& request,
    std::error_code& error)
{
    error.clear();
    if (!request.working_directory.empty()) {
        auto result = request.working_directory;
        if (!result.is_absolute()) {
            result = std::filesystem::absolute(result, error);
            if (error) {
                return { };
            }
        }
        return result.lexically_normal();
    }
    return std::filesystem::current_path(error).lexically_normal();
}

[[nodiscard]] bool contains_directory_separator(const std::string_view value) noexcept
{
    return value.find('/') != std::string_view::npos
        || value.find('\\') != std::string_view::npos;
}

[[nodiscard]] std::vector<std::filesystem::path> executable_candidates(
    const std::string& executable)
{
    std::vector<std::filesystem::path> result;
    if (contains_directory_separator(executable)) {
        result.emplace_back(executable);
        return result;
    }

    const auto raw_path = fsim::support::environment_variable("PATH");
    if (!raw_path) {
        return result;
    }
#if defined(_WIN32)
    constexpr char separator = ';';
#else
    constexpr char separator = ':';
#endif
    std::string_view search_path { *raw_path };
    std::size_t begin = 0;
    while (begin <= search_path.size()) {
        const auto end = search_path.find(separator, begin);
        auto directory = search_path.substr(
            begin, end == std::string_view::npos ? search_path.size() - begin : end - begin);
        if (directory.empty()) {
            directory = ".";
        }
        auto candidate = std::filesystem::path { std::string { directory } } / executable;
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
    const std::filesystem::path& working_directory)
{
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
    return { };
}

[[nodiscard]] HostToolchain infer_toolchain(const std::string& compiler)
{
    const auto filename = lowercase(std::filesystem::path { compiler }.filename().string());
    if (filename == "cl" || filename == "cl.exe" || filename == "clang-cl"
        || filename == "clang-cl.exe") {
        return HostToolchain::msvc;
    }
    return HostToolchain::gcc_like;
}

[[nodiscard]] std::string default_compiler()
{
#if defined(FSIM_SYSTEMC_DEFAULT_COMPILER)
    return FSIM_SYSTEMC_DEFAULT_COMPILER;
#elif defined(_WIN32)
    return "cl.exe";
#else
    return "c++";
#endif
}

void add_default_compiler_settings(project::SystemCSection& settings)
{
    if (!settings.compiler.empty()) {
#if defined(_WIN32)
        if (std::ranges::find(settings.defines, "SC_WIN_DLL")
            == settings.defines.end()) {
            settings.defines.insert(settings.defines.begin(), "SC_WIN_DLL");
        }
#endif
        return;
    }
    settings.compiler = default_compiler();
#if defined(FSIM_SYSTEMC_DEFAULT_COMPILE_OPTIONS)
    std::string_view encoded { FSIM_SYSTEMC_DEFAULT_COMPILE_OPTIONS };
    std::vector<std::string> options;
    while (!encoded.empty()) {
        const auto separator = encoded.find('|');
        options.emplace_back(encoded.substr(0U, separator));
        if (separator == std::string_view::npos) {
            break;
        }
        encoded.remove_prefix(separator + 1U);
    }
    settings.compile_options.insert(
        settings.compile_options.begin(), options.begin(), options.end());
#endif
#if defined(FSIM_SYSTEMC_DEFAULT_DEFINES)
    std::string_view encoded_defines { FSIM_SYSTEMC_DEFAULT_DEFINES };
    std::vector<std::string> defines;
    while (!encoded_defines.empty()) {
        const auto separator = encoded_defines.find('|');
        defines.emplace_back(encoded_defines.substr(0U, separator));
        if (separator == std::string_view::npos) {
            break;
        }
        encoded_defines.remove_prefix(separator + 1U);
    }
    settings.defines.insert(
        settings.defines.begin(), defines.begin(), defines.end());
#endif
}

[[nodiscard]] std::string shared_library_filename()
{
#if defined(_WIN32)
    return "plugin.dll";
#else
    return "plugin.so";
#endif
}

[[nodiscard]] bool source_extension_supported(const std::filesystem::path& path)
{
    const auto extension = lowercase(path.extension().string());
    return extension == ".cpp" || extension == ".cc" || extension == ".cxx";
}

void add_sequence_to_key(
    compiler::CacheKeyBuilder& builder,
    const std::string_view label,
    const std::vector<std::string>& values)
{
    builder.add(std::string { label } + ".count", std::to_string(values.size()));
    for (std::size_t index = 0; index < values.size(); ++index) {
        builder.add(
            std::string { label } + "." + std::to_string(index),
            values[index]);
    }
}

void add_paths_to_key(
    compiler::CacheKeyBuilder& builder,
    const std::string_view label,
    const std::vector<std::filesystem::path>& values)
{
    builder.add(std::string { label } + ".count", std::to_string(values.size()));
    for (std::size_t index = 0; index < values.size(); ++index) {
        builder.add(
            std::string { label } + "." + std::to_string(index),
            values[index].generic_string());
    }
}

[[nodiscard]] bool add_compiler_identity(
    compiler::CacheKeyBuilder& builder,
    const std::string& compiler_name,
    const std::filesystem::path& resolved_compiler)
{
    builder.add("compiler.requested", compiler_name);
    if (resolved_compiler.empty()) {
        builder.add("compiler.resolved", "<unresolved>");
        builder.add("compiler.binary", "<unavailable>");
        return false;
    }

    builder.add("compiler.resolved", resolved_compiler.generic_string());
    std::error_code error;
    if (builder.add_file("compiler.binary", resolved_compiler, error)) {
        return true;
    }
    builder.add("compiler.binary", "<unavailable>");
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
    return false;
}

void add_compiler_environment_to_key(
    compiler::CacheKeyBuilder& builder,
    const HostToolchain toolchain)
{
    constexpr std::array gcc_environment {
        "PATH",
        "CPATH",
        "CPLUS_INCLUDE_PATH",
        "C_INCLUDE_PATH",
        "OBJC_INCLUDE_PATH",
        "LIBRARY_PATH",
        "COMPILER_PATH",
        "GCC_EXEC_PREFIX",
        "SOURCE_DATE_EPOCH"
    };
    constexpr std::array msvc_environment {
        "PATH",
        "INCLUDE",
        "LIB",
        "LIBPATH",
        "CL",
        "_CL_",
        "VCToolsInstallDir",
        "VCINSTALLDIR",
        "WindowsSdkDir",
        "WindowsSDKVersion",
        "UniversalCRTSdkDir",
        "UCRTVersion",
        "Platform",
        "PreferredToolArchitecture",
        "SOURCE_DATE_EPOCH"
    };
    const auto add = [&](const auto& names) {
        builder.add(
            "compiler.environment.count",
            std::to_string(names.size()));
        for (std::size_t index = 0; index < names.size(); ++index) {
            const auto value = support::environment_variable(names[index]);
            builder.add(
                "compiler.environment." + std::to_string(index) + ".name",
                names[index]);
            builder.add(
                "compiler.environment." + std::to_string(index) + ".value",
                value ? *value : std::string { "<unset>" });
        }
    };
    if (toolchain == HostToolchain::msvc) {
        add(msvc_environment);
    } else {
        add(gcc_environment);
    }
}

[[nodiscard]] std::optional<std::string> read_text_file(
    const std::filesystem::path& path,
    std::error_code& error)
{
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

[[nodiscard]] std::string remove_cpp_comments(const std::string_view source)
{
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
            const auto delimiter = state == State::string_literal ? '"' : '\'';
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

[[nodiscard]] std::optional<IncludeDirective> parse_include_replacement(
    const std::string_view replacement)
{
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
    return IncludeDirective {
        std::string { replacement.substr(begin + 1, end - begin - 1) }, quoted
    };
}

[[nodiscard]] IncludeScan find_includes(
    const std::string_view source,
    const std::vector<std::string>& command_defines)
{
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
            const auto replacement = name_end == define.size() ? std::string { "1" } : define.substr(name_end + 1);
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
        const auto line = std::string_view { without_comments }.substr(
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
                    const auto name = std::string { line.substr(name_begin, position - name_begin) };
                    skip_space();
                    const auto replacement = std::string { line.substr(position) };
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
                const auto name = std::string { line.substr(name_begin, position - name_begin) };
                ambiguous_macro_state = ambiguous_macro_state || macros.contains(name);
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
    const std::filesystem::path& path)
{
    const auto extension = lowercase(path.extension().string());
    return extension.empty() || extension == ".h" || extension == ".hh"
        || extension == ".hpp" || extension == ".hxx" || extension == ".inc"
        || extension == ".ipp" || extension == ".tpp" || extension == ".c"
        || extension == ".cc" || extension == ".cpp" || extension == ".cxx";
}

[[nodiscard]] bool collect_conservative_root_dependencies(
    const std::vector<std::filesystem::path>& roots,
    std::vector<std::filesystem::path>& dependencies,
    diagnostic::Engine& diagnostics)
{
    std::error_code error;
    for (const auto& root : roots) {
        if (!std::filesystem::is_directory(root, error)) {
            error.clear();
            continue;
        }
        std::filesystem::recursive_directory_iterator iterator {
            root, std::filesystem::directory_options::skip_permission_denied, error
        };
        const std::filesystem::recursive_directory_iterator end;
        while (!error && iterator != end) {
            if (iterator->is_regular_file(error)
                && plausible_cpp_dependency(iterator->path())) {
                auto normalized = std::filesystem::weakly_canonical(iterator->path(), error);
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
    std::error_code& error)
{
    error.clear();
    if (!std::filesystem::is_regular_file(path, error)) {
        error.clear();
        return { };
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
    const std::vector<std::filesystem::path>& roots)
{
    std::error_code error;
    if (include.quoted) {
        auto candidate = normalized_existing_path(including_file.parent_path() / include.name, error);
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
    return { };
}

[[nodiscard]] bool add_transitive_dependencies_to_key(
    compiler::CacheKeyBuilder& builder,
    const std::vector<std::filesystem::path>& sources,
    const std::vector<std::filesystem::path>& includes,
    const std::vector<std::string>& command_defines,
    bool& cacheable,
    diagnostic::Engine& diagnostics)
{
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
    std::vector<std::filesystem::path> conservative_scan_roots = include_roots;
    std::unordered_set<std::string> scan_root_names = include_root_names;
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
        needs_conservative_scan = needs_conservative_scan || scan.has_unresolved_macro_include;
        cacheable = cacheable && !scan.has_unresolved_macro_include;
        for (const auto& include : scan.includes) {
            auto resolved = resolve_include(include, current, include_roots);
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
    diagnostic::Engine& diagnostics)
{
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
        const auto candidate = make_absolute(std::filesystem::path { library }, working_directory);
        const auto exists = std::filesystem::is_regular_file(candidate, error);
        error.clear();
        const bool explicit_path = std::filesystem::path { library }.is_absolute()
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
    const std::string& option)
{
    const auto folded = lowercase(option);
    if (toolchain == HostToolchain::msvc) {
        return folded == "/c" || folded == "/e" || folded == "/ep" || folded == "/p"
            || folded == "/link" || folded == "/md" || folded == "/mdd"
            || folded == "/mt" || folded == "/mtd"
            || folded == "/dll" || folded.rfind("/pdb:", 0) == 0
            || folded.rfind("/implib:", 0) == 0
            || folded.rfind("/machine:", 0) == 0
            || folded.rfind("/incremental", 0) == 0
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
    const std::string& option)
{
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
    diagnostic::Engine& diagnostics)
{
    const auto validate = [&](
                              const std::vector<std::string>& options,
                              const bool compile_options) {
        for (const auto& option : options) {
            if (option_changes_output(toolchain, option)) {
                report_error(
                    diagnostics,
                    "FSIM-SC-C004",
                    "SystemC compiler option '" + option
                        + "' conflicts with fsim's shared-library build contract");
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
    const std::string& define)
{
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

[[nodiscard]] bool is_path_like_library(const std::string& library)
{
    if (contains_directory_separator(library)) {
        return true;
    }
    const auto extension = lowercase(std::filesystem::path { library }.extension().string());
    return extension == ".a" || extension == ".so" || extension == ".dylib"
        || extension == ".lib" || extension == ".dll";
}

[[nodiscard]] std::string_view msvc_runtime_option() noexcept
{
#if defined(_MSC_VER)
#if defined(_DLL)
#if defined(_DEBUG)
    return "/MDd";
#else
    return "/MD";
#endif
#else
#if defined(_DEBUG)
    return "/MTd";
#else
    return "/MT";
#endif
#endif
#else
    // Non-Windows command-planning tests model the conventional MSVC
    // dynamic-release runtime. A native MSVC or clang-cl build derives the
    // exact flag from the CRT macros used to compile fsim_systemc itself.
    return "/MD";
#endif
}

[[nodiscard]] constexpr bool msvc_debug_mode() noexcept
{
#if defined(_DEBUG)
    return true;
#else
    return false;
#endif
}

[[nodiscard]] std::vector<std::string> common_compile_argv(
    const HostToolchain toolchain,
    const std::string& compiler_name,
    const std::filesystem::path& resolved_compiler,
    const std::vector<std::filesystem::path>& includes,
    const project::SystemCSection& settings)
{
    std::vector<std::string> argv;
    argv.push_back(
        path_argument(resolved_compiler.empty() ? std::filesystem::path { compiler_name }
                                                : resolved_compiler));

    if (toolchain == HostToolchain::msvc) {
        argv.emplace_back("/nologo");
        argv.emplace_back("/std:c++20");
        argv.emplace_back("/EHsc");
        argv.emplace_back("/utf-8");
        argv.emplace_back("/Zc:__cplusplus");
        argv.emplace_back("/FC");
        argv.emplace_back("/bigobj");
        argv.emplace_back(msvc_runtime_option());
        if (msvc_debug_mode()) {
            argv.emplace_back("/Od");
            argv.emplace_back("/Z7");
        } else {
            argv.emplace_back("/O2");
        }
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
    std::vector<std::filesystem::path>& intermediate_paths)
{
    std::vector<CompilerCommand> commands;
    if (toolchain == HostToolchain::msvc) {
        for (std::size_t index = 0; index < sources.size(); ++index) {
            auto object = output.parent_path() / ("source-" + std::to_string(index) + ".obj");
            auto program_database = output.parent_path() / ("source-" + std::to_string(index) + ".pdb");
            auto argv = common_compile_argv(
                toolchain, compiler_name, resolved_compiler, includes, settings);
            argv.emplace_back("/c");
            argv.push_back(path_argument(sources[index]));
            argv.push_back("/Fo" + path_argument(object));
            argv.push_back("/Fd" + path_argument(program_database));
            commands.push_back({ std::move(argv), working_directory, toolchain });
            intermediate_paths.push_back(std::move(object));
            intermediate_paths.push_back(std::move(program_database));
        }

        std::vector<std::string> link_argv;
        const auto link_stem = output.parent_path() / output.stem();
        auto link_program_database = link_stem;
        link_program_database += ".pdb";
        auto import_library = link_stem;
        import_library += ".lib";
        auto export_file = link_stem;
        export_file += ".exp";
        link_argv.push_back(path_argument(
            resolved_compiler.empty() ? std::filesystem::path { compiler_name }
                                      : resolved_compiler));
        link_argv.emplace_back("/nologo");
        link_argv.emplace_back(msvc_runtime_option());
        link_argv.emplace_back("/LD");
        for (std::size_t index = 0; index < sources.size(); ++index) {
            link_argv.push_back(path_argument(
                output.parent_path() / ("source-" + std::to_string(index) + ".obj")));
        }
        link_argv.push_back("/Fe" + path_argument(output));
        link_argv.emplace_back("/link");
        link_argv.emplace_back("/INCREMENTAL:NO");
        link_argv.emplace_back("/MACHINE:X64");
        link_argv.push_back(
            "/PDB:" + path_argument(link_program_database));
        link_argv.push_back(
            "/IMPLIB:" + path_argument(import_library));
        if (msvc_debug_mode()) {
            link_argv.emplace_back("/DEBUG:FULL");
        }
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
        intermediate_paths.push_back(std::move(link_program_database));
        intermediate_paths.push_back(std::move(import_library));
        intermediate_paths.push_back(std::move(export_file));
        commands.push_back({ std::move(link_argv), working_directory, toolchain });
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
    commands.push_back({ std::move(argv), working_directory, toolchain });
    return commands;
}

[[nodiscard]] bool hash_file(
    const std::filesystem::path& path,
    std::string& result,
    std::error_code& error)
{
    error.clear();
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        error = std::make_error_code(std::errc::io_error);
        return false;
    }

    support::Sha256 hasher;
    std::array<char, 64U * 1024U> buffer { };
    while (stream) {
        stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = stream.gcount();
        if (count > 0) {
            hasher.update(std::as_bytes(std::span { buffer.data(), static_cast<std::size_t>(count) }));
        }
    }
    if (!stream.eof()) {
        error = std::make_error_code(std::errc::io_error);
        return false;
    }
    result = support::Sha256::hex(hasher.finish());
    return true;
}

namespace {

    constexpr std::string_view kArtifactMetadataMagic = "fsim-systemc-artifact-v1";

    [[nodiscard]] std::filesystem::path artifact_metadata_path(
        const std::filesystem::path& library)
    {
        return library.string() + ".metadata";
    }

    [[nodiscard]] bool lowercase_sha256(const std::string_view value) noexcept
    {
        return value.size() == 64
            && std::all_of(value.begin(), value.end(), [](const char character) {
                   return (character >= '0' && character <= '9')
                       || (character >= 'a' && character <= 'f');
               });
    }

    [[nodiscard]] bool remove_stale_file(
        const std::filesystem::path& path,
        diagnostic::Engine& diagnostics)
    {
        std::error_code error;
        const bool exists = std::filesystem::exists(path, error);
        if (!error && (!exists || std::filesystem::remove(path, error))) {
            return true;
        }
        report_error(
            diagnostics,
            "FSIM-SC-C005",
            "cannot remove stale SystemC plug-in build input: " + error.message(),
            path);
        return false;
    }

} // namespace

[[nodiscard]] bool valid_cached_artifact(
    const PluginCompilePlan& plan,
    std::error_code& error)
{
    error.clear();
    if (!std::filesystem::is_regular_file(plan.library_path, error)) {
        error.clear();
        return false;
    }
    const auto size = std::filesystem::file_size(plan.library_path, error);
    if (error || size == 0) {
        error.clear();
        return false;
    }

    std::ifstream metadata_stream(
        artifact_metadata_path(plan.library_path), std::ios::binary);
    if (!metadata_stream) {
        error.clear();
        return false;
    }
    std::string magic;
    std::string key;
    std::string encoded_size;
    std::string expected_checksum;
    std::string trailing;
    if (!std::getline(metadata_stream, magic)
        || !std::getline(metadata_stream, key)
        || !std::getline(metadata_stream, encoded_size)
        || !std::getline(metadata_stream, expected_checksum)
        || std::getline(metadata_stream, trailing)
        || magic != kArtifactMetadataMagic
        || key != plan.cache_key
        || !lowercase_sha256(expected_checksum)) {
        error.clear();
        return false;
    }

    std::uintmax_t expected_size { };
    const auto parsed = std::from_chars(
        encoded_size.data(),
        encoded_size.data() + encoded_size.size(),
        expected_size);
    if (parsed.ec != std::errc { }
        || parsed.ptr != encoded_size.data() + encoded_size.size()
        || expected_size == 0 || expected_size != size) {
        error.clear();
        return false;
    }

    std::string actual;
    if (!hash_file(plan.library_path, actual, error)) {
        return false;
    }
    return actual == expected_checksum;
}

[[nodiscard]] bool prepare_artifact_build(
    const PluginCompilePlan& plan,
    diagnostic::Engine& diagnostics)
{
    if (!remove_stale_file(plan.build_path, diagnostics)) {
        return false;
    }
    for (const auto& intermediate : plan.intermediate_paths) {
        if (!remove_stale_file(intermediate, diagnostics)) {
            return false;
        }
    }

    std::error_code error;
    const auto directory = plan.library_path.parent_path();
    std::filesystem::directory_iterator iterator {
        directory,
        std::filesystem::directory_options::skip_permission_denied,
        error
    };
    if (error) {
        report_error(
            diagnostics,
            "FSIM-SC-C005",
            "cannot inspect SystemC plug-in staging directory: " + error.message(),
            directory);
        return false;
    }
    const auto metadata_prefix = artifact_metadata_path(plan.library_path).filename().string() + ".tmp.";
    const auto legacy_prefix = plan.library_path.filename().string() + ".sha256.tmp.";
    const std::filesystem::directory_iterator end;
    for (; iterator != end; iterator.increment(error)) {
        if (error) {
            break;
        }
        const auto filename = iterator->path().filename().string();
        if (!filename.starts_with(metadata_prefix)
            && !filename.starts_with(legacy_prefix)) {
            continue;
        }
        if (!remove_stale_file(iterator->path(), diagnostics)) {
            return false;
        }
    }
    if (error) {
        report_error(
            diagnostics,
            "FSIM-SC-C005",
            "cannot inspect SystemC plug-in staging directory: " + error.message(),
            directory);
        return false;
    }
    return true;
}

[[nodiscard]] bool publish_artifact(
    const PluginCompilePlan& plan,
    diagnostic::Engine& diagnostics)
{
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

    const auto artifact_size = std::filesystem::file_size(plan.build_path, error);
    if (error || artifact_size == 0) {
        report_error(
            diagnostics,
            "FSIM-SC-C008",
            "cannot size compiled SystemC plug-in: " + error.message(),
            plan.build_path);
        return false;
    }

    const auto metadata_path = artifact_metadata_path(plan.library_path);
    const auto suffix = checksum_temporary_counter.fetch_add(1, std::memory_order_relaxed);
    const auto temporary_metadata = std::filesystem::path {
        metadata_path.string() + ".tmp."
        + std::to_string(dependency_process_id()) + "-"
        + std::to_string(suffix)
    };
    {
        std::ofstream stream(
            temporary_metadata, std::ios::binary | std::ios::trunc);
        stream << kArtifactMetadataMagic << '\n'
               << plan.cache_key << '\n'
               << artifact_size << '\n'
               << checksum << '\n';
        stream.flush();
        if (!stream) {
            report_error(
                diagnostics,
                "FSIM-SC-C008",
                "cannot write SystemC plug-in metadata",
                temporary_metadata);
            std::filesystem::remove(temporary_metadata, error);
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
        std::filesystem::remove(temporary_metadata, error);
        return false;
    }

    if (!compiler::detail::atomic_replace_file(
            temporary_metadata, metadata_path, error)) {
        report_error(
            diagnostics,
            "FSIM-SC-C008",
            "cannot commit SystemC plug-in metadata: " + error.message(),
            metadata_path);
        std::error_code ignored;
        std::filesystem::remove(temporary_metadata, ignored);
        std::filesystem::remove(plan.library_path, ignored);
        return false;
    }
    std::filesystem::remove(plan.library_path.string() + ".sha256", error);
    return true;
}

} // namespace fsim::systemc::plugin_detail
