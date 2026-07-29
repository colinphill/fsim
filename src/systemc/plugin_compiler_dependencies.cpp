// SPDX-License-Identifier: Apache-2.0
#include "plugin_compiler_internal.hpp"

namespace fsim::systemc::plugin_detail {

[[nodiscard]] std::uint64_t dependency_process_id() noexcept {
#if defined(_WIN32)
    return static_cast<std::uint64_t>(GetCurrentProcessId());
#else
    return static_cast<std::uint64_t>(::getpid());
#endif
}

DependencyScratchDirectory::DependencyScratchDirectory(std::filesystem::path path)
        : path_(std::move(path))  {}

DependencyScratchDirectory::DependencyScratchDirectory(DependencyScratchDirectory&& other) noexcept
    : path_(std::exchange(other.path_, {}))  {}

DependencyScratchDirectory::~DependencyScratchDirectory()  {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

[[nodiscard]] const std::filesystem::path& DependencyScratchDirectory::path() const noexcept  {
        return path_;
    }

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
    const std::filesystem::path& source,
    const CompilerCommand* command,
    const ProcessResult* process) {
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



} // namespace fsim::systemc::plugin_detail
