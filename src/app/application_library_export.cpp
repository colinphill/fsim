// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"

#include "fsim/compiler/object_cache.hpp"
#include "fsim/library/artifact.hpp"
#include "fsim/library/portable_unit.hpp"
#include "fsim/support/path.hpp"
#include "fsim/support/sha256.hpp"
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

    std::string unit_language(const frontend::DesignUnit& unit)
    {
        if (unit.language == frontend::Language::Vhdl2008) {
            return "vhdl";
        }
        return unit.language == frontend::Language::Verilog2005
            ? "verilog"
            : "systemverilog";
    }

    std::string unit_kind(const frontend::UnitKind kind)
    {
        switch (kind) {
        case frontend::UnitKind::VhdlEntity:
            return "entity";
        case frontend::UnitKind::VhdlArchitecture:
            return "architecture";
        case frontend::UnitKind::VhdlConfiguration:
            return "configuration";
        case frontend::UnitKind::VhdlPackage:
            return "package";
        case frontend::UnitKind::VhdlContext:
            return "context";
        case frontend::UnitKind::VhdlPslVerificationUnit:
            return "psl-verification-unit";
        case frontend::UnitKind::SystemVerilogPackage:
            return "package";
        case frontend::UnitKind::SystemVerilogInterface:
            return "interface";
        case frontend::UnitKind::VerilogModule:
            return "module";
        case frontend::UnitKind::SystemVerilogProgram:
            return "program";
        case frontend::UnitKind::SystemVerilogConfiguration:
            return "configuration";
        case frontend::UnitKind::SystemVerilogBind:
            return "bind";
        }
        return "unit";
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
                compiled.host_fingerprint, { }, application_detail::target_name(), { },
                "compiler-default", { }, { }, { } });
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
        const auto optimization = config.build.optimization == project::Optimization::o0
            ? compiler::JitOptimizationLevel::o0
            : compiler::JitOptimizationLevel::o2;
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
                runtime_abi_version, 0, { }, host.llvm_version, host.target,
                host.data_layout, host.cpu, feature_identity(host.features),
                optimization == compiler::JitOptimizationLevel::o0 ? "O0" : "O2",
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
                (void)simulation.compiled_process_count();
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
    auto checked = check_project(config, diagnostics);
    if (!checked) {
        return false;
    }
    std::vector<const frontend::DesignUnit*> units;
    for (const auto& unit : checked->parsed.units) {
        const auto library_name = unit.library.empty()
            ? std::string_view { "work" }
            : std::string_view { unit.library };
        if (library_name == logical_library) {
            units.push_back(&unit);
        }
    }
    std::vector<const frontend::VerilogUdpDeclaration*> udp_declarations;
    for (const auto& declaration : checked->parsed.udp_declarations) {
        const auto library_name = declaration.library.empty()
            ? std::string_view { "work" }
            : std::string_view { declaration.library };
        if (library_name == logical_library) {
            udp_declarations.push_back(&declaration);
        }
    }
    std::map<std::string, library::PortableSystemVerilogClassUnit> class_units;
    for (const auto& declaration : checked->parsed.systemverilog_classes) {
        const auto library_name = declaration.library.empty()
            ? std::string_view { "work" }
            : std::string_view { declaration.library };
        if (library_name != logical_library)
            continue;
        auto& unit = class_units[declaration.compilation_unit_identity];
        unit.library = std::string { library_name };
        unit.compilation_unit_identity = declaration.compilation_unit_identity;
        unit.uvm_release = std::string { project::to_string(
            checked->systemverilog_uvm_provenance.release) };
        unit.declarations.push_back(declaration);
    }
    // Class resolution has already transactionally linked every valid
    // out-of-block definition into its owning declaration.  Persisting the raw
    // definitions as well would ask library loading to link them a second time.
    const bool has_systemc = std::ranges::any_of(
        config.source_sets,
        [&](const auto& source_set) {
            return source_set.library == logical_library
                && source_set.language == project::Language::systemc;
        });
    if (units.empty() && udp_declarations.empty() && class_units.empty()
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

    for (std::size_t index = 0; index < units.size(); ++index) {
        auto unit = *units[index];
        // Class resolution has copied every valid out-of-block method body into
        // its package-owned class declaration.  Do not archive the raw definition
        // list too, because library loading performs semantic resolution again.
        unit.systemverilog_class_method_definitions.clear();
        for (auto& dependency : unit.source_dependencies) {
            const auto normalized = std::filesystem::path(dependency).lexically_normal();
            const auto mapping = std::ranges::find_if(
                source_mappings,
                [&](const auto& item) {
                    if (std::filesystem::path(item.producer_name).lexically_normal()
                        == normalized) {
                        return true;
                    }
                    std::error_code error;
                    return std::filesystem::equivalent(
                               support::path_from_utf8(item.producer_name),
                               support::path_from_utf8(dependency), error)
                        && !error;
                });
            if (mapping != source_mappings.end()) {
                dependency = mapping->logical_name;
            }
        }
        if (!library::relocate_unit_sources(unit, source_mappings, diagnostics)) {
            return false;
        }
        auto bytes = library::serialize_portable_unit(unit, diagnostics);
        if (!bytes.has_value()) {
            return false;
        }
        const auto artifact = indexed_path("units", index, ".fsimir");
        const auto checksum = support::Sha256::hex(support::Sha256::digest(*bytes));
        metadata.units.push_back(
            { unit_language(unit), unit_kind(unit.kind), unit.name,
                unit.primary_name,
                unit.kind == frontend::UnitKind::VhdlArchitecture
                    ? unit.name
                    : std::string { },
                artifact, checksum,
                std::string { frontend::revision_string(unit.standard_revision) },
                unit.language == frontend::Language::Vhdl2008
                    ? unit.vhdl_compatibility_profile
                    : unit.verilog_compatibility_profile });
        payloads.push_back({ artifact, std::move(*bytes) });
    }
    for (std::size_t index = 0;
        index < udp_declarations.size(); ++index) {
        auto declaration = *udp_declarations[index];
        if (!library::relocate_udp_sources(
                declaration, source_mappings, diagnostics)) {
            return false;
        }
        auto bytes = library::serialize_portable_udp(declaration, diagnostics);
        if (!bytes.has_value()) {
            return false;
        }
        const auto artifact = indexed_path(
            "units", units.size() + index, ".fsimudp");
        const auto checksum = support::Sha256::hex(
            support::Sha256::digest(*bytes));
        metadata.units.push_back({ declaration.language == frontend::Language::Verilog2005
                ? "verilog"
                : "systemverilog",
            "primitive", declaration.name, { }, { }, artifact, checksum,
            std::string {
                frontend::revision_string(declaration.standard_revision) },
            declaration.verilog_compatibility_profile });
        payloads.push_back({ artifact, std::move(*bytes) });
    }
    std::size_t class_index { };
    for (auto& [identity, class_unit] : class_units) {
        if (identity.empty()
            || !library::relocate_class_unit_sources(
                class_unit, source_mappings, diagnostics)) {
            if (identity.empty()) {
                diagnostics.error(
                    "FSIM-LIB-0006",
                    "class declaration has no compilation-unit identity");
            }
            return false;
        }
        auto bytes = library::serialize_portable_class_unit(
            class_unit, diagnostics);
        if (!bytes)
            return false;
        const auto artifact = indexed_path(
            "units", units.size() + udp_declarations.size() + class_index++,
            ".fsimclass");
        const auto checksum = support::Sha256::hex(
            support::Sha256::digest(*bytes));
        metadata.units.push_back({ "systemverilog", "class-unit", identity, { }, { }, artifact, checksum,
            std::string { frontend::revision_string(
                class_unit.declarations.front().standard_revision) },
            class_unit.declarations.front().verilog_compatibility_profile });
        payloads.push_back({ artifact, std::move(*bytes) });
    }
    if (!append_systemc_native_artifact(
            config, logical_library, metadata, payloads, diagnostics)) {
        return false;
    }
    if (!units.empty() || !udp_declarations.empty()) {
        append_canonical_llvm_native_artifacts(
            config, logical_library, destination, metadata, payloads);
    }
    return library::publish(destination, metadata, payloads, diagnostics);
}

} // namespace fsim::app
