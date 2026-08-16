// SPDX-License-Identifier: Apache-2.0
#include "fsim/systemc/incremental.hpp"

#include "plugin_compiler_internal.hpp"
#include "producer_fingerprint.hpp"

#include "fsim/support/path.hpp"
#include "fsim/support/sha256.hpp"
#include "fsim/systemc_abi.h"
#include "fsim/version.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <system_error>
#include <unordered_map>
#include <unordered_set>

namespace fsim::systemc {

bool publish_incremental_object(
    const std::filesystem::path& destination,
    const IncrementalObjectMetadata& metadata,
    std::string_view payload,
    diagnostic::Engine& diagnostics);

bool publish_incremental_plugin(
    const std::filesystem::path& destination,
    const IncrementalPluginMetadata& metadata,
    std::string_view payload,
    diagnostic::Engine& diagnostics);

namespace {
    using namespace plugin_detail;

    constexpr std::string_view kCompileCode = "FSIM-SC-I004";
    constexpr std::string_view kLinkCode = "FSIM-SC-I005";
    std::atomic_uint64_t scratch_sequence { };

    void report(
        diagnostic::Engine& diagnostics,
        const std::string_view code,
        std::string message,
        const std::filesystem::path& path = { })
    {
        diagnostic::SourceSpan span;
        span.path = support::path_to_utf8(path);
        span.begin = { 1, 1, 0 };
        span.end = span.begin;
        diagnostics.error(std::string { code }, std::move(message), std::move(span));
    }

    std::string target_identity()
    {
#if defined(_WIN32) && defined(__MINGW32__)
        return "x86_64-w64-windows-gnu";
#elif defined(_WIN32)
        return "x86_64-pc-windows-msvc";
#elif defined(__linux__)
        return "x86_64-unknown-linux-gnu";
#else
        return "x86_64-unknown";
#endif
    }

    std::string object_filename()
    {
#if defined(_WIN32)
        return "translation-unit.obj";
#else
        return "translation-unit.o";
#endif
    }

    std::string plugin_filename()
    {
#if defined(_WIN32)
        return "fsim-systemc-plugin.dll";
#elif defined(__APPLE__)
        return "libfsim-systemc-plugin.dylib";
#else
        return "libfsim-systemc-plugin.so";
#endif
    }

    std::filesystem::path working_directory(
        const std::filesystem::path& requested,
        diagnostic::Engine& diagnostics)
    {
        std::error_code error;
        auto result = requested.empty()
            ? std::filesystem::current_path(error)
            : std::filesystem::absolute(requested, error);
        if (error) {
            report(
                diagnostics, kCompileCode,
                "cannot resolve SystemC working directory: " + error.message(),
                requested);
            return { };
        }
        result = result.lexically_normal();
        if (!std::filesystem::is_directory(result, error) || error) {
            report(
                diagnostics, kCompileCode,
                "SystemC working directory does not exist", result);
            return { };
        }
        return result;
    }

    std::filesystem::path absolute_from(
        const std::filesystem::path& path,
        const std::filesystem::path& base)
    {
        return (path.is_absolute() ? path : base / path).lexically_normal();
    }

    std::filesystem::path scratch_root(
        const std::filesystem::path& requested,
        const std::filesystem::path& base,
        diagnostic::Engine& diagnostics)
    {
        auto parent = requested.empty()
            ? std::filesystem::temp_directory_path()
            : absolute_from(requested, base);
        std::error_code error;
        std::filesystem::create_directories(parent, error);
        if (error) {
            report(
                diagnostics, kCompileCode,
                "cannot create SystemC scratch parent: " + error.message(), parent);
            return { };
        }
        for (std::size_t attempt = 0; attempt < 100; ++attempt) {
            const auto serial = scratch_sequence.fetch_add(1, std::memory_order_relaxed);
            const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
            const auto path = parent / ("fsim-systemc-phase-" + std::to_string(tick) + "-" + std::to_string(serial));
            if (std::filesystem::create_directory(path, error)) {
                return path;
            }
            if (error) {
                report(
                    diagnostics, kCompileCode,
                    "cannot create SystemC scratch directory: " + error.message(), path);
                return { };
            }
        }
        report(
            diagnostics, kCompileCode,
            "cannot allocate a unique SystemC scratch directory", parent);
        return { };
    }

    class ScratchCleanup {
    public:
        explicit ScratchCleanup(std::filesystem::path path)
            : path_(std::move(path))
        {
        }
        ~ScratchCleanup()
        {
            std::error_code error;
            std::filesystem::remove_all(path_, error);
        }

    private:
        std::filesystem::path path_;
    };

    project::SystemCSection effective_compile_settings(
        project::SystemCSection settings)
    {
        plugin_detail::add_default_compiler_settings(settings);
#if defined(FSIM_SYSTEMC_HEADER_PATH)
        auto header = std::filesystem::path { FSIM_SYSTEMC_HEADER_PATH };
#if defined(FSIM_SYSTEMC_INSTALLED_HEADER_PATH)
        std::error_code error;
        if (!std::filesystem::is_directory(header, error)) {
            header = FSIM_SYSTEMC_INSTALLED_HEADER_PATH;
        }
#endif
        settings.include_directories.insert(
            settings.include_directories.begin(), std::move(header));
#endif
#if defined(FSIM_SYSTEMC_UPSTREAM_HEADER_PATH)
        auto upstream_header
            = std::filesystem::path { FSIM_SYSTEMC_UPSTREAM_HEADER_PATH };
#if defined(FSIM_SYSTEMC_INSTALLED_UPSTREAM_HEADER_PATH)
        std::error_code upstream_error;
        if (!std::filesystem::is_directory(upstream_header, upstream_error)) {
            upstream_header = FSIM_SYSTEMC_INSTALLED_UPSTREAM_HEADER_PATH;
        }
#endif
        settings.include_directories.insert(
            std::next(settings.include_directories.begin()),
            std::move(upstream_header));
#endif
#if defined(FSIM_SCV_HEADER_PATH)
        auto scv_header = std::filesystem::path { FSIM_SCV_HEADER_PATH };
#if defined(FSIM_INSTALLED_SCV_HEADER_PATH)
        std::error_code scv_header_error;
        if (!std::filesystem::is_directory(scv_header, scv_header_error)) {
            scv_header = FSIM_INSTALLED_SCV_HEADER_PATH;
        }
#endif
        settings.include_directories.push_back(std::move(scv_header));
#endif
        settings.link_options.clear();
        settings.libraries.clear();
        return settings;
    }

    std::filesystem::path accellera_runtime_library()
    {
#if defined(FSIM_SYSTEMC_ACCELERA_LIBRARY_PATH)
        auto result = std::filesystem::path { FSIM_SYSTEMC_ACCELERA_LIBRARY_PATH };
#if defined(FSIM_SYSTEMC_INSTALLED_ACCELERA_LIBRARY_PATH)
        std::error_code error;
        if (!std::filesystem::is_regular_file(result, error)) {
            result = FSIM_SYSTEMC_INSTALLED_ACCELERA_LIBRARY_PATH;
        }
#endif
        return result;
#else
        return { };
#endif
    }

    std::filesystem::path official_runtime_library()
    {
#if defined(FSIM_SYSTEMC_OFFICIAL_LIBRARY_PATH)
        auto result = std::filesystem::path { FSIM_SYSTEMC_OFFICIAL_LIBRARY_PATH };
#if defined(FSIM_SYSTEMC_INSTALLED_OFFICIAL_LIBRARY_PATH)
        std::error_code error;
        if (!std::filesystem::is_regular_file(result, error)) {
            result = FSIM_SYSTEMC_INSTALLED_OFFICIAL_LIBRARY_PATH;
        }
#endif
        return result;
#else
        return { };
#endif
    }

    std::filesystem::path scv_runtime_library()
    {
#if defined(FSIM_SCV_LIBRARY_PATH)
        auto result = std::filesystem::path { FSIM_SCV_LIBRARY_PATH };
#if defined(FSIM_INSTALLED_SCV_LIBRARY_PATH)
        std::error_code error;
        if (!std::filesystem::is_regular_file(result, error)) {
            result = FSIM_INSTALLED_SCV_LIBRARY_PATH;
        }
#endif
        return result;
#else
        return { };
#endif
    }

    std::filesystem::path plugin_export_library()
    {
#if defined(FSIM_SYSTEMC_PLUGIN_EXPORT_LIBRARY_PATH)
        auto result
            = std::filesystem::path { FSIM_SYSTEMC_PLUGIN_EXPORT_LIBRARY_PATH };
#if defined(FSIM_SYSTEMC_INSTALLED_PLUGIN_EXPORT_LIBRARY_PATH)
        std::error_code error;
        if (!std::filesystem::is_regular_file(result, error)) {
            result = FSIM_SYSTEMC_INSTALLED_PLUGIN_EXPORT_LIBRARY_PATH;
        }
#endif
        return result;
#else
        return { };
#endif
    }

    std::optional<std::string> compiler_fingerprint(
        const project::SystemCSection& settings,
        const std::filesystem::path& base,
        diagnostic::Engine& diagnostics)
    {
        return plugin_producer_fingerprint(settings, base, diagnostics);
    }

    std::optional<std::string> read_binary(
        const std::filesystem::path& path,
        diagnostic::Engine& diagnostics,
        const std::string_view operation,
        const std::string_view code)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            report(
                diagnostics, code,
                "cannot open " + std::string { operation }, path);
            return std::nullopt;
        }
        std::ostringstream bytes;
        bytes << input.rdbuf();
        if (!input.good() && !input.eof()) {
            report(
                diagnostics, code,
                "cannot read " + std::string { operation }, path);
            return std::nullopt;
        }
        return std::move(bytes).str();
    }

    bool run_command(
        const CompilerCommand& command,
        diagnostic::Engine& diagnostics,
        const std::string_view phase,
        const std::string_view code)
    {
        const auto process = run_process(command);
        if (process.started && process.execution_error.empty()
            && process.exit_code == 0) {
            return true;
        }
        diagnostic::Diagnostic diagnostic;
        diagnostic.severity = diagnostic::Severity::error;
        diagnostic.code = std::string { code };
        diagnostic.message = !process.started
            ? std::string { phase } + " could not start the selected compiler: "
                + process.start_error
            : !process.execution_error.empty()
            ? std::string { phase } + " compiler process failed: "
                + process.execution_error
            : std::string { phase } + " compiler exited with status "
                + std::to_string(process.exit_code);
        diagnostic.notes.push_back(
            { "argv: " + format_compiler_command(command), { } });
        if (!process.output.empty()) {
            diagnostic.notes.push_back({ "compiler output:\n" + process.output, { } });
        }
        diagnostics.report(std::move(diagnostic));
        return false;
    }

    std::vector<std::filesystem::path> absolute_includes(
        const project::SystemCSection& settings,
        const std::filesystem::path& base)
    {
        std::vector<std::filesystem::path> result;
        result.reserve(settings.include_directories.size());
        for (const auto& include : settings.include_directories) {
            result.push_back(absolute_from(include, base));
        }
        return result;
    }

    bool is_clang_cl(
        const std::string& compiler,
        const std::filesystem::path& resolved)
    {
        const auto matches = [](const std::filesystem::path& candidate) {
            const auto filename = lowercase(candidate.filename().string());
            return filename == "clang-cl" || filename == "clang-cl.exe";
        };
        return matches(std::filesystem::path { compiler })
            || (!resolved.empty() && matches(resolved));
    }

    std::optional<std::vector<std::filesystem::path>> gcc_dependencies(
        const std::string& compiler,
        const std::filesystem::path& resolved,
        const std::filesystem::path& source,
        const std::vector<std::filesystem::path>& includes,
        const project::SystemCSection& settings,
        const std::filesystem::path& base,
        const std::filesystem::path& scratch,
        diagnostic::Engine& diagnostics)
    {
        const auto dependency_file = scratch / "translation-unit.d";
        auto argv = common_compile_argv(
            HostToolchain::gcc_like, compiler, resolved, includes, settings);
        argv.emplace_back("-M");
        argv.emplace_back("-MF");
        argv.push_back(path_argument(dependency_file));
        argv.emplace_back("-MT");
        argv.emplace_back("fsim_systemc_object");
        argv.push_back(path_argument(source));
        CompilerCommand command {
            std::move(argv), base, HostToolchain::gcc_like
        };
        if (!run_command(command, diagnostics, "SystemC dependency scan", kCompileCode)) {
            return std::nullopt;
        }
        std::error_code error;
        const auto bytes = read_text_file(dependency_file, error);
        const auto parsed = bytes
            ? parse_makefile_dependencies(*bytes)
            : std::nullopt;
        if (!parsed) {
            report(
                diagnostics, kCompileCode,
                "compiler emitted an invalid SystemC dependency file", dependency_file);
            return std::nullopt;
        }
        std::vector<std::filesystem::path> dependencies;
        dependencies.reserve(parsed->size());
        for (const auto& name : *parsed) {
            const auto dependency = support::path_from_utf8(name);
            auto path = normalized_existing_path(
                absolute_from(dependency, base), error);
            if (path.empty()) {
                report(
                    diagnostics, kCompileCode,
                    "compiler dependency is not a readable regular file",
                    dependency);
                return std::nullopt;
            }
            dependencies.push_back(std::move(path));
        }
        return dependencies;
    }

    std::optional<std::vector<std::filesystem::path>> msvc_dependencies(
        const std::string& compiler,
        const std::filesystem::path& resolved,
        const std::filesystem::path& source,
        const std::vector<std::filesystem::path>& includes,
        const project::SystemCSection& settings,
        const std::filesystem::path& base,
        const std::filesystem::path& scratch,
        diagnostic::Engine& diagnostics)
    {
        const auto dependency_file = scratch / "translation-unit.json";
        const auto object = scratch / "dependency-scan.obj";
        const auto program_database = scratch / "dependency-scan.pdb";
        auto argv = common_compile_argv(
            HostToolchain::msvc, compiler, resolved, includes, settings);
        argv.emplace_back("/c");
        argv.push_back(path_argument(source));
        argv.push_back("/Fo" + path_argument(object));
        argv.push_back("/Fd" + path_argument(program_database));
        argv.emplace_back("/sourceDependencies");
        argv.push_back(path_argument(dependency_file));
        CompilerCommand command { std::move(argv), base, HostToolchain::msvc };
        if (!run_command(command, diagnostics, "SystemC dependency scan", kCompileCode)) {
            return std::nullopt;
        }
        std::error_code error;
        const auto bytes = read_text_file(dependency_file, error);
        auto parsed = bytes
            ? parse_msvc_source_dependencies(*bytes)
            : std::nullopt;
        if (!parsed) {
            report(
                diagnostics, kCompileCode,
                "compiler emitted invalid SystemC source-dependency JSON",
                dependency_file);
            return std::nullopt;
        }
        for (auto& path : *parsed) {
            path = normalized_existing_path(path, error);
            if (path.empty()) {
                report(
                    diagnostics, kCompileCode,
                    "compiler dependency is not a readable regular file");
                return std::nullopt;
            }
        }
        return parsed;
    }

    std::optional<std::vector<std::filesystem::path>> clang_cl_dependencies(
        const std::string& compiler,
        const std::filesystem::path& resolved,
        const std::filesystem::path& source,
        const std::vector<std::filesystem::path>& includes,
        const project::SystemCSection& settings,
        const std::filesystem::path& base,
        const std::filesystem::path& scratch,
        diagnostic::Engine& diagnostics)
    {
        const auto dependency_file = scratch / "translation-unit.d";
        const auto object = scratch / "dependency-scan.obj";
        const auto program_database = scratch / "dependency-scan.pdb";
        auto argv = common_compile_argv(
            HostToolchain::msvc, compiler, resolved, includes, settings);
        argv.emplace_back("/c");
        argv.push_back(path_argument(source));
        argv.push_back("/Fo" + path_argument(object));
        argv.push_back("/Fd" + path_argument(program_database));
        argv.emplace_back("/clang:-MD");
        argv.emplace_back("/clang:-MF");
        argv.push_back("/clang:" + path_argument(dependency_file));
        argv.emplace_back("/clang:-MT");
        argv.emplace_back("/clang:fsim_systemc_object");
        CompilerCommand command { std::move(argv), base, HostToolchain::msvc };
        if (!run_command(command, diagnostics, "SystemC dependency scan", kCompileCode)) {
            return std::nullopt;
        }
        std::error_code error;
        const auto bytes = read_text_file(dependency_file, error);
        const auto parsed = bytes
            ? parse_makefile_dependencies(*bytes)
            : std::nullopt;
        if (!parsed) {
            report(
                diagnostics, kCompileCode,
                "compiler emitted an invalid SystemC dependency file", dependency_file);
            return std::nullopt;
        }
        std::vector<std::filesystem::path> dependencies;
        dependencies.reserve(parsed->size());
        for (const auto& name : *parsed) {
            const auto dependency = support::path_from_utf8(name);
            auto path = normalized_existing_path(
                absolute_from(dependency, base), error);
            if (path.empty()) {
                report(
                    diagnostics, kCompileCode,
                    "compiler dependency is not a readable regular file",
                    dependency);
                return std::nullopt;
            }
            dependencies.push_back(std::move(path));
        }
        return dependencies;
    }

    std::optional<std::vector<std::filesystem::path>> dependencies(
        const HostToolchain toolchain,
        const std::string& compiler,
        const std::filesystem::path& resolved,
        const std::filesystem::path& source,
        const std::vector<std::filesystem::path>& includes,
        const project::SystemCSection& settings,
        const std::filesystem::path& base,
        const std::filesystem::path& scratch,
        diagnostic::Engine& diagnostics)
    {
        auto result = toolchain == HostToolchain::gcc_like
            ? gcc_dependencies(
                  compiler, resolved, source, includes, settings, base, scratch,
                  diagnostics)
            : is_clang_cl(compiler, resolved)
                ? clang_cl_dependencies(
                      compiler, resolved, source, includes, settings, base, scratch,
                      diagnostics)
                : msvc_dependencies(
                      compiler, resolved, source, includes, settings, base, scratch,
                      diagnostics);
        if (!result) {
            return std::nullopt;
        }
        std::ranges::sort(*result, { }, [](const auto& path) {
            return path.generic_string();
        });
        result->erase(std::unique(result->begin(), result->end()), result->end());
        return result;
    }

    std::optional<std::string> hash_path(
        const std::filesystem::path& path,
        diagnostic::Engine& diagnostics,
        const std::string_view code)
    {
        std::string digest;
        std::error_code error;
        if (!hash_file(path, digest, error)) {
            report(
                diagnostics, code,
                "cannot hash native build input: " + error.message(), path);
            return std::nullopt;
        }
        return digest;
    }

    std::string logical_input_name(
        const std::filesystem::path& path,
        const std::filesystem::path& source,
        const std::size_t index)
    {
        if (path == source) {
            return "source/" + source.filename().generic_string();
        }
        std::ostringstream name;
        name << "dependency/" << std::setw(8) << std::setfill('0') << index
             << '/' << path.filename().generic_string();
        return name.str();
    }

    std::optional<std::vector<NativeInputIdentity>> input_identities(
        const std::filesystem::path& source,
        const std::vector<std::filesystem::path>& inputs,
        diagnostic::Engine& diagnostics)
    {
        std::vector<NativeInputIdentity> result;
        result.reserve(inputs.size());
        for (std::size_t index = 0; index < inputs.size(); ++index) {
            auto digest = hash_path(inputs[index], diagnostics, kCompileCode);
            if (!digest) {
                return std::nullopt;
            }
            result.push_back({ logical_input_name(inputs[index], source, index), std::move(*digest) });
        }
        return result;
    }

    bool inputs_unchanged(
        const std::vector<std::filesystem::path>& paths,
        const std::vector<NativeInputIdentity>& expected,
        diagnostic::Engine& diagnostics)
    {
        if (paths.size() != expected.size()) {
            return false;
        }
        for (std::size_t index = 0; index < paths.size(); ++index) {
            auto digest = hash_path(paths[index], diagnostics, kCompileCode);
            if (!digest || *digest != expected[index].checksum) {
                report(
                    diagnostics, kCompileCode,
                    "SystemC source or dependency changed during compilation; the "
                    "unpublished object was discarded",
                    paths[index]);
                return false;
            }
        }
        return true;
    }

    std::optional<IncrementalFactory> factory_record(
        const HierarchyRegistry& registry,
        const std::string& name,
        diagnostic::Engine& diagnostics)
    {
        auto parameters = registry.factory_parameters(name);
        if (!parameters) {
            report(
                diagnostics, kLinkCode,
                "linked SystemC factory has an invalid parameter schema: " + name);
            return std::nullopt;
        }
        IncrementalFactory result;
        result.name = name;
        for (const auto& parameter : *parameters) {
            result.parameters.push_back({ parameter.name,
                static_cast<std::uint32_t>(parameter.type),
                parameter.default_value.has_value(),
                parameter.default_value.value_or(0) });
        }
        return result;
    }

    bool matching_inventory(
        const HierarchyRegistry& registry,
        const std::vector<IncrementalFactory>& expected,
        diagnostic::Engine& diagnostics)
    {
        const auto names = registry.factory_names();
        if (names.size() != expected.size()) {
            report(
                diagnostics, kLinkCode,
                "SystemC plug-in runtime factory inventory differs from its metadata");
            return false;
        }
        for (std::size_t index = 0; index < names.size(); ++index) {
            auto actual = factory_record(registry, names[index], diagnostics);
            if (!actual || *actual != expected[index]) {
                if (!diagnostics.has_error()) {
                    report(
                        diagnostics, kLinkCode,
                        "SystemC plug-in factory metadata differs at '" + names[index] + "'");
                }
                return false;
            }
        }
        return true;
    }

    std::optional<std::string> planned_object_input_digest(
        const IncrementalCompileRequest& request,
        diagnostic::Engine& diagnostics)
    {
        const auto base = working_directory(request.working_directory, diagnostics);
        if (base.empty()) {
            return std::nullopt;
        }
        const auto source = absolute_from(request.source, base);
        std::error_code error;
        if (!source_extension_supported(source)
            || !std::filesystem::is_regular_file(source, error)) {
            report(
                diagnostics, kCompileCode,
                "SystemC compile requires one existing .cpp, .cc, or .cxx source",
                source);
            return std::nullopt;
        }
        auto settings = effective_compile_settings(request.settings);
        const auto compiler = settings.compiler.empty()
            ? default_compiler()
            : settings.compiler;
        const auto toolchain = infer_toolchain(compiler);
        if (!validate_options(settings, toolchain, diagnostics)) {
            return std::nullopt;
        }
        const auto resolved = resolve_executable(compiler, base);
        auto fingerprint = compiler_fingerprint(settings, base, diagnostics);
        if (resolved.empty() || !fingerprint) {
            if (resolved.empty()) {
                report(
                    diagnostics, kCompileCode,
                    "cannot find selected SystemC compiler '" + compiler + "'");
            }
            return std::nullopt;
        }
        const auto scratch = scratch_root(
            request.scratch_directory, base, diagnostics);
        if (scratch.empty()) {
            return std::nullopt;
        }
        ScratchCleanup cleanup { scratch };
        const auto includes = absolute_includes(settings, base);
        auto discovered = dependencies(
            toolchain, compiler, resolved, source, includes, settings, base,
            scratch, diagnostics);
        if (!discovered) {
            return std::nullopt;
        }
        if (std::ranges::find(*discovered, source) == discovered->end()) {
            discovered->insert(discovered->begin(), source);
        } else {
            std::ranges::rotate(
                *discovered, std::ranges::find(*discovered, source));
        }
        auto inputs = input_identities(source, *discovered, diagnostics);
        auto source_bytes = read_binary(
            source, diagnostics, "SystemC source", kCompileCode);
        if (!inputs || !source_bytes) {
            return std::nullopt;
        }
        IncrementalObjectMetadata metadata;
        metadata.runtime_abi = runtime_abi_version;
        metadata.systemc_abi = FSIM_SYSTEMC_ABI_VERSION;
        metadata.scv_compatibility = fsim_scv_compatibility_identity();
        metadata.toolchain = std::string { to_string(toolchain) };
        metadata.target = target_identity();
        metadata.compiler_fingerprint = std::move(*fingerprint);
        metadata.defines = settings.defines;
        metadata.compile_options = settings.compile_options;
        metadata.inputs = std::move(*inputs);
        metadata.defines_plugin_entry_point = source_bytes->find("fsim_plugin_init_v1") != std::string::npos;
        metadata.contains_macro_export = source_bytes->find("SC_FSIM_EXPORT") != std::string::npos;
        return compute_incremental_object_input_digest(metadata);
    }

    std::optional<std::string> planned_plugin_input_digest(
        const IncrementalLinkRequest& request,
        diagnostic::Engine& diagnostics)
    {
        const auto base = working_directory(request.working_directory, diagnostics);
        if (base.empty() || request.objects.empty()) {
            return std::nullopt;
        }
        IncrementalPluginMetadata plugin;
        plugin.runtime_abi = runtime_abi_version;
        plugin.systemc_abi = FSIM_SYSTEMC_ABI_VERSION;
        plugin.scv_compatibility = fsim_scv_compatibility_identity();
        plugin.logical_library = request.logical_library;
        plugin.link_options = request.settings.link_options;
        plugin.libraries = request.settings.libraries;
        std::unordered_set<std::string> seen;
        for (const auto& object_path : request.objects) {
            auto object = load_incremental_object_metadata(
                absolute_from(object_path, base), diagnostics);
            if (!object || !seen.insert(object->compilation_digest).second) {
                if (object && !diagnostics.has_error()) {
                    report(
                        diagnostics, kLinkCode,
                        "duplicate SystemC object compilation identity", object_path);
                }
                return std::nullopt;
            }
            if (plugin.toolchain.empty()) {
                plugin.toolchain = object->toolchain;
                plugin.target = object->target;
                plugin.compiler_fingerprint = object->compiler_fingerprint;
            } else if (plugin.toolchain != object->toolchain
                || plugin.target != object->target
                || plugin.compiler_fingerprint != object->compiler_fingerprint) {
                report(
                    diagnostics, kLinkCode,
                    "SystemC link inputs have incompatible toolchain identities");
                return std::nullopt;
            }
            plugin.object_digests.push_back(object->compilation_digest);
        }
        return compute_incremental_plugin_input_digest(plugin);
    }

    std::filesystem::path cache_artifact_path(
        const std::filesystem::path& root,
        const std::string_view kind,
        const std::string_view digest,
        const std::string_view extension)
    {
        return root / "systemc" / "incremental" / kind
            / std::string { digest.substr(0, 2) }
        / (std::string { digest } + std::string { extension });
    }

    std::unique_ptr<compiler::detail::CacheDirectoryLock> lock_cache_key(
        const std::filesystem::path& root,
        const std::string& digest,
        diagnostic::Engine& diagnostics)
    {
        const auto directory = root / "systemc" / "incremental" / "locks";
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        if (error) {
            report(
                diagnostics, kCompileCode,
                "cannot create incremental SystemC cache lock directory: "
                    + error.message(),
                directory);
            return { };
        }
        constexpr std::size_t lock_digest_characters = 32U;
        const auto lock_digest = digest.substr(0, lock_digest_characters);
        auto lock = std::make_unique<compiler::detail::CacheDirectoryLock>(
            directory / (lock_digest + ".lock"), error);
        if (!lock->held()) {
            report(
                diagnostics, kCompileCode,
                "cannot acquire incremental SystemC cache lock: " + error.message(),
                directory);
            return { };
        }
        return lock;
    }

} // namespace

bool compile_incremental_object(
    const IncrementalCompileRequest& request,
    diagnostic::Engine& diagnostics)
{
    const auto base = working_directory(request.working_directory, diagnostics);
    if (base.empty() || request.output.empty()) {
        if (request.output.empty()) {
            report(diagnostics, kCompileCode, "SystemC object output must not be empty");
        }
        return false;
    }
    const auto source = absolute_from(request.source, base);
    std::error_code error;
    if (!source_extension_supported(source)
        || !std::filesystem::is_regular_file(source, error)) {
        report(
            diagnostics, kCompileCode,
            "SystemC compile requires one existing .cpp, .cc, or .cxx source",
            source);
        return false;
    }
    auto settings = effective_compile_settings(request.settings);
    const auto compiler = settings.compiler.empty()
        ? default_compiler()
        : settings.compiler;
    const auto toolchain = infer_toolchain(compiler);
    if (!validate_options(settings, toolchain, diagnostics)) {
        return false;
    }
    const auto resolved = resolve_executable(compiler, base);
    if (resolved.empty()) {
        report(
            diagnostics, kCompileCode,
            "cannot find selected SystemC compiler '" + compiler + "'");
        return false;
    }
    auto fingerprint = compiler_fingerprint(settings, base, diagnostics);
    if (!fingerprint) {
        return false;
    }
    const auto scratch = scratch_root(
        request.scratch_directory, base, diagnostics);
    if (scratch.empty()) {
        return false;
    }
    ScratchCleanup cleanup { scratch };
    const auto includes = absolute_includes(settings, base);
    auto discovered = dependencies(
        toolchain, compiler, resolved, source, includes, settings, base,
        scratch, diagnostics);
    if (!discovered) {
        return false;
    }
    if (std::ranges::find(*discovered, source) == discovered->end()) {
        discovered->insert(discovered->begin(), source);
    } else {
        std::ranges::rotate(
            *discovered, std::ranges::find(*discovered, source));
    }
    if (const auto volatile_input = find_volatile_macro_input(*discovered)) {
        report(
            diagnostics, kCompileCode,
            "incremental SystemC artifacts reject volatile predefined macro '"
                + volatile_input->macro + "'",
            volatile_input->path);
        return false;
    }
    auto inputs = input_identities(source, *discovered, diagnostics);
    if (!inputs) {
        return false;
    }
    const auto object = scratch / object_filename();
    auto argv = common_compile_argv(
        toolchain, compiler, resolved, includes, settings);
    if (toolchain == HostToolchain::msvc) {
        argv.emplace_back("/c");
        argv.push_back(path_argument(source));
        argv.push_back("/Fo" + path_argument(object));
        argv.push_back("/Fd" + path_argument(scratch / "translation-unit.pdb"));
    } else {
        argv.emplace_back("-c");
        argv.push_back(path_argument(source));
        argv.emplace_back("-o");
        argv.push_back(path_argument(object));
    }
    const CompilerCommand command { std::move(argv), base, toolchain };
    if (!run_command(
            command, diagnostics, "SystemC translation-unit compilation",
            kCompileCode)
        || !std::filesystem::is_regular_file(object, error)
        || std::filesystem::file_size(object, error) == 0) {
        if (!diagnostics.has_error()) {
            report(
                diagnostics, kCompileCode,
                "SystemC compiler did not produce a native object", object);
        }
        return false;
    }
    if (!inputs_unchanged(*discovered, *inputs, diagnostics)) {
        return false;
    }
    auto payload = read_binary(
        object, diagnostics, "compiled SystemC object", kCompileCode);
    if (!payload) {
        return false;
    }
    IncrementalObjectMetadata metadata;
    metadata.runtime_abi = runtime_abi_version;
    metadata.systemc_abi = FSIM_SYSTEMC_ABI_VERSION;
    metadata.scv_compatibility = fsim_scv_compatibility_identity();
    metadata.producer = std::string { "fsim " } + std::string { version };
    metadata.toolchain = std::string { to_string(toolchain) };
    metadata.target = target_identity();
    metadata.compiler_fingerprint = std::move(*fingerprint);
    metadata.defines = settings.defines;
    metadata.compile_options = settings.compile_options;
    metadata.inputs = std::move(*inputs);
    const auto source_bytes = read_binary(
        source, diagnostics, "SystemC source", kCompileCode);
    if (!source_bytes) {
        return false;
    }
    metadata.defines_plugin_entry_point = source_bytes->find("fsim_plugin_init_v1") != std::string::npos;
    metadata.contains_macro_export = source_bytes->find("SC_FSIM_EXPORT") != std::string::npos;
    metadata.object = std::filesystem::path { "native" } / object_filename();
    metadata.object_checksum = support::Sha256::hex(
        support::Sha256::digest(*payload));
    metadata.input_digest = compute_incremental_object_input_digest(metadata);
    metadata.compilation_digest = compute_incremental_object_digest(metadata);
    return publish_incremental_object(
        absolute_from(request.output, base), metadata, *payload, diagnostics);
}

bool link_incremental_plugin(
    const IncrementalLinkRequest& request,
    diagnostic::Engine& diagnostics)
{
    const auto base = working_directory(request.working_directory, diagnostics);
    if (base.empty() || request.output.empty() || request.objects.empty()) {
        report(
            diagnostics, kLinkCode,
            "SystemC link requires object inputs and a non-empty output");
        return false;
    }
    std::vector<IncrementalObjectMetadata> metadata;
    std::vector<std::filesystem::path> payloads;
    std::unordered_set<std::string> paths;
    std::unordered_set<std::string> digests;
    for (const auto& input : request.objects) {
        const auto directory = absolute_from(input, base);
        if (!paths.insert(directory.generic_string()).second) {
            report(
                diagnostics, kLinkCode,
                "duplicate SystemC object input", directory);
            return false;
        }
        auto object = load_incremental_object_metadata(directory, diagnostics);
        if (!object) {
            return false;
        }
        if (!digests.insert(object->compilation_digest).second) {
            report(
                diagnostics, kLinkCode,
                "duplicate SystemC object compilation identity", directory);
            return false;
        }
        payloads.push_back(directory / object->object);
        metadata.push_back(std::move(*object));
    }
    const auto& first = metadata.front();
    if (std::ranges::any_of(metadata, [&](const auto& object) {
            return object.runtime_abi != first.runtime_abi
                || object.systemc_abi != first.systemc_abi
                || object.scv_compatibility != first.scv_compatibility
                || object.toolchain != first.toolchain
                || object.target != first.target
                || object.compiler_fingerprint != first.compiler_fingerprint;
        })) {
        report(
            diagnostics, kLinkCode,
            "SystemC link inputs have incompatible runtime, ABI, target, "
            "toolchain, or compiler fingerprints");
        return false;
    }
    auto settings = request.settings;
    settings.include_directories.clear();
    settings.defines.clear();
    settings.compile_options.clear();
    plugin_detail::add_default_compiler_settings(settings);
    const auto compiler = settings.compiler.empty()
        ? default_compiler()
        : settings.compiler;
    const auto toolchain = infer_toolchain(compiler);
    if (first.toolchain != to_string(toolchain)
        || !validate_options(settings, toolchain, diagnostics)) {
        if (!diagnostics.has_error()) {
            report(
                diagnostics, kLinkCode,
                "selected linker toolchain differs from the compiled objects");
        }
        return false;
    }
    auto fingerprint = compiler_fingerprint(settings, base, diagnostics);
    if (!fingerprint || *fingerprint != first.compiler_fingerprint) {
        if (!diagnostics.has_error()) {
            report(
                diagnostics, kLinkCode,
                "selected linker compiler identity differs from the compiled objects");
        }
        return false;
    }
    const auto resolved = resolve_executable(compiler, base);
    if (resolved.empty()) {
        report(
            diagnostics, kLinkCode,
            "cannot find selected SystemC linker '" + compiler + "'");
        return false;
    }
    const auto support = plugin_export_library();
    const auto accellera_runtime = accellera_runtime_library();
    const auto official_runtime = official_runtime_library();
    const auto scv_runtime = scv_runtime_library();
    std::error_code error;
    const auto entry_point_count = std::ranges::count_if(
        metadata, [](const auto& object) {
            return object.defines_plugin_entry_point;
        });
    const bool macro_exports = std::ranges::any_of(
        metadata, [](const auto& object) {
            return object.contains_macro_export;
        });
    if (entry_point_count > 1 || (entry_point_count != 0 && macro_exports)) {
        report(
            diagnostics, kLinkCode,
            "macro exports and a handwritten fsim_plugin_init_v1 are mutually "
            "exclusive, and at most one handwritten entry point is allowed");
        return false;
    }
    const bool needs_support = entry_point_count == 0;
    if (needs_support
        && (support.empty() || !std::filesystem::is_regular_file(support, error))) {
        report(
            diagnostics, kLinkCode,
            "cannot locate the fsim SystemC plug-in export library", support);
        return false;
    }
    if (accellera_runtime.empty()
        || !std::filesystem::is_regular_file(accellera_runtime, error)) {
        report(
            diagnostics, kLinkCode,
            "cannot locate the one shared Accellera SystemC runtime bridge",
            accellera_runtime);
        return false;
    }
    if (official_runtime.empty()
        || !std::filesystem::is_regular_file(official_runtime, error)) {
        report(
            diagnostics, kLinkCode,
            "cannot locate the governed official Accellera SystemC runtime",
            official_runtime);
        return false;
    }
    if (scv_runtime.empty()
        || !std::filesystem::is_regular_file(scv_runtime, error)) {
        report(
            diagnostics, kLinkCode,
            "cannot locate the governed official SCV runtime", scv_runtime);
        return false;
    }
    const auto scratch = scratch_root(
        request.scratch_directory, base, diagnostics);
    if (scratch.empty()) {
        return false;
    }
    ScratchCleanup cleanup { scratch };
    const auto output = scratch / plugin_filename();
    std::vector<std::string> argv;
    argv.push_back(path_argument(resolved));
    argv.insert(
        argv.end(), settings.compile_options.begin(), settings.compile_options.end());
    if (toolchain == HostToolchain::msvc) {
        argv.emplace_back("/nologo");
        argv.emplace_back(msvc_runtime_option());
        argv.emplace_back("/LD");
        for (const auto& payload : payloads) {
            argv.push_back(path_argument(payload));
        }
        if (!support.empty()) {
            argv.push_back(path_argument(support));
        }
        argv.push_back(path_argument(accellera_runtime));
        argv.push_back(path_argument(official_runtime));
        argv.push_back(path_argument(scv_runtime));
        argv.push_back("/Fe" + path_argument(output));
        argv.emplace_back("/link");
        argv.emplace_back("/INCREMENTAL:NO");
        argv.emplace_back("/MACHINE:X64");
        argv.insert(
            argv.end(), settings.link_options.begin(), settings.link_options.end());
        for (const auto& library : settings.libraries) {
            argv.push_back(
                library.starts_with('/') || library.starts_with('-')
                        || is_path_like_library(library)
                    ? library
                    : library + ".lib");
        }
    } else {
        argv.emplace_back("-shared");
        for (const auto& payload : payloads) {
            argv.push_back(path_argument(payload));
        }
        // Macro exports reference the registry in the same archive member as
        // the generic entry point, so ordinary archive extraction retains it.
        if (!support.empty()) {
            argv.push_back(path_argument(support));
        }
        argv.push_back(path_argument(accellera_runtime));
        argv.push_back(path_argument(official_runtime));
        argv.push_back(path_argument(scv_runtime));
        argv.emplace_back("-o");
        argv.push_back(path_argument(output));
        argv.insert(
            argv.end(), settings.link_options.begin(), settings.link_options.end());
        for (const auto& library : settings.libraries) {
            argv.push_back(
                library.starts_with('-') || is_path_like_library(library)
                    ? library
                    : "-l" + library);
        }
    }
    const CompilerCommand command { std::move(argv), base, toolchain };
    if (!run_command(command, diagnostics, "SystemC plug-in link", kLinkCode)
        || !std::filesystem::is_regular_file(output, error)
        || std::filesystem::file_size(output, error) == 0) {
        if (!diagnostics.has_error()) {
            report(
                diagnostics, kLinkCode,
                "SystemC linker did not produce a shared library", output);
        }
        return false;
    }
    std::string load_error;
    auto registry = HierarchyRegistry::load(output, load_error);
    if (!registry || registry->factory_count() == 0) {
        report(
            diagnostics, kLinkCode,
            registry
                ? "linked SystemC plug-in registered no module factories"
                : "linked SystemC plug-in failed ABI validation: " + load_error,
            output);
        return false;
    }
    IncrementalPluginMetadata plugin;
    plugin.runtime_abi = runtime_abi_version;
    plugin.systemc_abi = FSIM_SYSTEMC_ABI_VERSION;
    plugin.scv_compatibility = fsim_scv_compatibility_identity();
    plugin.producer = std::string { "fsim " } + std::string { version };
    plugin.logical_library = request.logical_library;
    plugin.toolchain = first.toolchain;
    plugin.target = first.target;
    plugin.compiler_fingerprint = first.compiler_fingerprint;
    for (const auto& object : metadata) {
        plugin.object_digests.push_back(object.compilation_digest);
    }
    plugin.link_options = settings.link_options;
    plugin.libraries = settings.libraries;
    for (const auto& name : registry->factory_names()) {
        auto factory = factory_record(*registry, name, diagnostics);
        if (!factory) {
            return false;
        }
        plugin.factories.push_back(std::move(*factory));
    }
    registry.reset();
    auto payload = read_binary(
        output, diagnostics, "linked SystemC plug-in", kLinkCode);
    if (!payload) {
        return false;
    }
    plugin.library = std::filesystem::path { "native" } / plugin_filename();
    plugin.library_checksum = support::Sha256::hex(
        support::Sha256::digest(*payload));
    plugin.input_digest = compute_incremental_plugin_input_digest(plugin);
    plugin.link_digest = compute_incremental_plugin_digest(plugin);
    return publish_incremental_plugin(
        absolute_from(request.output, base), plugin, *payload, diagnostics);
}

std::shared_ptr<HierarchyRegistry> load_incremental_plugin(
    const std::filesystem::path& directory,
    diagnostic::Engine& diagnostics,
    const std::string_view expected_compiler_fingerprint)
{
    auto metadata = load_incremental_plugin_metadata(directory, diagnostics);
    if (!metadata) {
        return { };
    }
    std::string current_fingerprint;
    if (expected_compiler_fingerprint.empty()) {
        auto fingerprint = compiler_fingerprint(
            project::SystemCSection { }, directory, diagnostics);
        if (!fingerprint) {
            return { };
        }
        current_fingerprint = std::move(*fingerprint);
    } else {
        current_fingerprint = expected_compiler_fingerprint;
    }
    if (metadata->compiler_fingerprint != current_fingerprint) {
        report(
            diagnostics, kLinkCode,
            "SystemC plug-in producer identity is stale or incompatible with the "
            "current upstream source, compiler, standard library, or bridge",
            directory);
        return { };
    }
    std::string error;
    auto registry = HierarchyRegistry::load(
        directory / metadata->library, error, true);
    if (!registry) {
        report(
            diagnostics, kLinkCode,
            "SystemC plug-in failed runtime ABI validation: " + error,
            directory / metadata->library);
        return { };
    }
    if (!matching_inventory(*registry, metadata->factories, diagnostics)) {
        return { };
    }
    return std::shared_ptr<HierarchyRegistry> { std::move(registry) };
}

IncrementalPhaseResult compile_incremental_object_cached(
    IncrementalCompileRequest request,
    const std::filesystem::path& cache_directory,
    diagnostic::Engine& diagnostics)
{
    IncrementalPhaseResult result;
    auto digest = planned_object_input_digest(request, diagnostics);
    if (!digest) {
        return result;
    }
    result.input_digest = *digest;
    const auto base = working_directory(request.working_directory, diagnostics);
    const auto root = absolute_from(cache_directory, base);
    result.artifact = cache_artifact_path(
        root, "objects", *digest, ".fsimscobj");
    std::error_code error;
    if (std::filesystem::exists(result.artifact, error)) {
        auto metadata = load_incremental_object_metadata(
            result.artifact, diagnostics);
        if (metadata && metadata->input_digest == *digest) {
            result.success = true;
            result.cache_hit = true;
        }
        return result;
    }
    auto lock = lock_cache_key(root, *digest, diagnostics);
    if (!lock) {
        return result;
    }
    error.clear();
    if (std::filesystem::exists(result.artifact, error)) {
        auto metadata = load_incremental_object_metadata(
            result.artifact, diagnostics);
        if (metadata && metadata->input_digest == *digest) {
            result.success = true;
            result.cache_hit = true;
        }
        return result;
    }
    request.output = result.artifact;
    result.success = compile_incremental_object(request, diagnostics);
    return result;
}

IncrementalPhaseResult link_incremental_plugin_cached(
    IncrementalLinkRequest request,
    const std::filesystem::path& cache_directory,
    diagnostic::Engine& diagnostics)
{
    IncrementalPhaseResult result;
    auto digest = planned_plugin_input_digest(request, diagnostics);
    if (!digest) {
        return result;
    }
    result.input_digest = *digest;
    const auto base = working_directory(request.working_directory, diagnostics);
    const auto root = absolute_from(cache_directory, base);
    result.artifact = cache_artifact_path(
        root, "plugins", *digest, ".fsimscplugin");
    std::error_code error;
    if (std::filesystem::exists(result.artifact, error)) {
        auto metadata = load_incremental_plugin_metadata(
            result.artifact, diagnostics);
        if (metadata && metadata->input_digest == *digest) {
            result.success = true;
            result.cache_hit = true;
        }
        return result;
    }
    auto lock = lock_cache_key(root, *digest, diagnostics);
    if (!lock) {
        return result;
    }
    error.clear();
    if (std::filesystem::exists(result.artifact, error)) {
        auto metadata = load_incremental_plugin_metadata(
            result.artifact, diagnostics);
        if (metadata && metadata->input_digest == *digest) {
            result.success = true;
            result.cache_hit = true;
        }
        return result;
    }
    request.output = result.artifact;
    result.success = link_incremental_plugin(request, diagnostics);
    return result;
}

} // namespace fsim::systemc
