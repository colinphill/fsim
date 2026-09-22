// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"

#include "../diagnostic/artifact_identity.hpp"
#include "fsim/app/design_artifact.hpp"
#include "fsim/library/artifact.hpp"
#include "fsim/library/source_mapping.hpp"
#include "fsim/semantic/compiled_design_linker.hpp"
#include "fsim/support/path.hpp"
#include "fsim/support/sha256.hpp"
#include "fsim/systemc_abi.h"

#include <map>
#include <set>

namespace fsim::app::application_detail {
namespace {

    std::optional<std::string> read_payload(
        const std::filesystem::path& path,
        const std::string_view expected_checksum,
        diagnostic::Engine& diagnostics,
        const std::optional<std::uintmax_t> maximum_bytes = std::nullopt)
    {
        auto payload = read_binary_payload(path, maximum_bytes);
        if (!payload.bytes) {
            const auto budget = payload.budget_exceeded
                ? " exceeds the compiled-HIR decode byte budget: "
                : ": ";
            diagnostics.error(
                "FSIM-LIB-0008",
                (payload.failure == BinaryPayloadReadFailure::open
                        ? "cannot open mapped library payload"
                        : "cannot read mapped library payload")
                    + std::string { budget } + support::path_to_utf8(path));
            return std::nullopt;
        }
        if (support::Sha256::hex(support::Sha256::digest(*payload.bytes))
            != expected_checksum) {
            diagnostics.error(
                "FSIM-LIB-0008",
                "mapped library payload checksum mismatch: "
                    + support::path_to_utf8(path));
            return std::nullopt;
        }
        return std::move(payload.bytes);
    }

    bool body_has_instances(const frontend::GenerateBody& body);

    bool regions_have_instances(
        const std::vector<frontend::GenerateRegion>& regions)
    {
        return std::ranges::any_of(regions, [](const auto& region) {
            if (body_has_instances(region.then_body)
                || body_has_instances(region.else_body)) {
                return true;
            }
            return std::ranges::any_of(
                region.alternatives,
                [](const auto& alternative) {
                    return body_has_instances(alternative.body);
                });
        });
    }

    bool body_has_instances(const frontend::GenerateBody& body)
    {
        return !body.instances.empty() || regions_have_instances(body.generate_regions);
    }

    bool design_has_hierarchy_queries(const frontend::ParsedDesign& parsed)
    {
        return std::ranges::any_of(parsed.units, [](const auto& unit) {
            return !unit.instances.empty()
                || regions_have_instances(unit.generate_regions);
        });
    }

    std::optional<std::string> qualified_library(const std::string_view target)
    {
        const auto colon = target.find(':');
        if (colon == std::string_view::npos) {
            return std::nullopt;
        }
        auto remainder = target.substr(colon + 1);
        const auto open = remainder.find('(');
        if (open != std::string_view::npos) {
            remainder = remainder.substr(0, open);
        }
        const auto dot = remainder.rfind('.');
        if (dot == std::string_view::npos || dot == 0) {
            return std::nullopt;
        }
        return std::string { remainder.substr(0, dot) };
    }

    bool metadata_identity_matches(
        const library::UnitIndexEntry& entry,
        const frontend::DesignUnit& unit)
    {
        const auto language = unit.language == frontend::Language::Vhdl2008
            ? std::string_view { "vhdl" }
            : unit.language == frontend::Language::Verilog2005
            ? std::string_view { "verilog" }
            : std::string_view { "systemverilog" };
        const auto kind = [&]() -> std::string_view {
            switch (unit.kind) {
            case frontend::UnitKind::VhdlEntity:
                return "entity";
            case frontend::UnitKind::VhdlArchitecture:
                return "architecture";
            case frontend::UnitKind::VhdlConfiguration:
                return "configuration";
            case frontend::UnitKind::VhdlPackage:
            case frontend::UnitKind::SystemVerilogPackage:
                return "package";
            case frontend::UnitKind::VhdlContext:
                return "context";
            case frontend::UnitKind::VhdlPslVerificationUnit:
                return "psl-verification-unit";
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
        }();
        const auto architecture = unit.kind == frontend::UnitKind::VhdlArchitecture
            ? std::string_view { unit.name }
            : std::string_view { };
        return entry.name == unit.name
            && entry.primary_name == unit.primary_name
            && entry.language == language && entry.kind == kind
            && entry.architecture == architecture
            && entry.standard == frontend::revision_string(unit.standard_revision)
            && entry.compatibility_profile
            == (unit.language == frontend::Language::Vhdl2008
                    ? unit.vhdl_compatibility_profile
                    : unit.verilog_compatibility_profile);
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
#endif

    void record_native_acceptance(
        CheckedProject::MappedLibrary& provenance,
        const std::string_view kind,
        const std::string_view fingerprint)
    {
        if (provenance.native_kind == kind
            && provenance.native_fingerprint == fingerprint) {
            return;
        }
        if (provenance.native_accepted) {
            provenance.native_kind += "+" + std::string { kind };
            provenance.native_fingerprint += ";" + std::string { fingerprint };
        } else {
            provenance.native_accepted = true;
            provenance.native_kind = kind;
            provenance.native_fingerprint = fingerprint;
        }
    }

#if defined(FSIM_HAS_LLVM)
    bool admit_llvm_artifact(
        const project::Config& config,
        const std::filesystem::path& directory,
        const library::NativeArtifact& native,
        CheckedProject::MappedLibrary& provenance,
        diagnostic::Engine& diagnostics)
    {
        const auto optimization = jit_optimization(config.build.optimization);
        const auto host = compiler::LlvmJit::native_host_identity(optimization);
        const std::string expected_optimization {
            compiler::to_string(optimization)
        };
        const auto expected_features = feature_identity(host.features);
        if (native.runtime_abi != runtime_abi_version
            || native.compiler_fingerprint != host.fingerprint
            || native.llvm_version != host.llvm_version
            || native.target != host.target
            || native.data_layout != host.data_layout
            || native.cpu != host.cpu
            || native.features != expected_features
            || native.optimization != expected_optimization) {
            diagnostics.error(
                "FSIM-LIB-0008",
                diagnostic::unsupported_artifact_identity(
                    "mapped LLVM native object producer",
                    "runtime ABI " + std::to_string(native.runtime_abi)
                        + ", host fingerprint " + native.compiler_fingerprint
                        + ", LLVM " + native.llvm_version + ", target "
                        + native.target + ", data layout "
                        + native.data_layout + ", CPU " + native.cpu
                        + ", features SHA-256 "
                        + support::Sha256::hex(
                            support::Sha256::digest(native.features))
                        + ", optimization " + native.optimization,
                    "runtime ABI " + std::to_string(runtime_abi_version)
                        + ", host fingerprint " + host.fingerprint
                        + ", LLVM " + host.llvm_version + ", target "
                        + host.target + ", data layout " + host.data_layout
                        + ", CPU " + host.cpu + ", features SHA-256 "
                        + support::Sha256::hex(
                            support::Sha256::digest(expected_features))
                        + ", optimization " + expected_optimization,
                    ".fsimlib native payload"));
            return false;
        }
        auto bytes = read_payload(
            directory / native.artifact, native.checksum, diagnostics);
        if (!bytes.has_value()) {
            return false;
        }
        auto cache_directory = config.build.cache_path;
        if (!cache_directory.is_absolute()) {
            cache_directory = config.base_directory / cache_directory;
        }
        compiler::ObjectCache cache {
            cache_directory / "llvm-native" / "llvm" / "objects"
        };
        const auto payload = std::as_bytes(
            std::span<const char> { bytes->data(), bytes->size() });
        std::error_code error;
        if (!cache.store(native.cache_key, payload, error)) {
            diagnostics.error(
                "FSIM-LIB-0008",
                "cannot admit mapped LLVM object into the consumer cache: "
                    + error.message());
            return false;
        }
        record_native_acceptance(provenance, "llvm_object", host.fingerprint);
        return true;
    }
#endif

    bool admit_systemc_artifact(
        const project::Config& config,
        const std::filesystem::path& directory,
        const library::NativeArtifact& native,
        CheckedProject::MappedLibrary& provenance,
        diagnostic::Engine& diagnostics)
    {
        diagnostic::Engine fingerprint_diagnostics;
        const auto fingerprint = systemc::plugin_host_fingerprint(
            config.systemc, config.base_directory, fingerprint_diagnostics);
        if (native.scv_compatibility != fsim_scv_compatibility_identity()) {
            diagnostics.error(
                "FSIM-LIB-0008",
                diagnostic::unsupported_artifact_identity(
                    "mapped SystemC native plug-in SCV producer",
                    native.scv_compatibility,
                    fsim_scv_compatibility_identity(),
                    ".fsimlib native payload"));
            return false;
        }
        if (!fingerprint.has_value()) {
            for (const auto& item : fingerprint_diagnostics.diagnostics()) {
                diagnostics.report(item);
            }
            if (!diagnostics.has_error()) {
                diagnostics.error(
                    "FSIM-LIB-0008",
                    "cannot establish the current SystemC native producer "
                    "identity");
            }
            return false;
        }
        if (native.runtime_abi != runtime_abi_version
            || native.systemc_abi != FSIM_SYSTEMC_ABI_VERSION
            || native.compiler_fingerprint != *fingerprint
            || native.target != target_name()
            || native.cpu != "compiler-default") {
            diagnostics.error(
                "FSIM-LIB-0008",
                diagnostic::unsupported_artifact_identity(
                    "mapped SystemC native plug-in producer",
                    "runtime ABI " + std::to_string(native.runtime_abi)
                        + ", SystemC ABI "
                        + std::to_string(native.systemc_abi) + ", SCV "
                        + native.scv_compatibility + ", fingerprint "
                        + native.compiler_fingerprint + ", target "
                        + native.target + ", CPU " + native.cpu,
                    "runtime ABI " + std::to_string(runtime_abi_version)
                        + ", SystemC ABI "
                        + std::to_string(FSIM_SYSTEMC_ABI_VERSION) + ", SCV "
                        + std::string { fsim_scv_compatibility_identity() }
                        + ", fingerprint " + *fingerprint + ", target "
                        + target_name() + ", CPU compiler-default",
                    ".fsimlib native payload"));
            return false;
        }
        if (!read_payload(
                directory / native.artifact, native.checksum, diagnostics)) {
            return false;
        }
        provenance.systemc_plugin = directory / native.artifact;
        record_native_acceptance(
            provenance, "systemc_plugin", native.compiler_fingerprint);
        return true;
    }

} // namespace

bool load_required_mapped_libraries(
    const project::Config& config,
    CompilationWorkspace& checked,
    diagnostic::Engine& diagnostics)
{
    std::map<std::string, const project::LibraryMapping*> mappings;
    for (const auto& mapping : config.library_mappings) {
        mappings.emplace(mapping.library, &mapping);
    }
    if (mappings.empty()) {
        return true;
    }

    std::set<std::string> required;
    bool unqualified_root = false;
    for (const auto& top : config.project.tops) {
        if (const auto library_name = qualified_library(top.target)) {
            if (mappings.contains(*library_name)) {
                required.insert(*library_name);
            }
        } else {
            unqualified_root = true;
        }
    }
    if (config.project.tops.empty() && !config.project.top.empty()) {
        if (const auto library_name = qualified_library(config.project.top)) {
            if (mappings.contains(*library_name)) {
                required.insert(*library_name);
            }
        } else {
            unqualified_root = true;
        }
    }
    for (const auto& binding : config.bindings) {
        if (binding.target.has_value()) {
            if (const auto library_name = qualified_library(*binding.target);
                library_name && mappings.contains(*library_name)) {
                required.insert(*library_name);
            }
        }
    }
    const bool hierarchy_query = design_has_hierarchy_queries(checked.parsed)
        || std::ranges::any_of(
            config.source_sets,
            [](const auto& source_set) {
                return source_set.language == project::Language::systemc;
            });
    if (unqualified_root || hierarchy_query) {
        for (const auto& library_name : config.elaboration.search_libraries) {
            if (mappings.contains(library_name)) {
                required.insert(library_name);
            }
        }
    }
    for (const auto& unit : checked.parsed.units) {
        for (const auto& context : unit.vhdl_context) {
            for (const auto& selected : context.selected_names) {
                const auto dot = selected.find('.');
                const auto library_name = std::string {
                    selected.substr(0, dot)
                };
                if (mappings.contains(library_name)) {
                    required.insert(library_name);
                }
            }
        }
    }

    enum class State { loading,
        loaded };
    std::map<std::string, State> states;
    std::set<std::string> mapped_udp_keys;
    std::set<std::string> mapped_unit_keys;
    std::vector<library::VhdlPackageDependency> vhdl_dependency_identity;
    std::optional<semantic::CompiledDesign> vhdl_dependency_bundle;
    std::optional<std::string> vhdl_dependency_bundle_bytes;
    const auto load_one = [&](const auto& self, const std::string& library_name)
        -> bool {
        if (const auto state = states.find(library_name); state != states.end()) {
            if (state->second == State::loaded) {
                return true;
            }
            diagnostics.error(
                "FSIM-LIB-0008",
                "mapped library dependency cycle reaches '" + library_name + "'");
            return false;
        }
        const auto mapping = mappings.find(library_name);
        if (mapping == mappings.end()) {
            diagnostics.error(
                "FSIM-LIB-0008",
                "mapped library dependency '" + library_name
                    + "' has no declared mapping");
            return false;
        }
        states.emplace(library_name, State::loading);
        auto metadata = library::load_metadata(
            mapping->second->path, library_name, diagnostics);
        if (!metadata.has_value()) {
            return false;
        }
        if (!metadata->trace_archive.empty()) {
            const auto archive = trace_archive_from_hex(
                metadata->trace_archive);
            auto decoded = decode_trace_archive(
                archive, TraceArchiveKind::Library);
            if (archive.empty() || !decoded.ok()) {
                for (const auto& diagnostic : decoded.diagnostics)
                    application_detail::import_diagnostic(
                        diagnostics, diagnostic);
                if (!diagnostics.has_error()) {
                    diagnostics.error("FSIM-TRACE-ARCHIVE-002",
                        ".fsimlib trace profile transport is malformed");
                }
                return false;
            }
            if (checked.trace_archive
                && !trace_archive_profiles_compatible(
                    *checked.trace_archive, decoded.snapshot)) {
                diagnostics.error("FSIM-TRACE-ARCHIVE-003",
                    "mapped libraries contain incompatible trace profiles");
                return false;
            }
            checked.trace_archive
                = std::make_shared<const TraceArchiveSnapshot>(
                    std::move(decoded.snapshot));
        }
        for (const auto& dependency : metadata->dependencies) {
            if (!self(self, dependency)) {
                return false;
            }
        }
        CheckedProject::MappedLibrary provenance;
        provenance.library = library_name;
        provenance.directory = mapping->second->path;
        provenance.metadata_digest = support::Sha256::hex(
            support::Sha256::digest(library::serialize_metadata(*metadata)));
        provenance.vhdl_package_dependencies = metadata->vhdl_package_dependencies;
        std::optional<frontend::VhdlStandard> mapped_vhdl_standard;
        for (const auto& standard : metadata->standards) {
            const auto language = project::parse_language(standard.language);
            if (!language.has_value()) {
                diagnostics.error(
                    "FSIM-LIB-0008",
                    "mapped library metadata names unsupported language '"
                        + standard.language + "'");
                return false;
            }
            const auto canonical = project::canonical_standard(
                *language, standard.revision);
            if (!canonical) {
                diagnostics.error(
                    "FSIM-LIB-0008",
                    "mapped library metadata names unsupported standard '"
                        + standard.revision + "' for " + standard.language);
                return false;
            }
            if (const auto revision = frontend_standard_revision(
                    *language, *canonical);
                std::ranges::find(
                    provenance.standard_revisions, revision)
                == provenance.standard_revisions.end()) {
                provenance.standard_revisions.push_back(revision);
            }
            if (*language == project::Language::vhdl) {
                if (const auto selected = project::parse_vhdl_standard(standard.revision)) {
                    mapped_vhdl_standard = frontend_vhdl_standard(*selected);
                }
            }
        }
        const auto source_settings_for = [&provenance, &diagnostics, &library_name](
                                             const std::string_view language_name,
                                             const std::string_view standard,
                                             const std::string_view profile)
            -> project::SourceSet* {
            const auto language = project::parse_language(language_name);
            const auto canonical = language
                    && *language == project::Language::systemc
                ? std::optional<std::string_view> { standard }
                : language
                ? project::canonical_standard(*language, standard)
                : std::nullopt;
            if (!language || !canonical) {
                diagnostics.error(
                    "FSIM-LIB-0008",
                    "mapped source provenance names unsupported language/standard '"
                        + std::string { language_name } + "/"
                        + std::string { standard } + "'");
                return nullptr;
            }
            const auto matches = [&](const project::SourceSet& settings) {
                if (settings.language != *language
                    || settings.standard != *canonical) {
                    return false;
                }
                if (*language == project::Language::verilog
                    || *language == project::Language::system_verilog) {
                    return project::compatibility_profile(
                               settings.compatibility_switches)
                        == profile;
                }
                return true;
            };
            if (const auto existing = std::ranges::find_if(
                    provenance.source_settings, matches);
                existing != provenance.source_settings.end()) {
                return &*existing;
            }
            project::SourceSet settings;
            settings.language = *language;
            settings.standard = *canonical;
            settings.library = library_name;
            settings.compilation_unit = "file";
            if ((*language == project::Language::verilog
                    || *language == project::Language::system_verilog)
                && profile != "none") {
                std::size_t begin = 0;
                while (begin <= profile.size()) {
                    const auto end = profile.find(',', begin);
                    const auto item = profile.substr(
                        begin,
                        end == std::string_view::npos
                            ? std::string_view::npos
                            : end - begin);
                    if (!item.empty()) {
                        settings.compatibility_switches.emplace_back(item);
                    }
                    if (end == std::string_view::npos) {
                        break;
                    }
                    begin = end + 1;
                }
            }
            provenance.source_settings.push_back(std::move(settings));
            return &provenance.source_settings.back();
        };
        for (const auto& source_entry : metadata->sources) {
            if (source_settings_for(
                    source_entry.language, source_entry.standard,
                    source_entry.compatibility_profile)
                == nullptr) {
                return false;
            }
        }
        for (const auto& unit_entry : metadata->units) {
            if (source_settings_for(
                    unit_entry.language, unit_entry.standard,
                    unit_entry.compatibility_profile)
                == nullptr) {
                return false;
            }
        }
        if (mapped_vhdl_standard) {
            for (const auto& owner : checked.parsed.units) {
                if (owner.language != frontend::Language::Vhdl2008) {
                    continue;
                }
                for (const auto& context : owner.vhdl_context) {
                    for (const auto& selected : context.selected_names) {
                        if (selected.starts_with(library_name + ".")
                            && owner.vhdl_standard != *mapped_vhdl_standard) {
                            diagnostics.error(
                                "FSIM-FE-VHORDER-011",
                                "mapped VHDL library '" + library_name
                                    + "' was analyzed as "
                                    + std::string { frontend::to_string(
                                        *mapped_vhdl_standard) }
                                    + " but the owning source uses "
                                    + std::string { frontend::to_string(
                                        owner.vhdl_standard) },
                                span(context.span));
                        }
                    }
                }
            }
        }
        for (const auto& source_entry : metadata->sources) {
            if (source_entry.artifact.empty()) {
                continue;
            }
            auto source_bytes = read_payload(
                mapping->second->path / source_entry.artifact,
                source_entry.checksum,
                diagnostics);
            if (!source_bytes.has_value()) {
                return false;
            }
            const auto logical_path = support::path_from_utf8(source_entry.logical_name);
            auto* source_settings = source_settings_for(
                source_entry.language, source_entry.standard,
                source_entry.compatibility_profile);
            if (source_settings == nullptr) {
                return false;
            }
            CheckedSource checked_source {
                logical_path, source_entry.language, source_entry.standard,
                source_entry.checksum, { }, source_entry.checksum,
                mapping->second->path / source_entry.artifact
            };
            checked_source.standard_revision = frontend_standard_revision(
                source_settings->language, source_settings->standard);
            if (source_entry.language == "systemc") {
                provenance.systemc_sources.push_back(
                    mapping->second->path / source_entry.artifact);
                checked.systemc_sources.push_back(std::move(checked_source));
            } else {
                checked.hdl_sources.push_back(std::move(checked_source));
            }
            source_settings->files.push_back(logical_path);
        }
        auto compiled_bytes = read_payload(
            mapping->second->path / metadata->compiled_hir_artifact,
            metadata->compiled_hir_checksum,
            diagnostics, kCompiledHirDecodeBudgetBytes);
        if (!compiled_bytes) {
            return false;
        }
        auto compiled_design = deserialize_compiled_hir_bundle(
            *compiled_bytes,
            support::path_to_utf8(
                mapping->second->path / metadata->compiled_hir_artifact),
            diagnostics);
        if (!compiled_design) {
            return false;
        }
        if (!metadata->vhdl_package_dependencies.empty()) {
            if (!validate_vhdl_package_dependencies(
                    metadata->vhdl_package_dependencies,
                    ".fsimlib", diagnostics)
                || !compiled_vhdl_package_dependencies_match(
                    *compiled_design,
                    metadata->vhdl_package_dependencies,
                    ".fsimlib", diagnostics)) {
                return false;
            }
        }
        auto remaining_compiled_units = compiled_unit_metadata_entries(
            *compiled_design, library_name);
        std::set<std::string> indexed_udp_keys;
        std::set<std::string> indexed_class_identities;
        for (const auto& entry : metadata->units) {
            if (entry.kind == "primitive") {
                const auto* declaration = find_compiled_udp(
                    *compiled_design, library_name, entry.name);
                if (declaration == nullptr
                    || !compiled_udp_metadata_matches(
                        entry, *declaration, library_name)) {
                    diagnostics.error(
                        "FSIM-LIB-0008",
                        "mapped UDP identity does not match its metadata index");
                    return false;
                }
                const auto key = compiled_udp_key(*declaration);
                indexed_udp_keys.insert(key);
                const auto local_duplicate = std::ranges::any_of(
                    checked.parsed.udp_declarations,
                    [&](const auto& existing) {
                        const auto existing_library = existing.library.empty()
                            ? std::string_view { "work" }
                            : std::string_view { existing.library };
                        return key == "udp:" + std::string { existing_library }
                            + "." + existing.name;
                    });
                if (local_duplicate || !mapped_udp_keys.insert(key).second) {
                    diagnostics.error(
                        "FSIM-LIB-0008",
                        "mapped UDP collides with an already loaded declaration '"
                            + key + "'");
                    return false;
                }
                provenance.unit_checksums.push_back(
                    metadata->compiled_hir_checksum);
                continue;
            }
            if (entry.kind == "class") {
                const auto* declaration = find_compiled_class(
                    *compiled_design, library_name, entry.name);
                if (declaration == nullptr
                    || !compiled_class_metadata_matches(
                        entry, *compiled_design, *declaration,
                        library_name)) {
                    diagnostics.error(
                        "FSIM-LIB-0008",
                        "mapped class identity does not match its compiled-HIR index");
                    return false;
                }
                if (!indexed_class_identities.insert(entry.name).second) {
                    diagnostics.error(
                        "FSIM-LIB-0008",
                        "mapped class has a duplicate compiled-HIR index '"
                            + entry.name + "'");
                    return false;
                }
                provenance.unit_checksums.push_back(
                    metadata->compiled_hir_checksum);
                continue;
            }
            const auto expected = std::ranges::find(
                remaining_compiled_units, entry);
            if (expected == remaining_compiled_units.end()) {
                diagnostics.error(
                    "FSIM-LIB-0008",
                    "mapped unit identity does not match its compiled-HIR index");
                return false;
            }
            const auto local_duplicate = std::ranges::any_of(
                checked.parsed.units, [&](const auto& existing) {
                    return metadata_identity_matches(entry, existing)
                        && (existing.library.empty()
                                ? std::string_view { "work" }
                                : std::string_view { existing.library })
                            == library_name;
                });
            const auto key = compiled_unit_metadata_key(entry, library_name);
            if (local_duplicate || !mapped_unit_keys.insert(key).second) {
                diagnostics.error(
                    "FSIM-LIB-0008",
                    "mapped unit collides with an already loaded design unit '"
                        + key + "'");
                return false;
            }
            remaining_compiled_units.erase(expected);
            provenance.unit_checksums.push_back(
                metadata->compiled_hir_checksum);
        }
        if (!remaining_compiled_units.empty()) {
            diagnostics.error(
                "FSIM-LIB-0008",
                "mapped compiled-HIR unit is missing from its metadata index");
            return false;
        }
        for (const auto& declaration
            : compiled_design->systemverilog_hir.udps()) {
            const auto declaration_library = declaration.library.empty()
                ? std::string_view { "work" }
                : std::string_view { declaration.library };
            if (declaration_library == library_name
                && !indexed_udp_keys.contains(
                    compiled_udp_key(declaration))) {
                diagnostics.error(
                    "FSIM-LIB-0008",
                    "mapped UDP identity does not match its metadata index");
                return false;
            }
        }
        for (const auto& declaration
            : compiled_design->systemverilog_hir.classes()) {
            const auto* owner = compiled_class_owner(
                *compiled_design, declaration);
            const auto declaration_library
                = owner == nullptr || owner->library.empty()
                ? std::string_view { "work" }
                : std::string_view { owner->library };
            if (declaration_library == library_name
                && !indexed_class_identities.contains(
                    semantic::sv::class_declaration_identity(declaration))) {
                diagnostics.error(
                    "FSIM-LIB-0008",
                    "mapped class identity is missing from its compiled-HIR index");
                return false;
            }
        }
        auto primary_bundle = semantic::extract_compiled_library(
            *compiled_design, library_name);
        if (!primary_bundle.ok()) {
            diagnostics.error(
                "FSIM-LIB-0008",
                "cannot project mapped primary library HIR: "
                    + primary_bundle.error);
            return false;
        }
        if (!metadata->vhdl_package_dependencies.empty()) {
            std::vector<std::string> dependency_libraries;
            for (const auto& dependency
                : metadata->vhdl_package_dependencies) {
                const auto separator = dependency.package.find('.');
                const auto dependency_library
                    = dependency.package.substr(0, separator);
                if (dependency_library != library_name
                    && std::ranges::find(
                           dependency_libraries, dependency_library)
                        == dependency_libraries.end()) {
                    dependency_libraries.push_back(dependency_library);
                }
            }
            auto dependencies = semantic::extract_compiled_libraries(
                *compiled_design, dependency_libraries);
            if (!dependencies.ok()) {
                diagnostics.error(
                    "FSIM-LIB-0008",
                    "cannot project mapped compiler dependency HIR: "
                        + dependencies.error);
                return false;
            }
            auto dependency_bytes = serialize_compiled_hir_bundle(
                *dependencies.design, diagnostics);
            if (!dependency_bytes) {
                return false;
            }
            const auto incompatible_environment
                = !vhdl_dependency_identity.empty()
                && (metadata->vhdl_package_dependencies
                        != vhdl_dependency_identity
                    || *dependency_bytes
                        != *vhdl_dependency_bundle_bytes);
            if (incompatible_environment) {
                diagnostics.error(
                    "FSIM-ART-VHDEP-001",
                    "mapped libraries select incompatible compiler-supplied "
                    "VHDL package environments; regenerate every .fsimlib "
                    "with one VHDL standard and this fsim build");
                return false;
            }
            if (vhdl_dependency_identity.empty()) {
                vhdl_dependency_identity
                    = metadata->vhdl_package_dependencies;
                vhdl_dependency_bundle_bytes
                    = std::move(*dependency_bytes);
                vhdl_dependency_bundle
                    = std::move(*dependencies.design);
            }
        }
        checked.mapped_compiled_designs.push_back(
            std::move(*primary_bundle.design));
        for (const auto& native : metadata->native_artifacts) {
            if (native.kind == "systemc_plugin") {
                if (!admit_systemc_artifact(
                        config, mapping->second->path, native, provenance,
                        diagnostics)) {
                    return false;
                }
            } else if (native.kind == "llvm_object") {
#if defined(FSIM_HAS_LLVM)
                if (!admit_llvm_artifact(
                        config, mapping->second->path, native, provenance,
                        diagnostics)) {
                    return false;
                }
#endif
            }
        }
        checked.mapped_libraries.push_back(std::move(provenance));
        states[library_name] = State::loaded;
        return true;
    };

    for (const auto& library_name : required) {
        if (!load_one(load_one, library_name)) {
            return false;
        }
    }
    if (vhdl_dependency_bundle) {
        checked.mapped_compiled_designs.push_back(
            std::move(*vhdl_dependency_bundle));
    }
    return true;
}

} // namespace fsim::app::application_detail
