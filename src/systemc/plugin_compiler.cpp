// SPDX-License-Identifier: Apache-2.0
#include "plugin_compiler_internal.hpp"

#include <mutex>

namespace fsim::systemc {
using namespace plugin_detail;

namespace {

#if defined(_WIN32)
// Concurrent cl.exe dependency scans and cache publication have exhibited
// intermittent process failures on hosted Windows. Serialize the complete
// plan/build/verify transaction in-process; the per-key directory lock still
// provides cross-process publication safety.
std::mutex windows_compile_mutex;
#endif

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

std::optional<std::string> plugin_host_fingerprint(
    const project::SystemCSection& settings,
    const std::filesystem::path& requested_working_directory,
    diagnostic::Engine& diagnostics) {
    auto effective_settings = settings;
#if defined(FSIM_SYSTEMC_HEADER_PATH)
    auto header_directory = std::filesystem::path{FSIM_SYSTEMC_HEADER_PATH};
#if defined(FSIM_SYSTEMC_INSTALLED_HEADER_PATH)
    std::error_code header_error;
    if (!std::filesystem::is_directory(header_directory, header_error)) {
        header_directory = FSIM_SYSTEMC_INSTALLED_HEADER_PATH;
    }
#endif
    effective_settings.include_directories.insert(
        effective_settings.include_directories.begin(), header_directory);
#endif
#if defined(FSIM_SYSTEMC_SUPPORT_LIBRARY_PATH)
    auto support_library =
        std::filesystem::path{FSIM_SYSTEMC_SUPPORT_LIBRARY_PATH};
#if defined(FSIM_SYSTEMC_INSTALLED_SUPPORT_LIBRARY_PATH)
    std::error_code support_error;
    if (!std::filesystem::is_regular_file(support_library, support_error)) {
        support_library = FSIM_SYSTEMC_INSTALLED_SUPPORT_LIBRARY_PATH;
    }
#endif
    effective_settings.libraries.insert(
        effective_settings.libraries.begin(), support_library.string());
#endif
    PluginCompileRequest request;
    request.working_directory = requested_working_directory;
    std::error_code error;
    const auto working_directory = effective_working_directory(request, error);
    if (error || working_directory.empty()) {
        report_error(
            diagnostics, "FSIM-SC-C001",
            "cannot resolve SystemC compiler working directory: "
                + error.message(),
            requested_working_directory);
        return std::nullopt;
    }
    const auto compiler_name = effective_settings.compiler.empty()
        ? default_compiler() : effective_settings.compiler;
    const auto toolchain = infer_toolchain(compiler_name);
    if (!validate_options(effective_settings, toolchain, diagnostics)) {
        return std::nullopt;
    }
    const auto resolved_compiler =
        resolve_executable(compiler_name, working_directory);
    compiler::CacheKeyBuilder builder;
    builder.add("kind", "fsim-systemc-host-v1");
    builder.add("source-language", "c++");
    builder.add("source-standard", "c++20");
    builder.add("runtime-abi", std::to_string(runtime_abi_version));
    builder.add("systemc-abi", std::to_string(FSIM_SYSTEMC_ABI_VERSION));
    builder.add("toolchain", to_string(toolchain));
    builder.add("fiber-backend", FSIM_SYSTEMC_FIBER_IDENTITY);
    if (toolchain == HostToolchain::msvc) {
        builder.add("msvc-runtime", msvc_runtime_option());
    }
#if defined(_WIN32)
    builder.add("host-format", "windows-pe-x86-64");
#else
    builder.add("host-format", "linux-elf-x86-64");
#endif
    if (!add_compiler_identity(builder, compiler_name, resolved_compiler)) {
        report_dependency_cache_disabled(
            diagnostics,
            "the selected compiler executable identity could not be hashed",
            resolved_compiler.empty()
                ? std::filesystem::path{compiler_name} : resolved_compiler);
        return std::nullopt;
    }
    add_compiler_environment_to_key(builder, toolchain);
    add_sequence_to_key(builder, "define", effective_settings.defines);
    add_sequence_to_key(
        builder, "compile-option", effective_settings.compile_options);
    add_sequence_to_key(
        builder, "link-option", effective_settings.link_options);
    add_sequence_to_key(builder, "library", effective_settings.libraries);
    return builder.finish();
}

std::optional<PluginCompilePlan> plan_plugin_compile(
    const PluginCompileRequest& request,
    diagnostic::Engine& diagnostics) {
    auto effective_settings = request.settings;
#if defined(FSIM_SYSTEMC_HEADER_PATH)
    auto header_directory = std::filesystem::path{FSIM_SYSTEMC_HEADER_PATH};
#if defined(FSIM_SYSTEMC_INSTALLED_HEADER_PATH)
    std::error_code header_error;
    if (!std::filesystem::is_directory(header_directory, header_error)) {
        header_directory = FSIM_SYSTEMC_INSTALLED_HEADER_PATH;
    }
#endif
    effective_settings.include_directories.insert(
        effective_settings.include_directories.begin(), header_directory);
#endif
#if defined(FSIM_SYSTEMC_SUPPORT_LIBRARY_PATH)
    auto support_library =
        std::filesystem::path{FSIM_SYSTEMC_SUPPORT_LIBRARY_PATH};
#if defined(FSIM_SYSTEMC_INSTALLED_SUPPORT_LIBRARY_PATH)
    std::error_code support_error;
    if (!std::filesystem::is_regular_file(
            support_library, support_error)) {
        support_library =
            FSIM_SYSTEMC_INSTALLED_SUPPORT_LIBRARY_PATH;
    }
#endif
    effective_settings.libraries.insert(
        effective_settings.libraries.begin(),
        support_library.string());
#endif
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
    includes.reserve(effective_settings.include_directories.size());
    for (const auto& include : effective_settings.include_directories) {
        includes.push_back(make_absolute(include, working_directory));
    }

    const auto compiler_name =
        effective_settings.compiler.empty()
            ? default_compiler()
            : effective_settings.compiler;
    const auto toolchain = infer_toolchain(compiler_name);
    if (!validate_options(effective_settings, toolchain, diagnostics)) {
        return std::nullopt;
    }
    const auto resolved_compiler = resolve_executable(compiler_name, working_directory);
    auto cache_directory =
        request.cache_directory.empty() ? std::filesystem::path{".fsim-cache"}
                                        : request.cache_directory;
    cache_directory = make_absolute(cache_directory, working_directory);

    compiler::CacheKeyBuilder key_builder;
    key_builder.add("kind", "fsim-systemc-shared-library-v1");
    key_builder.add("fingerprint-schema", "systemc-compiler-v2");
    key_builder.add("source-language", "c++");
    key_builder.add("source-standard", "c++20");
    key_builder.add("logical-library", request.logical_library);
    key_builder.add("runtime-abi", std::to_string(runtime_abi_version));
    key_builder.add("systemc-abi", std::to_string(FSIM_SYSTEMC_ABI_VERSION));
    key_builder.add("toolchain", to_string(toolchain));
    key_builder.add("fiber-backend", FSIM_SYSTEMC_FIBER_IDENTITY);
    if (toolchain == HostToolchain::msvc) {
        key_builder.add("msvc-runtime", msvc_runtime_option());
    }
#if defined(_WIN32)
    key_builder.add("host-format", "windows-pe-x86-64");
#else
    key_builder.add("host-format", "linux-elf-x86-64");
#endif
    const bool compiler_identity_complete =
        add_compiler_identity(
            key_builder, compiler_name, resolved_compiler);
    add_compiler_environment_to_key(key_builder, toolchain);
    if (!compiler_identity_complete) {
        report_dependency_cache_disabled(
            diagnostics,
            "the selected compiler executable identity could not be hashed",
            resolved_compiler.empty()
                ? std::filesystem::path{compiler_name}
                : resolved_compiler);
    }
    add_paths_to_key(key_builder, "include", includes);
    add_sequence_to_key(key_builder, "define", effective_settings.defines);
    add_sequence_to_key(
        key_builder, "compile-option", effective_settings.compile_options);
    add_sequence_to_key(
        key_builder, "link-option", effective_settings.link_options);
    add_sequence_to_key(key_builder, "library", effective_settings.libraries);

    compiler::CacheKeyBuilder host_builder;
    host_builder.add("kind", "fsim-systemc-host-v1");
    host_builder.add("source-language", "c++");
    host_builder.add("source-standard", "c++20");
    host_builder.add("runtime-abi", std::to_string(runtime_abi_version));
    host_builder.add(
        "systemc-abi", std::to_string(FSIM_SYSTEMC_ABI_VERSION));
    host_builder.add("toolchain", to_string(toolchain));
    host_builder.add("fiber-backend", FSIM_SYSTEMC_FIBER_IDENTITY);
    if (toolchain == HostToolchain::msvc) {
        host_builder.add("msvc-runtime", msvc_runtime_option());
    }
#if defined(_WIN32)
    host_builder.add("host-format", "windows-pe-x86-64");
#else
    host_builder.add("host-format", "linux-elf-x86-64");
#endif
    (void)add_compiler_identity(
        host_builder, compiler_name, resolved_compiler);
    add_compiler_environment_to_key(host_builder, toolchain);
    add_sequence_to_key(
        host_builder, "define", effective_settings.defines);
    add_sequence_to_key(
        host_builder, "compile-option", effective_settings.compile_options);
    add_sequence_to_key(
        host_builder, "link-option", effective_settings.link_options);
    add_sequence_to_key(
        host_builder, "library", effective_settings.libraries);
    const auto host_fingerprint = host_builder.finish();
    // Raw compiler/linker options can reference response files, plug-ins,
    // profiles, sysroots, forced includes, or other inputs with
    // toolchain-specific spelling. Keep supporting the literal argv contract,
    // but never claim a persistent hit unless all inputs came through the
    // structured manifest fields.
    bool cacheable = compiler_identity_complete
        && effective_settings.compile_options.empty()
        && effective_settings.link_options.empty();
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
            effective_settings,
            working_directory,
            cache_directory,
            cacheable,
            diagnostics)
        || !add_linked_library_contents_to_key(
            key_builder,
            effective_settings.libraries,
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
    plan.host_fingerprint = host_fingerprint;
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
        effective_settings,
        plan.build_path,
        working_directory,
        plan.intermediate_paths);
    return plan;
}

PluginCompileResult compile_plugin(
    const PluginCompileRequest& request,
    diagnostic::Engine& diagnostics) {
#if defined(_WIN32)
    const std::lock_guard compile_guard{windows_compile_mutex};
#endif
    PluginCompileResult result;
    const auto plan = plan_plugin_compile(request, diagnostics);
    if (!plan) {
        return result;
    }
    result.cache_key = plan->cache_key;
    result.host_fingerprint = plan->host_fingerprint;
    result.library_path = plan->library_path;

    std::error_code error;
    if (plan->cacheable && valid_cached_artifact(*plan, error)) {
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

    if (plan->cacheable && valid_cached_artifact(*plan, error)) {
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
    if (!prepare_artifact_build(*plan, diagnostics)) {
        return result;
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
        if (!process.execution_error.empty()) {
            report_error(
                diagnostics,
                "FSIM-SC-C007",
                "SystemC compiler process failed after launch: "
                    + process.execution_error);
            std::filesystem::remove(plan->build_path, error);
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
