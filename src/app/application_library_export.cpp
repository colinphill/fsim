// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"

#include "fsim/app/design_artifact.hpp"
#include "fsim/compiler/object_cache.hpp"
#include "fsim/library/artifact.hpp"
#include "fsim/library/source_mapping.hpp"
#include "fsim/semantic/compiled_design_linker.hpp"
#include "fsim/support/path.hpp"
#include "fsim/support/sha256.hpp"
#include "fsim/systemc/scv.hpp"
#include "fsim/systemc_abi.h"
#include "fsim/version.hpp"
#if defined(FSIM_HAS_LLVM)
#include "fsim/compiler/llvm_jit.hpp"
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>

namespace fsim::app {
namespace {

    std::string indexed_path(
        const std::string_view directory,
        const std::size_t index,
        const std::string_view suffix)
    {
        std::ostringstream output;
        output << directory << '/' << std::setw(8) << std::setfill('0') << index
               << suffix;
        return output.str();
    }

    std::optional<std::string> read_checked_source(
        const std::filesystem::path& path,
        const std::string_view expected_digest,
        diagnostic::Engine& diagnostics)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            diagnostics.error(
                "FSIM-LIB-0007",
                "cannot reopen checked source for library export: "
                    + support::path_to_utf8(path));
            return std::nullopt;
        }
        std::ostringstream contents;
        contents << input.rdbuf();
        if (!input.good() && !input.eof()) {
            diagnostics.error(
                "FSIM-LIB-0007",
                "cannot reread checked source for library export: "
                    + support::path_to_utf8(path));
            return std::nullopt;
        }
        auto bytes = contents.str();
        const auto digest = support::Sha256::hex(support::Sha256::digest(bytes));
        if (digest != expected_digest) {
            diagnostics.error(
                "FSIM-LIB-0007",
                "source changed while exporting logical library: "
                    + support::path_to_utf8(path));
            return std::nullopt;
        }
        return bytes;
    }

    std::optional<std::string> read_binary_file(
        const std::filesystem::path& path,
        diagnostic::Engine& diagnostics)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            diagnostics.error(
                "FSIM-LIB-0007",
                "cannot open native library artifact for export: "
                    + support::path_to_utf8(path));
            return std::nullopt;
        }
        std::ostringstream contents;
        contents << input.rdbuf();
        if (!input.good() && !input.eof()) {
            diagnostics.error(
                "FSIM-LIB-0007",
                "cannot read native library artifact for export: "
                    + support::path_to_utf8(path));
            return std::nullopt;
        }
        return contents.str();
    }

    bool append_systemc_native_artifact(
        const project::Config& config,
        const std::string_view logical_library,
        library::Metadata& metadata,
        std::vector<library::PortablePayload>& payloads,
        diagnostic::Engine& diagnostics)
    {
        for (const auto& entry : application_detail::systemc_requests(config)) {
            if (entry.library != logical_library) {
                continue;
            }
            auto compiled = systemc::compile_plugin(entry.request, diagnostics);
            if (!compiled.success || compiled.host_fingerprint.empty()) {
                return false;
            }
            auto bytes = read_binary_file(compiled.library_path, diagnostics);
            if (!bytes.has_value()) {
                return false;
            }
            const auto artifact = std::filesystem::path { "native" } / "systemc"
                / compiled.library_path.filename();
            metadata.native_artifacts.push_back({ "systemc_plugin", artifact,
                support::Sha256::hex(support::Sha256::digest(*bytes)),
                runtime_abi_version, FSIM_SYSTEMC_ABI_VERSION,
                fsim_scv_compatibility_identity(), compiled.host_fingerprint,
                { }, application_detail::target_name(), { }, "compiler-default",
                { }, { }, { } });
            payloads.push_back({ artifact, std::move(*bytes) });
            break;
        }
        return true;
    }

    bool validate_portable_systemc_settings(
        const project::Config& config,
        const std::string_view logical_library,
        diagnostic::Engine& diagnostics)
    {
        const bool global_inputs = !config.systemc.include_directories.empty()
            || !config.systemc.defines.empty()
            || !config.systemc.compile_options.empty()
            || !config.systemc.link_options.empty()
            || !config.systemc.libraries.empty();
        const bool source_inputs = std::ranges::any_of(
            config.source_sets,
            [&](const auto& source_set) {
                return source_set.library == logical_library
                    && source_set.language == project::Language::systemc
                    && (!source_set.include_directories.empty()
                        || !source_set.defines.empty());
            });
        if (!global_inputs && !source_inputs) {
            return true;
        }
        diagnostics.error(
            "FSIM-LIB-0007",
            "cannot export SystemC logical library '"
                + std::string { logical_library }
                + "' with producer-only include directories, definitions, "
                  "compiler/linker options, or external libraries; format 1 "
                  "portable fallback requires self-contained source");
        return false;
    }

#if defined(FSIM_HAS_LLVM)
    std::string feature_identity(const std::vector<std::string>& features)
    {
        std::string result;
        for (const auto& feature : features) {
            if (!result.empty()) {
                result.push_back('\n');
            }
            result += feature;
        }
        return result;
    }

    bool append_llvm_native_artifacts(
        const project::Config& config,
        library::Metadata& metadata,
        std::vector<library::PortablePayload>& payloads,
        diagnostic::Engine& diagnostics)
    {
        auto cache_directory = config.build.cache_path;
        if (!cache_directory.is_absolute()) {
            cache_directory = config.base_directory / cache_directory;
        }
        const auto object_root = cache_directory / "llvm-native" / "llvm" / "objects";
        std::error_code exists_error;
        const bool exists = std::filesystem::exists(object_root, exists_error);
        if (exists_error) {
            diagnostics.error(
                "FSIM-LIB-0007",
                "cannot inspect native object cache for export: "
                    + exists_error.message());
            return false;
        }
        if (!exists) {
            return true;
        }
        std::vector<std::string> keys;
        std::error_code iteration_error;
        for (std::filesystem::recursive_directory_iterator iterator(
                 object_root, iteration_error),
            end;
            !iteration_error && iterator != end;
            iterator.increment(iteration_error)) {
            if (!iterator->is_regular_file()
                || iterator->path().extension() != ".fobj") {
                continue;
            }
            const auto key = iterator->path().stem().string();
            if (compiler::valid_cache_key(key)) {
                keys.push_back(key);
            }
        }
        if (iteration_error) {
            diagnostics.error(
                "FSIM-LIB-0007",
                "cannot enumerate native object cache for export: "
                    + iteration_error.message());
            return false;
        }
        std::ranges::sort(keys);
        keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
        if (keys.empty()) {
            return true;
        }
        const auto optimization = application_detail::jit_optimization(
            config.build.optimization);
        const auto host = compiler::LlvmJit::native_host_identity(optimization);
        compiler::ObjectCache cache { object_root };
        for (const auto& key : keys) {
            std::error_code load_error;
            auto bytes = cache.load(key, load_error);
            if (!bytes.has_value()) {
                // The producer cache is best-effort; a stale entry cannot invalidate an
                // otherwise complete portable export.
                continue;
            }
            const std::string raw {
                reinterpret_cast<const char*>(bytes->data()), bytes->size()
            };
            const auto artifact = std::filesystem::path { "native" } / "llvm"
                / (key + ".o");
            metadata.native_artifacts.push_back({ "llvm_object", artifact,
                support::Sha256::hex(support::Sha256::digest(raw)),
                runtime_abi_version, 0, { }, host.fingerprint,
                host.llvm_version, host.target,
                host.data_layout, host.cpu, feature_identity(host.features),
                std::string { compiler::to_string(optimization) },
                key });
            payloads.push_back({ artifact, raw });
        }
        return true;
    }

    std::filesystem::path native_seed_path(
        const std::filesystem::path& destination,
        const std::string_view suffix)
    {
        static std::atomic_uint64_t sequence { };
        const auto nonce = static_cast<std::uint64_t>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count());
        const auto parent = destination.parent_path().empty()
            ? std::filesystem::path { "." }
            : destination.parent_path();
        return parent
            / ("." + destination.filename().string() + ".native-seed-"
                + std::to_string(nonce) + "-"
                + std::to_string(sequence.fetch_add(1, std::memory_order_relaxed))
                + std::string { suffix });
    }

    void remove_seed_tree(const std::filesystem::path& path) noexcept
    {
        std::error_code error;
        if (!std::filesystem::exists(path, error)) {
            return;
        }
        for (std::filesystem::recursive_directory_iterator iterator(
                 path, error),
            end;
            !error && iterator != end;
            iterator.increment(error)) {
            std::filesystem::permissions(
                iterator->path(), std::filesystem::perms::owner_all,
                std::filesystem::perm_options::add, error);
            error.clear();
        }
        std::filesystem::permissions(
            path, std::filesystem::perms::owner_all,
            std::filesystem::perm_options::add, error);
        error.clear();
        std::filesystem::remove_all(path, error);
    }
#endif

    void append_canonical_llvm_native_artifacts(
        const project::Config& config,
        const std::string_view logical_library,
        const std::filesystem::path& destination,
        library::Metadata& metadata,
        std::vector<library::PortablePayload>& payloads)
    {
#if defined(FSIM_HAS_LLVM)
        const auto seed_artifact = native_seed_path(destination, ".fsimlib");
        const auto seed_cache = native_seed_path(destination, ".cache");
        diagnostic::Engine seed_diagnostics;
        if (!library::publish(
                seed_artifact, metadata, payloads, seed_diagnostics)) {
            remove_seed_tree(seed_artifact);
            remove_seed_tree(seed_cache);
            return;
        }
        auto seed_config = config;
        std::erase_if(
            seed_config.source_sets,
            [&](const auto& source_set) {
                return source_set.library == logical_library
                    && source_set.language != project::Language::systemc;
            });
        std::erase_if(
            seed_config.library_mappings,
            [&](const auto& mapping) {
                return mapping.library == logical_library;
            });
        seed_config.library_mappings.push_back(
            { std::string { logical_library }, seed_artifact });
        seed_config.build.cache_path = seed_cache;
        try {
            auto built = build_project(seed_config, seed_diagnostics);
            if (built) {
                Simulation simulation(
                    std::move(*built), seed_config.run.max_deltas,
                    SimulationEngine::compiled);
                simulation.await_all_native_compilation();
                (void)append_llvm_native_artifacts(
                    seed_config, metadata, payloads, seed_diagnostics);
            }
        } catch (...) {
            // Native payloads are optional. The portable artifact remains complete
            // when host compilation or materialization is unavailable.
        }
        remove_seed_tree(seed_artifact);
        remove_seed_tree(seed_cache);
#else
        (void)config;
        (void)logical_library;
        (void)destination;
        (void)metadata;
        (void)payloads;
#endif
    }

} // namespace

bool export_library(
    const project::Config& config,
    const std::string_view logical_library,
    const std::filesystem::path& destination,
    diagnostic::Engine& diagnostics)
{
    auto checked = application_detail::check_project_workspace(
        config, diagnostics, true);
    if (!checked) {
        return false;
    }
    const auto has_systemverilog_unit = std::ranges::any_of(
        checked->systemverilog_hir.units(), [&](const auto& unit) {
        const auto library_name = unit.library.empty()
            ? std::string_view { "work" }
            : std::string_view { unit.library };
        return library_name == logical_library
            && unit.kind != semantic::sv::UnitKind::compilation_unit;
    });
    const auto has_vhdl_unit = std::ranges::any_of(
        checked->vhdl_hir.units(), [&](const auto& unit) {
        const auto library_name = unit.library.empty()
            ? std::string_view { "work" }
            : std::string_view { unit.library };
        return library_name == logical_library;
    });
    const bool has_unit = has_systemverilog_unit || has_vhdl_unit;
    const auto has_udp = std::ranges::any_of(
        checked->systemverilog_hir.udps(), [&](const auto& declaration) {
        const auto library_name = declaration.library.empty()
            ? std::string_view { "work" }
            : std::string_view { declaration.library };
        return library_name == logical_library;
    });
    const auto has_class = std::ranges::any_of(
        checked->systemverilog_hir.classes(), [&](const auto& declaration) {
            const auto* owner = application_detail::compiled_class_owner(
                *checked, declaration);
            const auto library_name = owner == nullptr || owner->library.empty()
                ? std::string_view { "work" }
                : std::string_view { owner->library };
            return owner != nullptr && library_name == logical_library;
        });
    const bool has_systemc = std::ranges::any_of(
        config.source_sets,
        [&](const auto& source_set) {
            return source_set.library == logical_library
                && source_set.language == project::Language::systemc;
        });
    if (!has_unit && !has_udp && !has_class
        && !has_systemc) {
        diagnostics.error(
            "FSIM-LIB-0007",
            "project contains no exportable units or sources in logical library '"
                + std::string { logical_library } + "'");
        return false;
    }
    if (has_systemc
        && !validate_portable_systemc_settings(
            config, logical_library, diagnostics)) {
        return false;
    }

    library::Metadata metadata;
    metadata.library = std::string { logical_library };
    metadata.producer = std::string { "fsim " } + std::string { version };
    metadata.runtime_schema = 2;
    if (config.run.trace_file && config.run.trace_enabled) {
        auto control = apply_trace_control(trace_control_request(config.run,
            TraceControlSurface::ProjectCli, TraceControlPhase::Simulate));
        if (!control.ok()) {
            for (const auto& diagnostic : control.diagnostics)
                application_detail::import_diagnostic(diagnostics, diagnostic);
            return false;
        }
        const auto snapshot = make_trace_archive_snapshot(
            *control.application, config.base_directory);
        auto archive = encode_trace_archive(snapshot, TraceArchiveKind::Library);
        if (!archive.ok()) {
            for (const auto& diagnostic : archive.diagnostics)
                application_detail::import_diagnostic(diagnostics, diagnostic);
            return false;
        }
        metadata.trace_archive = trace_archive_hex(archive.archive);
    }
    std::set<std::pair<std::string, std::string>> standards;
    for (const auto& source_set : config.source_sets) {
        if (source_set.library == logical_library
            && source_set.language != project::Language::systemc) {
            standards.emplace(
                std::string { project::to_string(source_set.language) },
                source_set.standard);
        }
    }
    for (const auto& [language, revision] : standards) {
        metadata.standards.push_back({ language, revision });
    }
    metadata.vhdl_package_dependencies = application_detail::vhdl_package_dependencies(*checked);

    std::vector<library::PortablePayload> payloads;
    std::vector<library::SourceNameMapping> source_mappings;
    std::map<std::filesystem::path, std::string> source_languages;
    std::map<std::filesystem::path, std::string> source_standards;
    std::map<std::filesystem::path, std::string> source_compatibility_profiles;
    const auto absolute_source = [&](const std::filesystem::path& path) {
        return (path.is_absolute() ? path : config.base_directory / path)
            .lexically_normal();
    };
    for (const auto& source_set : config.source_sets) {
        if (source_set.library != logical_library) {
            continue;
        }
        for (const auto& path : source_set.files) {
            const auto absolute = absolute_source(path);
            source_languages.emplace(
                absolute, std::string { project::to_string(source_set.language) });
            source_standards.emplace(
                absolute,
                source_set.language == project::Language::systemc
                    ? std::string { }
                    : source_set.standard);
            source_compatibility_profiles.emplace(
                absolute,
                source_set.language == project::Language::vhdl
                    ? std::string { application_detail::vhdl_compatibility_profile() }
                    : project::compatibility_profile(
                          source_set.compatibility_switches));
        }
    }
    std::set<std::filesystem::path> source_paths;
    for (const auto& source : checked->hdl_sources) {
        const auto root = source.path.lexically_normal();
        const auto language = source_languages.find(root);
        if (language == source_languages.end()) {
            continue;
        }
        source_paths.insert(root);
        for (const auto& dependency : source.dependencies) {
            const auto path = dependency.path.lexically_normal();
            source_paths.insert(path);
            source_languages.emplace(path, language->second);
            source_standards.emplace(path, source_standards.at(root));
            source_compatibility_profiles.emplace(
                path, source_compatibility_profiles.at(root));
        }
    }
    for (const auto& source : checked->systemc_sources) {
        const auto root = source.path.lexically_normal();
        if (source_languages.contains(root)) {
            source_paths.insert(root);
        }
    }
    std::size_t source_index = 0;
    for (const auto& path : source_paths) {
        std::string digest;
        for (const auto& source : checked->hdl_sources) {
            if (source.path.lexically_normal() == path) {
                digest = source.content_digest;
            }
            for (const auto& dependency : source.dependencies) {
                if (dependency.path.lexically_normal() == path) {
                    digest = dependency.content_digest;
                }
            }
        }
        for (const auto& source : checked->systemc_sources) {
            if (source.path.lexically_normal() == path) {
                digest = source.content_digest;
            }
        }
        if (digest.empty()) {
            continue;
        }
        auto contents = read_checked_source(path, digest, diagnostics);
        if (!contents.has_value()) {
            return false;
        }
        const auto filename = path.filename().empty()
            ? std::string { "source" }
            : support::path_to_utf8(path.filename());
        const auto artifact = indexed_path(
            "sources", source_index++, "/" + filename);
        metadata.sources.push_back(
            { artifact, artifact, digest, source_languages[path],
                source_standards[path], source_compatibility_profiles[path] });
        source_mappings.push_back({ support::path_to_utf8(path), artifact });
        payloads.push_back({ artifact, std::move(*contents) });
    }

    std::vector<std::string> persisted_libraries {
        std::string { logical_library }
    };
    for (const auto& dependency : metadata.vhdl_package_dependencies) {
        const auto separator = dependency.package.find('.');
        const auto library = dependency.package.substr(0, separator);
        if (!library.empty()
            && std::ranges::find(persisted_libraries, library)
                == persisted_libraries.end()) {
            persisted_libraries.push_back(library);
        }
    }
    auto projected_design = semantic::extract_compiled_libraries(
        std::move(static_cast<semantic::CompiledDesign&>(*checked)),
        persisted_libraries);
    if (!projected_design.ok()) {
        diagnostics.error(
            "FSIM-LIB-0007", projected_design.error);
        return false;
    }
    auto compiled_design = std::move(*projected_design.design);
    auto compiled_mappings = source_mappings;
    std::vector<std::filesystem::path> mapped_compiled_sources;
    mapped_compiled_sources.reserve(compiled_mappings.size());
    for (const auto& mapping : compiled_mappings) {
        mapped_compiled_sources.push_back(
            support::path_from_utf8(mapping.producer_name)
                .lexically_normal());
    }
    std::size_t compiled_source_index { };
    const auto map_compiled_source = [&](const std::string_view name) {
        if (name.empty()) {
            return;
        }
        const auto path = support::path_from_utf8(name).lexically_normal();
        if (!support::path_is_portably_absolute(path)
            || std::ranges::any_of(
                mapped_compiled_sources, [&](const auto& mapped) {
                    return application_detail::same_source_path(
                        mapped, path);
                })) {
            return;
        }
        mapped_compiled_sources.push_back(path);
        const auto filename = path.filename().empty()
            ? std::string { "source" }
            : support::path_to_utf8(path.filename());
        compiled_mappings.push_back({ std::string { name },
            indexed_path("compiled/sources", compiled_source_index++,
                "/" + filename) });
    };
    for (const auto& source : compiled_design.semantics.source_files()) {
        map_compiled_source(source.physical_name);
    }
    for (const auto& source : compiled_design.semantics.source_spans()) {
        map_compiled_source(source.logical_name);
    }
    for (const auto& dependency : compiled_design.dependencies()) {
        map_compiled_source(dependency.logical_name);
    }
    for (const auto& unit : compiled_design.systemverilog_hir.units()) {
        for (const auto& dependency : unit.source_dependencies) {
            map_compiled_source(dependency);
        }
    }
    for (const auto& unit : compiled_design.vhdl_hir.units()) {
        for (const auto& dependency : unit.source_dependencies) {
            map_compiled_source(dependency);
        }
    }
    if (!application_detail::relocate_compiled_design_sources(
            compiled_design, compiled_mappings, diagnostics)) {
        return false;
    }
    auto compiled_units = application_detail::compiled_unit_metadata_entries(
        compiled_design, logical_library);
    for (auto& entry : compiled_units) {
        metadata.units.push_back(std::move(entry));
    }
    for (const auto& declaration
        : compiled_design.systemverilog_hir.udps()) {
        const auto library = declaration.library.empty()
            ? std::string_view { "work" }
            : std::string_view { declaration.library };
        if (library != logical_library) {
            continue;
        }
        metadata.units.push_back({
            declaration.language == semantic::Language::verilog
                ? "verilog"
                : "systemverilog",
            "primitive", declaration.name, { }, { }, { }, { },
            std::string { application_detail::compiled_udp_standard(
                declaration) },
            declaration.compatibility_profile });
    }
    for (const auto& declaration
        : compiled_design.systemverilog_hir.classes()) {
        auto entry = application_detail::compiled_class_metadata_entry(
            compiled_design, declaration, logical_library);
        if (!entry) {
            const auto* owner = application_detail::compiled_class_owner(
                compiled_design, declaration);
            if (owner != nullptr) {
                const auto library_name = owner->library.empty()
                    ? std::string_view { "work" }
                    : std::string_view { owner->library };
                if (library_name != logical_library) {
                    continue;
                }
            }
            diagnostics.error(
                "FSIM-LIB-0007",
                "compiled class declaration has no valid HIR owner: '"
                    + semantic::sv::class_declaration_identity(declaration)
                    + "'");
            return false;
        }
        metadata.units.push_back(std::move(*entry));
    }
    auto compiled_bytes = serialize_compiled_hir_bundle(
        compiled_design, diagnostics);
    if (!compiled_bytes) {
        return false;
    }
    metadata.compiled_hir_artifact = "compiled/design.fsimhir";
    metadata.compiled_hir_checksum = support::Sha256::hex(
        support::Sha256::digest(*compiled_bytes));
    payloads.push_back(
        { metadata.compiled_hir_artifact, std::move(*compiled_bytes) });

    if (!append_systemc_native_artifact(
            config, logical_library, metadata, payloads, diagnostics)) {
        return false;
    }
    if (has_unit || has_udp) {
        append_canonical_llvm_native_artifacts(
            config, logical_library, destination, metadata, payloads);
    }
    return library::publish(destination, metadata, payloads, diagnostics);
}

} // namespace fsim::app
