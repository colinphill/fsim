// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"
#include "../systemc/producer_fingerprint.hpp"

#include "fsim/app/artifact_phase.hpp"
#include "fsim/app/design_artifact.hpp"
#include "fsim/app/sdf_phase_persistence.hpp"
#include "fsim/artifact/design.hpp"
#include "fsim/artifact/object.hpp"
#include "fsim/support/path.hpp"
#include "fsim/support/sha256.hpp"
#include "fsim/systemc/scv_artifact.hpp"
#include "fsim/version.hpp"

#include <array>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>

namespace fsim::app {
namespace {

    std::optional<std::string> read_design_payload(
        const std::filesystem::path& path,
        const std::string_view checksum,
        diagnostic::Engine& diagnostics)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            diagnostics.error(
                "FSIM-ART-0014",
                "cannot open .fsimdesign payload: " + support::path_to_utf8(path));
            return std::nullopt;
        }
        std::error_code size_error;
        const auto file_size = std::filesystem::file_size(path, size_error);
        if (size_error
            || file_size > std::string { }.max_size()
            || file_size
                > static_cast<std::uintmax_t>(
                    std::numeric_limits<std::streamsize>::max())) {
            diagnostics.error(
                "FSIM-ART-0014",
                "cannot size .fsimdesign payload: "
                    + support::path_to_utf8(path));
            return std::nullopt;
        }
        std::string bytes(static_cast<std::size_t>(file_size), '\0');
        input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (input.gcount() != static_cast<std::streamsize>(bytes.size())
            || input.bad()) {
            diagnostics.error(
                "FSIM-ART-0014",
                "cannot read .fsimdesign payload: "
                    + support::path_to_utf8(path));
            return std::nullopt;
        }
        if (support::Sha256::hex(support::Sha256::digest(bytes)) != checksum) {
            diagnostics.error(
                "FSIM-ART-0014",
                ".fsimdesign payload checksum mismatch: "
                    + support::path_to_utf8(path));
            return std::nullopt;
        }
        return bytes;
    }

    std::optional<std::uint64_t> verify_design_payload(
        const std::filesystem::path& path,
        const std::string_view checksum,
        diagnostic::Engine& diagnostics)
    {
        std::ifstream input(path, std::ios::binary);
        std::error_code size_error;
        const auto file_size = std::filesystem::file_size(path, size_error);
        if (!input || size_error
            || file_size > static_cast<std::uintmax_t>(
                std::numeric_limits<std::streamsize>::max())) {
            diagnostics.error("FSIM-ART-0014",
                "cannot open or size .fsimdesign payload: "
                    + support::path_to_utf8(path));
            return std::nullopt;
        }
        support::Sha256 digest;
        std::array<char, 64 * 1024> buffer { };
        std::uint64_t total { };
        while (input) {
            input.read(buffer.data(),
                static_cast<std::streamsize>(buffer.size()));
            const auto count = input.gcount();
            if (count > 0) {
                digest.update(std::string_view { buffer.data(),
                    static_cast<std::size_t>(count) });
                total += static_cast<std::uint64_t>(count);
            }
        }
        if (!input.eof() || total != file_size) {
            diagnostics.error("FSIM-ART-0014",
                "cannot read .fsimdesign payload: "
                    + support::path_to_utf8(path));
            return std::nullopt;
        }
        if (support::Sha256::hex(digest.finish()) != checksum) {
            diagnostics.error("FSIM-ART-0014",
                ".fsimdesign payload checksum mismatch: "
                    + support::path_to_utf8(path));
            return std::nullopt;
        }
        return static_cast<std::uint64_t>(file_size);
    }

    std::string selected_root_identity(
        const BuiltProject& project,
        const std::string_view alias,
        const std::string_view fallback)
    {
        const auto found = std::ranges::find_if(
            project.design.specializations(), [&](const auto& specialization) {
                return specialization.instance == alias;
            });
        return found == project.design.specializations().end()
            ? std::string { fallback }
            : found->unit;
    }

    const artifact::DesignPayload* payload_by_kind(
        const artifact::DesignMetadata& metadata,
        const std::string_view kind)
    {
        const auto found = std::ranges::find_if(
            metadata.payloads,
            [&](const auto& payload) { return payload.kind == kind; });
        return found == metadata.payloads.end() ? nullptr : &*found;
    }

    std::optional<std::pair<std::string, std::string>> systemc_target(
        const std::string_view target)
    {
        constexpr std::string_view prefix = "systemc:";
        if (!target.starts_with(prefix)) {
            return std::nullopt;
        }
        const auto body = target.substr(prefix.size());
        const auto separator = body.find('.');
        if (separator == std::string_view::npos || separator == 0
            || separator + 1 == body.size()) {
            return std::nullopt;
        }
        return std::pair {
            std::string { body.substr(0, separator) },
            std::string { body.substr(separator + 1) }
        };
    }

    struct RehydratedSystemC {
        std::vector<std::filesystem::path> native_plugins;
        std::vector<std::shared_ptr<systemc::HierarchyRegistry>> registries;
        std::vector<std::uint64_t> roots;
    };

    std::optional<RehydratedSystemC> rehydrate_systemc(
        const std::filesystem::path& directory,
        const artifact::DesignMetadata& metadata,
        elaboration::ElaboratedDesign& runtime,
        semantic::design::DesignIr& design_ir,
        diagnostic::Engine& diagnostics)
    {
        if (metadata.systemc_plugins.empty()) {
            if (!runtime.systemc_instances().empty()
                || !runtime.systemc_processes().empty()
                || !runtime.systemc_objects().empty()) {
                diagnostics.error(
                    "FSIM-ART-0014",
                    ".fsimdesign contains SystemC runtime state without embedded plug-ins");
                return std::nullopt;
            }
            return RehydratedSystemC { };
        }
        auto state = runtime.state();

        project::SystemCSection host_settings;
        diagnostic::Engine fingerprint_diagnostics;
        const auto current_fingerprint = systemc::plugin_producer_fingerprint(
            host_settings, directory, fingerprint_diagnostics);
        if (!current_fingerprint) {
            diagnostics.error(
                "FSIM-ART-0014",
                "cannot establish the current SystemC upstream/compiler/"
                "standard-library/bridge producer identity");
            return std::nullopt;
        }

        RehydratedSystemC result;
        std::map<std::string, std::shared_ptr<systemc::HierarchyRegistry>> by_library;
        for (const auto& record : metadata.systemc_plugins) {
            if (!systemc::validate_scv_artifact_compatibility(
                    record.scv_compatibility,
                    "embedded .fsimdesign SystemC plug-in", diagnostics)) {
                return std::nullopt;
            }
            const auto plugin_directory = directory / record.directory;
            auto plugin_metadata = systemc::load_incremental_plugin_metadata(
                plugin_directory, diagnostics);
            if (!plugin_metadata) {
                return std::nullopt;
            }
            const auto metadata_bytes = systemc::serialize_incremental_plugin_metadata(*plugin_metadata);
            std::vector<std::string> factories;
            for (const auto& factory : plugin_metadata->factories) {
                factories.push_back(factory.name);
            }
            if (plugin_metadata->logical_library != record.logical_library
                || plugin_metadata->input_digest != record.input_digest
                || plugin_metadata->link_digest != record.link_digest
                || plugin_metadata->compiler_fingerprint
                    != record.compiler_fingerprint
                || plugin_metadata->scv_compatibility
                    != record.scv_compatibility
                || plugin_metadata->library_checksum != record.library_checksum
                || support::Sha256::hex(support::Sha256::digest(metadata_bytes))
                    != record.metadata_checksum
                || factories != record.factories) {
                diagnostics.error(
                    "FSIM-ART-0014",
                    "embedded SystemC plug-in metadata disagrees with design provenance");
                return std::nullopt;
            }
            if (record.compiler_fingerprint != *current_fingerprint) {
                diagnostics.error(
                    "FSIM-ART-0014",
                    "embedded SystemC plug-in producer identity is stale or "
                    "incompatible with the current upstream source, compiler, "
                    "standard library, or bridge (embedded "
                        + record.compiler_fingerprint + ", current "
                        + *current_fingerprint + ")");
                return std::nullopt;
            }
            auto registry = systemc::load_incremental_plugin(
                plugin_directory, diagnostics, *current_fingerprint);
            if (!registry
                || !by_library.emplace(record.logical_library, registry).second) {
                if (!diagnostics.has_error()) {
                    diagnostics.error(
                        "FSIM-ART-0014",
                        "duplicate embedded SystemC logical library '"
                            + record.logical_library + "'");
                }
                return std::nullopt;
            }
            result.native_plugins.push_back(
                plugin_directory / plugin_metadata->library);
            result.registries.push_back(std::move(registry));
        }

        const auto resolution = application_detail::magnitude_and_unit(
            metadata.time_resolution);
        const auto factor = resolution
            ? application_detail::unit_femtoseconds(resolution->unit)
            : std::nullopt;
        if (!resolution || !factor || resolution->magnitude == 0
            || resolution->magnitude
                > std::numeric_limits<std::uint64_t>::max() / *factor) {
            diagnostics.error(
                "FSIM-ART-0014",
                "cannot restore SystemC time resolution '"
                    + metadata.time_resolution + "'");
            return std::nullopt;
        }
        for (const auto& registry : result.registries) {
            registry->set_time_resolution(resolution->magnitude * *factor);
        }

        std::set<std::string> instance_paths;
        for (const auto& instance : state.systemc_instances) {
            instance_paths.insert(instance.instance);
        }
        const auto has_systemc_parent = [&](const std::string& path) {
            return std::ranges::any_of(instance_paths, [&](const auto& parent) {
                return parent.size() < path.size()
                    && path.starts_with(parent)
                    && path[parent.size()] == '.';
            });
        };
        std::map<std::string, std::uint64_t> new_handles_by_path;
        std::unordered_map<std::uint64_t, std::string> old_paths_by_handle;
        const auto remember = [&](const std::uint64_t handle, std::string path) {
            if (handle != 0) {
                old_paths_by_handle.emplace(handle, std::move(path));
            }
        };
        for (const auto& object : state.systemc_objects) {
            remember(object.native_handle, object.name);
        }
        for (const auto& instance : state.systemc_instances) {
            remember(instance.native_handle, instance.instance);
            for (const auto& port : instance.ports) {
                remember(port.native_handle, instance.instance + "." + port.name);
            }
            for (const auto& event : instance.events) {
                remember(event.native_handle, instance.instance + "." + event.name);
            }
            for (const auto& channel : instance.primitive_channels) {
                remember(channel.native_handle, instance.instance + "." + channel.name);
            }
            for (const auto& signal : instance.internal_signals) {
                remember(signal.native_handle, instance.instance + "." + signal.name);
            }
            for (const auto& export_object : instance.exports) {
                remember(
                    export_object.native_handle,
                    instance.instance + "." + export_object.name);
            }
        }

        for (const auto& instance : state.systemc_instances) {
            if (has_systemc_parent(instance.instance)) {
                continue;
            }
            const auto target = systemc_target(instance.target);
            if (!target) {
                diagnostics.error(
                    "FSIM-ART-0014",
                    "serialized SystemC instance has an invalid target '"
                        + instance.target + "'");
                return std::nullopt;
            }
            const auto found = by_library.find(target->first);
            if (found == by_library.end()) {
                diagnostics.error(
                    "FSIM-ART-0014",
                    "serialized SystemC instance requires missing logical library '"
                        + target->first + "'");
                return std::nullopt;
            }
            std::string error;
            auto module = found->second->instantiate(
                target->second, instance.instance, 0,
                instance.construction_values, error);
            if (!module) {
                diagnostics.error(
                    "FSIM-ART-0014",
                    "cannot reconstruct SystemC instance '" + instance.instance
                        + "': " + error);
                return std::nullopt;
            }
            result.roots.push_back(module->handle);
            std::vector<std::uint64_t> pending { module->handle };
            while (!pending.empty()) {
                const auto handle = pending.back();
                pending.pop_back();
                const auto info = found->second->object_info(handle);
                if (!info || !new_handles_by_path.emplace(info->path, handle).second) {
                    diagnostics.error(
                        "FSIM-ART-0014",
                        "reconstructed SystemC hierarchy has duplicate or missing object metadata");
                    return std::nullopt;
                }
                for (const auto& child : found->second->child_objects(handle)) {
                    pending.push_back(child.handle);
                }
            }
        }

        std::unordered_map<std::uint64_t, std::uint64_t> handle_map;
        for (const auto& [old_handle, path] : old_paths_by_handle) {
            const auto found = new_handles_by_path.find(path);
            if (found == new_handles_by_path.end()) {
                diagnostics.error(
                    "FSIM-ART-0014",
                    "reconstructed SystemC hierarchy is missing object '" + path + "'");
                return std::nullopt;
            }
            handle_map.emplace(old_handle, found->second);
        }
        const auto remap = [&](std::uint64_t& handle) {
            if (handle == 0) {
                return true;
            }
            const auto found = handle_map.find(handle);
            if (found == handle_map.end()) {
                return false;
            }
            handle = found->second;
            return true;
        };
        const auto require_remap = [&](std::uint64_t& handle) {
            if (remap(handle)) {
                return true;
            }
            diagnostics.error(
                "FSIM-ART-0014",
                "reconstructed SystemC hierarchy cannot remap native handle "
                    + std::to_string(handle));
            return false;
        };
        for (auto& instance : state.systemc_instances) {
            if (!require_remap(instance.native_handle)) {
                return std::nullopt;
            }
            for (auto& port : instance.ports) {
                if (!require_remap(port.native_handle)) {
                    return std::nullopt;
                }
            }
            for (auto& event : instance.events) {
                if (!require_remap(event.native_handle)) {
                    return std::nullopt;
                }
            }
            for (auto& channel : instance.primitive_channels) {
                if (!require_remap(channel.native_handle)) {
                    return std::nullopt;
                }
            }
            for (auto& signal : instance.internal_signals) {
                if (!require_remap(signal.native_handle)) {
                    return std::nullopt;
                }
            }
            for (auto& export_object : instance.exports) {
                if (!require_remap(export_object.native_handle)) {
                    return std::nullopt;
                }
            }
        }
        for (auto& process : state.systemc_processes) {
            if (!require_remap(process.native_handle)) {
                return std::nullopt;
            }
        }
        for (auto& object : state.systemc_objects) {
            if (!require_remap(object.native_handle)) {
                return std::nullopt;
            }
        }
        for (auto& object : design_ir.mutable_objects()) {
            if (object.native_handle != 0 && !require_remap(object.native_handle)) {
                return std::nullopt;
            }
        }
        for (auto& boundary : design_ir.mutable_boundaries()) {
            if (boundary.native_handle != 0
                && !require_remap(boundary.native_handle)) {
                return std::nullopt;
            }
        }
        auto rebuilt = elaboration::ElaboratedDesign::from_state(std::move(state));
        if (!rebuilt) {
            diagnostics.error(
                "FSIM-ART-0014",
                "remapped SystemC runtime state is structurally invalid");
            return std::nullopt;
        }
        runtime = std::move(*rebuilt);

        try {
            for (const auto& instance : runtime.systemc_instances()) {
                const auto bind = [&](const std::uint64_t handle, const auto signal) {
                    const auto owner = std::ranges::find_if(
                        result.registries, [&](const auto& registry) {
                            return registry->owns_handle(handle);
                        });
                    if (owner == result.registries.end()) {
                        throw std::logic_error { "remapped SystemC object has no owner" };
                    }
                    (*owner)->bind_runtime_object(handle, signal);
                };
                for (const auto& port : instance.ports)
                    bind(port.native_handle, port.signal);
                for (const auto& event : instance.events)
                    bind(event.native_handle, event.signal);
                for (const auto& signal : instance.internal_signals)
                    bind(signal.native_handle, signal.signal);
                for (const auto& export_object : instance.exports)
                    bind(export_object.native_handle, export_object.signal);
            }
            for (const auto& registry : result.registries) {
                std::vector<std::uint64_t> owned_roots;
                std::ranges::copy_if(
                    result.roots, std::back_inserter(owned_roots),
                    [&](const auto handle) { return registry->owns_handle(handle); });
                registry->complete_elaboration(owned_roots);
            }
        } catch (const std::exception& error) {
            diagnostics.error(
                "FSIM-ART-0014",
                "cannot bind reconstructed SystemC hierarchy: "
                    + std::string { error.what() });
            return std::nullopt;
        }
        return result;
    }

} // namespace

static bool publish_design_artifact_impl(
    const project::Config& config,
    const BuiltProject& project,
    const std::filesystem::path& destination,
    diagnostic::Engine& diagnostics,
    elaboration::ElaboratedDesign* consumable_design)
{
    auto semantics = serialize_semantic_state(project.semantics, diagnostics);
    auto design_ir = serialize_design_ir_state(project.design_ir, diagnostics);
    auto classes = serialize_class_state(
        project.systemverilog_class_specializations, diagnostics);
    auto constraint_hir = serialize_systemverilog_constraint_hir_state(
        project.systemverilog_hir, project.semantics, diagnostics);
    auto vhdl_hir = serialize_vhdl_hir_state(
        project.vhdl_hir, project.semantics, diagnostics);
    auto coverage = serialize_systemverilog_coverage_state(
        project.systemverilog_coverage, diagnostics);
    if (!semantics || !design_ir || !classes || !constraint_hir
        || !vhdl_hir || !coverage) {
        return false;
    }

    artifact::DesignMetadata metadata;
    metadata.producer = std::string { "fsim " } + std::string { version };
    metadata.time_resolution = project.time_resolution;
    metadata.delay_mode = std::string { project::to_string(config.run.delay_mode) };
    metadata.optimization = std::string { project::to_string(project.optimization) };
    metadata.cache_key = project.cache_key;
    const auto coverage_identity = artifact::make_code_coverage_artifact_identity(
        project.code_coverage_enabled);
    if (!coverage_identity.ok()) {
        diagnostics.error(
            std::string { artifact::kCodeCoverageArtifactDiagnostic },
            "could not construct the v3 design code-coverage identity");
        return false;
    }
    metadata.code_coverage = coverage_identity.identity;
    metadata.uvm_release = std::string { project::to_string(
        project.systemverilog_uvm_provenance.release) };
    metadata.uvm_source_identity = project.systemverilog_uvm_provenance.source_identity;
    metadata.seed = project.seed;
    metadata.entropy_seed = project.entropy_seed;
    metadata.search_libraries = config.elaboration.search_libraries;
    for (std::size_t index = 0; index < config.project.tops.size(); ++index) {
        const auto& root = config.project.tops[index];
        const auto alias = root.alias.empty()
            && index < project.design.roots().size()
            ? project.design.roots()[index]
            : root.alias;
        metadata.roots.push_back({ alias, root.target,
            selected_root_identity(project, alias, root.target) });
    }
    if (metadata.roots.empty() && !config.project.top.empty()) {
        const auto alias = project.design.roots().empty()
            ? std::string { "top" }
            : project.design.roots().front();
        metadata.roots.push_back({ alias, config.project.top,
            selected_root_identity(project, alias, config.project.top) });
    }
    fsim::runtime::SystemVerilogUvmCheckpointProvenance uvm_provenance;
    uvm_provenance.content_identity = project.cache_key;
    uvm_provenance.cache_identity = project.cache_key + ":"
        + std::string { project::to_string(project.optimization) };
    uvm_provenance.artifact_identity = project.artifact_identity;
    uvm_provenance.uvm_release = metadata.uvm_release;
    uvm_provenance.source_identity = metadata.uvm_source_identity;
    uvm_provenance.roots.reserve(metadata.roots.size());
    for (const auto& root : metadata.roots) {
        uvm_provenance.roots.push_back(root.alias);
    }
    auto uvm_bootstrap = fsim::runtime::make_systemverilog_uvm_bootstrap_checkpoint(
        std::move(uvm_provenance));
    if (!uvm_bootstrap) {
        diagnostics.error(
            "FSIM-UVM-STATE-002",
            "could not construct the bounded portable UVM bootstrap state");
        return false;
    }
    auto uvm_state = serialize_systemverilog_uvm_state(
        uvm_bootstrap.artifact, diagnostics);
    if (!uvm_state) {
        return false;
    }
    for (const auto& binding : config.bindings) {
        metadata.bindings.push_back(
            { binding.instance, binding.target, binding.resolver });
    }
    for (const auto& object : project.objects) {
        artifact::DesignObjectInput input;
        input.metadata_digest = object.metadata_digest;
        input.compilation_digest = object.compilation_digest;
        input.language = object.language;
        input.standard = object.standard;
        input.compatibility_profile = object.compatibility_profile;
        input.library = object.library;
        input.code_coverage = object.code_coverage;
        input.vhdl_package_dependencies = object.vhdl_package_dependencies;
        input.unit_checksums = object.unit_checksums;
        metadata.objects.push_back(std::move(input));
    }
    for (const auto& provenance : project.vhdl_unit_provenance) {
        artifact::DesignVhdlUnitProvenance record;
        record.unit = provenance.unit.value();
        record.standard = provenance.standard;
        record.predefined_environment = provenance.predefined_environment;
        record.compatibility_profile = provenance.compatibility_profile;
        record.package_dependencies = provenance.package_dependencies;
        metadata.vhdl_unit_provenance.push_back(std::move(record));
    }
    for (const auto& unit : project.semantics.units()) {
        if (unit.language != semantic::Language::verilog
            && unit.language != semantic::Language::system_verilog) {
            continue;
        }
        const auto identity = unit.library + "::" + unit.name;
        const auto revision = project.verilog_unit_revisions.find(identity);
        const auto profile
            = project.verilog_unit_compatibility_profiles.find(identity);
        if (revision == project.verilog_unit_revisions.end()
            || profile == project.verilog_unit_compatibility_profiles.end()
            || profile->second.empty()) {
            diagnostics.error(
                "FSIM-ART-0014",
                "standalone design publication requires complete per-unit "
                "Verilog/SystemVerilog standard and compatibility provenance");
            return false;
        }
        metadata.verilog_unit_provenance.push_back({ unit.id.value(),
            unit.language == semantic::Language::verilog
                ? "verilog"
                : "systemverilog",
            std::string { frontend::revision_string(revision->second) },
            profile->second });
    }
    std::set<std::string> selected_plugin_libraries;
    for (const auto& instance : project.design.systemc_instances()) {
        if (const auto target = systemc_target(instance.target)) {
            selected_plugin_libraries.insert(target->first);
        }
    }
    metadata.specialization_count = project.design.specializations().size();
    metadata.signal_count = project.design.signals().size();
    metadata.process_count = project.design.processes().size();

    std::optional<std::string> runtime;
    std::optional<elaboration::ElaboratedDesignState> runtime_state;
    std::optional<std::string> runtime_checksum;
    if (consumable_design != nullptr) {
        runtime_state.emplace(std::move(*consumable_design).state());
        runtime_checksum = runtime_state_checksum(*runtime_state, diagnostics);
    } else {
        runtime = serialize_runtime_state(project.design, diagnostics);
        if (runtime) {
            runtime_checksum = support::Sha256::hex(
                support::Sha256::digest(*runtime));
        }
    }
    if (!runtime_checksum) {
        return false;
    }
    const auto add_payload = [&](
                                 const std::string_view kind,
                                 const std::filesystem::path& path,
                                 const std::string& bytes) {
        metadata.payloads.push_back({ std::string { kind }, path,
            support::Sha256::hex(support::Sha256::digest(bytes)) });
    };
    metadata.payloads.push_back(
        { "runtime", "state/runtime.bin", *runtime_checksum });
    add_payload("semantics", "state/semantics.bin", *semantics);
    add_payload("design-ir", "state/design-ir.bin", *design_ir);
    add_payload("classes", "state/classes.bin", *classes);
    add_payload(
        "sv-constraint-hir", "state/sv-constraint-hir.bin", *constraint_hir);
    add_payload("vhdl-hir", "state/vhdl-hir.bin", *vhdl_hir);
    add_payload("sv-coverage", "state/sv-coverage.bin", *coverage);
    add_payload("sv-uvm", "state/sv-uvm.bin", *uvm_state);
    std::vector<library::PortablePayload> payloads;
    payloads.reserve(runtime ? 8 : 7);
    if (runtime) {
        payloads.push_back(
            { metadata.payloads[0].artifact, std::move(*runtime) });
    }
    payloads.insert(payloads.end(), {
        { metadata.payloads[1].artifact, std::move(*semantics) },
        { metadata.payloads[2].artifact, std::move(*design_ir) },
        { metadata.payloads[3].artifact, std::move(*classes) },
        { metadata.payloads[4].artifact, std::move(*constraint_hir) },
        { metadata.payloads[5].artifact, std::move(*vhdl_hir) },
        { metadata.payloads[6].artifact, std::move(*coverage) },
        { metadata.payloads[7].artifact, std::move(*uvm_state) }
    });
    std::vector<artifact::GeneratedDesignPayload> generated_payloads;
    if (runtime_state) {
        generated_payloads.push_back({ metadata.payloads[0].artifact,
            *runtime_checksum,
            [&](const std::filesystem::path& path,
                diagnostic::Engine& writer_diagnostics) {
                std::ofstream output(path, std::ios::binary | std::ios::trunc);
                if (!output) {
                    writer_diagnostics.error("FSIM-ART-0014",
                        "cannot open generated runtime payload: "
                            + support::path_to_utf8(path));
                    return false;
                }
                if (!serialize_runtime_state(
                        *runtime_state, output, writer_diagnostics)) {
                    return false;
                }
                output.close();
                if (!output) {
                    writer_diagnostics.error("FSIM-ART-0014",
                        "cannot finish generated runtime payload: "
                            + support::path_to_utf8(path));
                    return false;
                }
                return true;
            } });
    }
    std::set<std::string> plugin_libraries;
    for (std::size_t index = 0; index < project.systemc_plugins.size(); ++index) {
        const auto native = project.systemc_plugins[index];
        const auto artifact_directory = native.parent_path().parent_path();
        auto plugin = systemc::load_incremental_plugin_metadata(
            artifact_directory, diagnostics);
        if (!plugin) {
            diagnostics.error(
                "FSIM-ART-0014",
                "standalone design publication requires linked .fsimscplugin "
                "provenance for every SystemC image");
            return false;
        }
        if (!selected_plugin_libraries.contains(plugin->logical_library)) {
            continue;
        }
        if (!plugin_libraries.insert(plugin->logical_library).second) {
            diagnostics.error(
                "FSIM-ART-0014",
                "standalone design contains duplicate SystemC logical library '"
                    + plugin->logical_library + "'");
            return false;
        }
        const auto embedded = std::filesystem::path { "systemc" } / "plugins"
            / ("plugin-" + std::to_string(index));
        const auto metadata_bytes = systemc::serialize_incremental_plugin_metadata(*plugin);
        auto native_bytes = read_design_payload(
            artifact_directory / plugin->library,
            plugin->library_checksum, diagnostics);
        if (!native_bytes) {
            return false;
        }
        artifact::DesignSystemCPlugin record;
        record.logical_library = plugin->logical_library;
        record.input_digest = plugin->input_digest;
        record.link_digest = plugin->link_digest;
        record.compiler_fingerprint = plugin->compiler_fingerprint;
        record.scv_compatibility = plugin->scv_compatibility;
        record.directory = embedded;
        record.metadata_checksum = support::Sha256::hex(
            support::Sha256::digest(metadata_bytes));
        record.library_checksum = plugin->library_checksum;
        for (const auto& factory : plugin->factories) {
            record.factories.push_back(factory.name);
        }
        metadata.systemc_plugins.push_back(record);
        const auto metadata_path = embedded / systemc::kIncrementalPluginMetadataFilename;
        const auto native_path = embedded / plugin->library;
        add_payload(
            "systemc-plugin-metadata:" + plugin->logical_library,
            metadata_path, metadata_bytes);
        add_payload(
            "systemc-plugin-native:" + plugin->logical_library,
            native_path, *native_bytes);
        payloads.push_back({ metadata_path, metadata_bytes });
        payloads.push_back({ native_path, std::move(*native_bytes) });
    }
    if (plugin_libraries != selected_plugin_libraries) {
        diagnostics.error(
            "FSIM-ART-0014",
            "standalone design publication is missing linked provenance for one "
            "or more selected SystemC logical libraries");
        return false;
    }
    if (metadata.objects.empty() && metadata.systemc_plugins.empty()) {
        diagnostics.error(
            "FSIM-ART-0014",
            "standalone design publication requires explicit HDL object or "
            "SystemC plug-in provenance");
        return false;
    }
    metadata.specialization_cache_keys = project.specialization_cache_keys;
    metadata.unit_count = project.semantics.units().size();
    metadata.semantic_source_count = project.semantics.source_files().size();
    if (project.trace_archive) {
        auto trace = encode_trace_archive(
            *project.trace_archive, TraceArchiveKind::Design);
        if (!trace.ok()) {
            for (const auto& diagnostic : trace.diagnostics)
                application_detail::import_diagnostic(diagnostics, diagnostic);
            return false;
        }
        metadata.trace_archive = trace_archive_hex(trace.archive);
    }
    metadata.design_digest = artifact::compute_design_digest(metadata);
    if (!append_sdf_phase_payloads(project, metadata, payloads, diagnostics))
        return false;
    if (!project.sdf_phase_artifacts.empty()) {
        uvm_bootstrap.artifact.provenance.content_identity = metadata.cache_key;
        uvm_bootstrap.artifact.provenance.cache_identity = metadata.cache_key
            + ":" + std::string { project::to_string(project.optimization) };
        auto updated_uvm_state = serialize_systemverilog_uvm_state(
            uvm_bootstrap.artifact, diagnostics);
        if (!updated_uvm_state)
            return false;
        metadata.payloads[7].checksum = support::Sha256::hex(
            support::Sha256::digest(*updated_uvm_state));
        const auto uvm_artifact = metadata.payloads[7].artifact;
        const auto payload = std::ranges::find(
            payloads, uvm_artifact, &library::PortablePayload::path);
        if (payload == payloads.end()) {
            diagnostics.error("FSIM-ART-0014",
                "standalone design publication lost its UVM state payload");
            return false;
        }
        payload->bytes = std::move(*updated_uvm_state);
    }
    metadata.design_digest = artifact::compute_design_digest(metadata);
    return artifact::publish_design(destination, metadata, payloads,
        generated_payloads, diagnostics);
}

bool publish_design_artifact(
    const project::Config& config,
    const BuiltProject& project,
    const std::filesystem::path& destination,
    diagnostic::Engine& diagnostics)
{
    return publish_design_artifact_impl(
        config, project, destination, diagnostics, nullptr);
}

bool publish_design_artifact(
    const project::Config& config,
    BuiltProject&& project,
    const std::filesystem::path& destination,
    diagnostic::Engine& diagnostics)
{
    return publish_design_artifact_impl(
        config, project, destination, diagnostics, &project.design);
}

std::optional<BuiltProject> load_design_artifact(
    const std::filesystem::path& directory,
    diagnostic::Engine& diagnostics)
{
    auto metadata = artifact::load_design_metadata(directory, diagnostics);
    if (!metadata) {
        return std::nullopt;
    }
    std::shared_ptr<const TraceArchiveSnapshot> trace_archive;
    if (!metadata->trace_archive.empty()) {
        const auto archive = trace_archive_from_hex(metadata->trace_archive);
        auto decoded = decode_trace_archive(
            archive, TraceArchiveKind::Design);
        if (archive.empty() || !decoded.ok()) {
            for (const auto& diagnostic : decoded.diagnostics)
                application_detail::import_diagnostic(diagnostics, diagnostic);
            if (!diagnostics.has_error()) {
                diagnostics.error("FSIM-TRACE-ARCHIVE-002",
                    ".fsimdesign trace profile transport is malformed");
            }
            return std::nullopt;
        }
        trace_archive = std::make_shared<const TraceArchiveSnapshot>(
            std::move(decoded.snapshot));
    }
    for (const auto& object : metadata->objects) {
        if (!application_detail::validate_vhdl_package_dependencies(
                object.vhdl_package_dependencies,
                ".fsimdesign", diagnostics)) {
            return std::nullopt;
        }
    }
    for (const auto& provenance : metadata->vhdl_unit_provenance) {
        if (!application_detail::validate_vhdl_package_dependencies(
                provenance.package_dependencies,
                ".fsimdesign VHDL unit provenance", diagnostics)) {
            return std::nullopt;
        }
    }
    const auto* runtime_index = payload_by_kind(*metadata, "runtime");
    const auto* semantic_index = payload_by_kind(*metadata, "semantics");
    const auto* design_ir_index = payload_by_kind(*metadata, "design-ir");
    const auto* class_index = payload_by_kind(*metadata, "classes");
    const auto* constraint_hir_index = payload_by_kind(
        *metadata, "sv-constraint-hir");
    const auto* vhdl_hir_index = payload_by_kind(*metadata, "vhdl-hir");
    const auto* coverage_index = payload_by_kind(*metadata, "sv-coverage");
    const auto* uvm_index = payload_by_kind(*metadata, "sv-uvm");
    if (runtime_index == nullptr || semantic_index == nullptr
        || design_ir_index == nullptr || class_index == nullptr
        || constraint_hir_index == nullptr || vhdl_hir_index == nullptr
        || coverage_index == nullptr || uvm_index == nullptr) {
        diagnostics.error(
            "FSIM-ART-0014", ".fsimdesign is missing a required state payload");
        return std::nullopt;
    }
    const auto runtime_path = directory / runtime_index->artifact;
    const auto runtime_size = verify_design_payload(
        runtime_path, runtime_index->checksum, diagnostics);
    if (!runtime_size) {
        return std::nullopt;
    }
    std::ifstream runtime_input(runtime_path, std::ios::binary);
    if (!runtime_input) {
        diagnostics.error("FSIM-ART-0014",
            "cannot open .fsimdesign payload: "
                + support::path_to_utf8(runtime_path));
        return std::nullopt;
    }
    auto runtime = deserialize_runtime_state(runtime_input, *runtime_size,
        support::path_to_utf8(runtime_index->artifact), diagnostics);

    auto semantic_bytes = read_design_payload(
        directory / semantic_index->artifact, semantic_index->checksum,
        diagnostics);
    auto semantics = semantic_bytes
        ? deserialize_semantic_state(*semantic_bytes,
              support::path_to_utf8(semantic_index->artifact), diagnostics)
        : std::optional<semantic::Model> { };
    semantic_bytes.reset();

    auto design_ir_bytes = read_design_payload(
        directory / design_ir_index->artifact, design_ir_index->checksum,
        diagnostics);
    auto design_ir = design_ir_bytes
        ? deserialize_design_ir_state(*design_ir_bytes,
              support::path_to_utf8(design_ir_index->artifact), diagnostics)
        : std::optional<semantic::design::DesignIr> { };
    design_ir_bytes.reset();

    auto class_bytes = read_design_payload(
        directory / class_index->artifact, class_index->checksum,
        diagnostics);
    auto classes = class_bytes
        ? deserialize_class_state(*class_bytes,
              support::path_to_utf8(class_index->artifact), diagnostics)
        : std::optional<std::vector<
              frontend::SystemVerilogClassSpecialization>> { };
    class_bytes.reset();

    auto constraint_hir_bytes = read_design_payload(
        directory / constraint_hir_index->artifact,
        constraint_hir_index->checksum,
        diagnostics);
    auto constraint_hir = semantics && constraint_hir_bytes
        ? deserialize_systemverilog_constraint_hir_state(
              *constraint_hir_bytes,
              support::path_to_utf8(constraint_hir_index->artifact),
              *semantics, diagnostics)
        : std::optional<semantic::sv::Hir> { };
    constraint_hir_bytes.reset();

    auto vhdl_hir_bytes = read_design_payload(
        directory / vhdl_hir_index->artifact, vhdl_hir_index->checksum,
        diagnostics);
    auto vhdl_hir = semantics && vhdl_hir_bytes
        ? deserialize_vhdl_hir_state(*vhdl_hir_bytes,
              support::path_to_utf8(vhdl_hir_index->artifact), *semantics,
              diagnostics)
        : std::optional<semantic::vhdl::Hir> { };
    vhdl_hir_bytes.reset();

    auto coverage_bytes = read_design_payload(
        directory / coverage_index->artifact, coverage_index->checksum,
        diagnostics);
    auto coverage = coverage_bytes
        ? deserialize_systemverilog_coverage_state(*coverage_bytes,
              support::path_to_utf8(coverage_index->artifact), diagnostics)
        : std::optional<frontend::SystemVerilogCoverageState> { };
    coverage_bytes.reset();

    auto uvm_bytes = read_design_payload(
        directory / uvm_index->artifact, uvm_index->checksum, diagnostics);
    auto uvm_state = uvm_bytes
        ? deserialize_systemverilog_uvm_state(*uvm_bytes,
              support::path_to_utf8(uvm_index->artifact), diagnostics)
        : std::optional<fsim::runtime::SystemVerilogUvmCheckpointArtifact> { };
    uvm_bytes.reset();
    if (!runtime || !semantics || !design_ir || !classes || !constraint_hir
        || !vhdl_hir || !coverage || !uvm_state
        || !design_ir->valid(*semantics)
        || !application_detail::valid_runtime_projection(*design_ir, *runtime)) {
        if (!diagnostics.has_error()) {
            diagnostics.error(
                "FSIM-ART-0014",
                ".fsimdesign state projections are inconsistent");
        }
        return std::nullopt;
    }
    auto live_systemc = rehydrate_systemc(
        directory, *metadata, *runtime, *design_ir, diagnostics);
    if (!live_systemc) {
        return std::nullopt;
    }
    std::vector<std::string> roots;
    roots.reserve(metadata->roots.size());
    for (const auto& root : metadata->roots) {
        roots.push_back(root.alias);
    }
    const fsim::runtime::SystemVerilogUvmCheckpointProvenance expected_uvm {
        metadata->cache_key,
        metadata->cache_key + ":" + metadata->optimization,
        uvm_state->provenance.artifact_identity,
        roots,
        metadata->uvm_release,
        metadata->uvm_source_identity
    };
    if (runtime->roots() != roots || design_ir->roots() != roots
        || semantics->units().size() != metadata->unit_count
        || semantics->source_files().size() != metadata->semantic_source_count
        || runtime->specializations().size() != metadata->specialization_count
        || runtime->signals().size() != metadata->signal_count
        || runtime->processes().size() != metadata->process_count
        || fsim::runtime::validate_systemverilog_uvm_checkpoint(
               *uvm_state, expected_uvm)
            != fsim::runtime::SystemVerilogUvmCheckpointError::None) {
        diagnostics.error(
            "FSIM-ART-0014",
            ".fsimdesign metadata counts or roots disagree with state payloads");
        return std::nullopt;
    }
    std::vector<CheckedProject::ObjectProvenance> objects;
    objects.reserve(metadata->objects.size());
    for (const auto& object : metadata->objects) {
        CheckedProject::ObjectProvenance provenance;
        provenance.metadata_digest = object.metadata_digest;
        provenance.compilation_digest = object.compilation_digest;
        provenance.language = object.language;
        provenance.standard = object.standard;
        provenance.compatibility_profile = object.compatibility_profile;
        provenance.library = object.library;
        provenance.code_coverage = object.code_coverage;
        provenance.vhdl_package_dependencies = object.vhdl_package_dependencies;
        provenance.unit_checksums = object.unit_checksums;
        objects.push_back(std::move(provenance));
    }
    std::vector<VhdlUnitProvenance> vhdl_unit_provenance;
    vhdl_unit_provenance.reserve(metadata->vhdl_unit_provenance.size());
    for (const auto& record : metadata->vhdl_unit_provenance) {
        if (record.unit >= semantics->units().size()) {
            diagnostics.error(
                "FSIM-ART-0014",
                ".fsimdesign VHDL unit provenance references an invalid "
                "semantic unit");
            return std::nullopt;
        }
        VhdlUnitProvenance provenance;
        provenance.unit = semantics->units()[record.unit].id;
        provenance.standard = record.standard;
        provenance.predefined_environment = record.predefined_environment;
        provenance.compatibility_profile = record.compatibility_profile;
        provenance.package_dependencies = record.package_dependencies;
        vhdl_unit_provenance.push_back(std::move(provenance));
    }
    std::map<std::string, frontend::StandardRevision, std::less<>>
        verilog_unit_revisions;
    std::map<std::string, std::string, std::less<>>
        verilog_unit_compatibility_profiles;
    std::size_t semantic_verilog_units { };
    for (const auto& unit : semantics->units()) {
        if (unit.language == semantic::Language::verilog
            || unit.language == semantic::Language::system_verilog) {
            ++semantic_verilog_units;
        }
    }
    for (const auto& record : metadata->verilog_unit_provenance) {
        if (record.unit >= semantics->units().size()) {
            diagnostics.error(
                "FSIM-ART-0014",
                ".fsimdesign Verilog/SystemVerilog provenance references an "
                "invalid semantic unit");
            return std::nullopt;
        }
        const auto& unit = semantics->units()[record.unit];
        const auto expected_language = unit.language
                == semantic::Language::verilog
            ? project::Language::verilog
            : unit.language == semantic::Language::system_verilog
            ? project::Language::system_verilog
            : project::Language::systemc;
        if ((record.language == "verilog")
                != (expected_language == project::Language::verilog)
            || (record.language == "systemverilog")
                != (expected_language == project::Language::system_verilog)) {
            diagnostics.error(
                "FSIM-ART-0014",
                ".fsimdesign Verilog/SystemVerilog provenance language does "
                "not match its semantic unit");
            return std::nullopt;
        }
        const auto canonical = project::canonical_standard(
            expected_language, record.standard);
        if (!canonical || record.compatibility_profile.empty()) {
            diagnostics.error(
                "FSIM-ART-0014",
                ".fsimdesign Verilog/SystemVerilog provenance names an "
                "unsupported standard or empty compatibility profile");
            return std::nullopt;
        }
        const auto identity = unit.library + "::" + unit.name;
        if (!verilog_unit_revisions.emplace(
                                       identity,
                                       application_detail::frontend_standard_revision(
                                           expected_language, *canonical))
                .second
            || !verilog_unit_compatibility_profiles.emplace(
                                                       identity, record.compatibility_profile)
                .second) {
            diagnostics.error(
                "FSIM-ART-0014",
                ".fsimdesign contains duplicate Verilog/SystemVerilog unit "
                "provenance");
            return std::nullopt;
        }
    }
    if (metadata->format >= 8
        && metadata->verilog_unit_provenance.size()
            != semantic_verilog_units) {
        diagnostics.error(
            "FSIM-ART-0014",
            ".fsimdesign omits Verilog/SystemVerilog semantic-unit provenance");
        return std::nullopt;
    }
    const auto optimization = metadata->optimization == "O0"
        ? project::Optimization::o0
        : project::Optimization::o2;
    auto primary_hierarchy = live_systemc->registries.empty()
        ? std::shared_ptr<systemc::HierarchyRegistry> { }
        : live_systemc->registries.front();
    auto built = BuiltProject {
        std::move(*runtime), std::move(*design_ir), std::move(*semantics),
        std::move(*constraint_hir), std::move(*vhdl_hir),
        metadata->cache_key, metadata->time_resolution,
        directory.parent_path() / ".fsim-sim-cache", optimization,
        metadata->specialization_cache_keys,
        std::move(live_systemc->native_plugins),
        std::move(primary_hierarchy), std::move(live_systemc->roots),
        metadata->seed, metadata->entropy_seed, false,
        directory.parent_path(), std::move(live_systemc->registries), { },
        std::move(objects),
        { project::parse_systemverilog_uvm_release(metadata->uvm_release)
                .value_or(project::SystemVerilogUvmRelease::none),
            metadata->uvm_source_identity },
        metadata->design_digest, std::move(*classes),
        std::move(*coverage), std::move(*uvm_state),
        std::move(vhdl_unit_provenance),
        std::move(verilog_unit_revisions),
        std::move(verilog_unit_compatibility_profiles), { }, { }
    };
    if (!restore_sdf_phase_artifacts(directory, *metadata, built, diagnostics))
        return std::nullopt;
    built.trace_archive = std::move(trace_archive);
    built.code_coverage_enabled = metadata->code_coverage.enabled;
    return built;
}

bool elaborate_artifact(
    const project::Config& config,
    const std::span<const std::filesystem::path> objects,
    const std::filesystem::path& destination,
    diagnostic::Engine& diagnostics)
{
    auto built = build_objects(config, objects, diagnostics);
    return built
        && publish_design_artifact(
            config, std::move(*built), destination, diagnostics);
}

bool elaborate_artifact(
    const project::Config& config,
    const std::span<const std::filesystem::path> objects,
    const std::span<const std::filesystem::path> systemc_plugins,
    const std::filesystem::path& destination,
    diagnostic::Engine& diagnostics)
{
    auto built = build_objects(config, objects, systemc_plugins, diagnostics);
    return built
        && publish_design_artifact(
            config, std::move(*built), destination, diagnostics);
}

std::optional<ArtifactInspection> inspect_artifact(
    const std::filesystem::path& directory,
    diagnostic::Engine& diagnostics)
{
    const auto object_metadata = std::filesystem::exists(directory / artifact::kObjectMetadataFilename);
    const auto design_metadata = std::filesystem::exists(directory / artifact::kDesignMetadataFilename);
    const auto systemc_object_metadata = std::filesystem::exists(
        directory / systemc::kIncrementalObjectMetadataFilename);
    const auto systemc_plugin_metadata = std::filesystem::exists(
        directory / systemc::kIncrementalPluginMetadataFilename);
    const auto metadata_count = static_cast<unsigned>(object_metadata)
        + static_cast<unsigned>(design_metadata)
        + static_cast<unsigned>(systemc_object_metadata)
        + static_cast<unsigned>(systemc_plugin_metadata);
    if (metadata_count != 1) {
        diagnostics.error(
            "FSIM-ART-0014",
            "artifact inspection requires exactly one .fsimobj, .fsimscobj, "
            ".fsimscplugin, or .fsimdesign metadata record: "
                + support::path_to_utf8(directory));
        return std::nullopt;
    }
    ArtifactInspection result;
    result.compatible = true;
    if (object_metadata) {
        const auto metadata = artifact::load_object_metadata(
            directory, diagnostics);
        if (!metadata) {
            return std::nullopt;
        }
        result.phase = ArtifactPhaseKind::compilation;
        result.format = metadata->format;
        result.language = metadata->language;
        result.standard = metadata->standard;
        result.library = metadata->library;
        result.digests.push_back(metadata->compilation_digest);
        for (const auto& unit : metadata->units) {
            result.units.push_back(
                unit.language + ":" + metadata->library + "." + unit.name);
            result.digests.push_back(unit.checksum);
        }
        return result;
    }
    if (systemc_object_metadata) {
        const auto metadata = systemc::load_incremental_object_metadata(
            directory, diagnostics);
        if (!metadata) {
            return std::nullopt;
        }
        result.phase = ArtifactPhaseKind::systemc_compilation;
        result.format = metadata->format;
        result.runtime_abi = metadata->runtime_abi;
        result.language = "systemc";
        result.toolchain = metadata->toolchain;
        result.target = metadata->target;
        result.digests = {
            metadata->compiler_fingerprint, metadata->input_digest,
            metadata->compilation_digest, metadata->object_checksum
        };
        for (const auto& input : metadata->inputs) {
            result.units.push_back(input.logical_name);
            result.digests.push_back(input.checksum);
        }
        return result;
    }
    if (systemc_plugin_metadata) {
        const auto metadata = systemc::load_incremental_plugin_metadata(
            directory, diagnostics);
        if (!metadata) {
            return std::nullopt;
        }
        result.phase = ArtifactPhaseKind::systemc_link;
        result.format = metadata->format;
        result.runtime_abi = metadata->runtime_abi;
        result.language = "systemc";
        result.library = metadata->logical_library;
        result.toolchain = metadata->toolchain;
        result.target = metadata->target;
        result.digests = {
            metadata->compiler_fingerprint, metadata->input_digest,
            metadata->link_digest, metadata->library_checksum
        };
        result.digests.insert(
            result.digests.end(), metadata->object_digests.begin(),
            metadata->object_digests.end());
        for (const auto& factory : metadata->factories) {
            result.units.push_back(
                "systemc:" + metadata->logical_library + "." + factory.name);
        }
        return result;
    }
    const auto metadata = artifact::load_design_metadata(directory, diagnostics);
    if (!metadata) {
        return std::nullopt;
    }
    result.phase = ArtifactPhaseKind::elaboration;
    result.format = metadata->format;
    result.runtime_abi = metadata->runtime_abi;
    result.digests = { metadata->design_digest, metadata->cache_key };
    result.process_count = metadata->process_count;
    for (const auto& root : metadata->roots) {
        result.roots.push_back(root.alias);
        result.units.push_back(root.selected_identity);
    }
    for (const auto& object : metadata->objects) {
        result.digests.push_back(object.metadata_digest);
        result.digests.push_back(object.compilation_digest);
        result.digests.insert(
            result.digests.end(), object.unit_checksums.begin(),
            object.unit_checksums.end());
    }
    for (const auto& payload : metadata->payloads) {
        result.digests.push_back(payload.checksum);
    }
    return result;
}

} // namespace fsim::app

namespace fsim::app::application_detail {
namespace {

#if defined(FSIM_HAS_LLVM)
constexpr std::string_view kAotReceiptSchema = "fsim-aot-receipt-v1";
constexpr std::string_view kAotSelectionPolicy = "fsim-native-selection-v1";
constexpr std::uintmax_t kMaximumAotReceiptBytes = 8192U;

struct AotReceiptContext {
    std::string key;
    std::string design;
    std::string optimization;
    std::string host;
    std::string inventory;
};

struct AotReceipt {
    cli::AotScope scope { cli::AotScope::selected };
    std::uint64_t processes { };
    std::uint64_t modules { };
};

[[nodiscard]] std::string aot_process_inventory(
    const BuiltProject& built)
{
    compiler::CacheKeyBuilder builder;
    builder.add("kind", kAotSelectionPolicy);
    builder.add("design", built.artifact_identity);
    builder.add("process-count",
        std::to_string(built.design_ir.processes().size()));
    builder.add("specialization-count",
        std::to_string(built.design_ir.specializations().size()));
    for (const auto& specialization : built.design_ir.specializations()) {
        builder.add("specialization-id",
            std::to_string(specialization.id.value()));
        builder.add("specialization-name", specialization.name);
        builder.add("specialization-instance",
            std::to_string(specialization.instance.value()));
        builder.add("specialization-process-count",
            std::to_string(specialization.processes.size()));
        for (const auto process : specialization.processes) {
            builder.add("process", std::to_string(process.value()));
        }
    }
    for (const auto& key : built.specialization_cache_keys) {
        builder.add("specialization-cache-key", key);
    }
    return builder.finish();
}

[[nodiscard]] AotReceiptContext aot_receipt_context(
    const BuiltProject& built)
{
    const auto optimization = jit_optimization(built.optimization);
    const auto host = compiler::LlvmJit::native_host_identity(optimization);
    compiler::CacheKeyBuilder builder;
    builder.add("kind", kAotReceiptSchema);
    builder.add("selection-policy", kAotSelectionPolicy);
    builder.add("design", built.artifact_identity);
    builder.add("optimization", project::to_string(built.optimization));
    builder.add("native-host", host.fingerprint);
    return {
        builder.finish(), built.artifact_identity,
        std::string { project::to_string(built.optimization) },
        host.fingerprint, aot_process_inventory(built)
    };
}

[[nodiscard]] compiler::ObjectCache aot_receipt_cache(
    const std::filesystem::path& cache_path)
{
    return compiler::ObjectCache {
        cache_path / "llvm-native" / "aot-receipts"
    };
}

[[nodiscard]] std::string serialize_aot_receipt(
    const AotReceiptContext& context,
    const cli::AotScope scope,
    const std::uint64_t processes,
    const std::uint64_t modules)
{
    std::ostringstream output;
    output << kAotReceiptSchema << '\n'
           << "scope="
           << (scope == cli::AotScope::all ? "all" : "selected") << '\n'
           << "design=" << context.design << '\n'
           << "optimization=" << context.optimization << '\n'
           << "host=" << context.host << '\n'
           << "inventory=" << context.inventory << '\n'
           << "processes=" << processes << '\n'
           << "modules=" << modules << '\n';
    return std::move(output).str();
}

[[nodiscard]] bool parse_aot_count(
    const std::string_view value, std::uint64_t& result)
{
    const auto [end, error]
        = std::from_chars(value.begin(), value.end(), result);
    return error == std::errc { } && end == value.end();
}

[[nodiscard]] std::optional<AotReceipt> parse_aot_receipt(
    const std::span<const std::byte> payload,
    const AotReceiptContext& context)
{
    if (payload.empty() || payload.size() > kMaximumAotReceiptBytes) {
        return std::nullopt;
    }
    const std::string text {
        reinterpret_cast<const char*>(payload.data()), payload.size()
    };
    std::array<std::string_view, 8> lines;
    auto remaining = std::string_view { text };
    for (auto& line : lines) {
        const auto newline = remaining.find('\n');
        if (newline == std::string_view::npos) {
            return std::nullopt;
        }
        line = remaining.substr(0U, newline);
        remaining.remove_prefix(newline + 1U);
    }
    if (!remaining.empty() || lines[0] != kAotReceiptSchema
        || !lines[1].starts_with("scope=")
        || lines[2] != "design=" + context.design
        || lines[3] != "optimization=" + context.optimization
        || lines[4] != "host=" + context.host
        || lines[5] != "inventory=" + context.inventory
        || !lines[6].starts_with("processes=")
        || !lines[7].starts_with("modules=")) {
        return std::nullopt;
    }
    const auto scope = lines[1].substr(std::string_view { "scope=" }.size());
    AotReceipt receipt;
    if (scope == "all") {
        receipt.scope = cli::AotScope::all;
    } else if (scope != "selected") {
        return std::nullopt;
    }
    if (!parse_aot_count(
            lines[6].substr(std::string_view { "processes=" }.size()),
            receipt.processes)
        || !parse_aot_count(
            lines[7].substr(std::string_view { "modules=" }.size()),
            receipt.modules)) {
        return std::nullopt;
    }
    return receipt;
}

[[nodiscard]] std::optional<AotReceipt> load_aot_receipt(
    const BuiltProject& built,
    diagnostic::Engine& diagnostics)
{
    const auto context = aot_receipt_context(built);
    const auto cache = aot_receipt_cache(built.cache_path);
    const auto path = cache.path_for(context.key);
    std::error_code size_error;
    const auto bytes = std::filesystem::file_size(path, size_error);
    if (!size_error && bytes > kMaximumAotReceiptBytes) {
        diagnostics.warning("FSIM-AOT-002",
            "ignoring an oversized native AOT receipt");
        return std::nullopt;
    }
    std::error_code error;
    const auto payload = cache.load(context.key, error);
    if (!payload) {
        if (error != std::errc::no_such_file_or_directory) {
            diagnostics.warning("FSIM-AOT-002",
                "ignoring an unreadable native AOT receipt: "
                    + error.message());
        }
        return std::nullopt;
    }
    const auto receipt = parse_aot_receipt(*payload, context);
    if (!receipt) {
        diagnostics.warning("FSIM-AOT-002",
            "ignoring an incompatible native AOT receipt");
    }
    return receipt;
}

[[nodiscard]] bool store_aot_receipt(
    const AotReceiptContext& context,
    const std::filesystem::path& cache_path,
    const cli::AotScope scope,
    const std::uint64_t processes,
    const std::uint64_t modules,
    diagnostic::Engine& diagnostics)
{
    const auto payload
        = serialize_aot_receipt(context, scope, processes, modules);
    const auto bytes = std::as_bytes(
        std::span { payload.data(), payload.size() });
    std::error_code error;
    if (!aot_receipt_cache(cache_path).store(context.key, bytes, error)) {
        diagnostics.error("FSIM-AOT-001",
            "cannot publish the native AOT receipt: " + error.message());
        return false;
    }
    return true;
}
#endif

} // namespace

int handle_elaborate(
    const cli::Invocation& invocation,
    const project::Config& config,
    diagnostic::Engine& diagnostics,
    std::ostream& output,
    std::ostream&)
{
    if (!invocation.artifact_output) {
        return 1;
    }
    auto built = build_objects(
        config, invocation.objects, invocation.systemc_plugins, diagnostics);
    const auto root_count = built ? built->design.roots().size() : 0;
    if (!built || !publish_design_artifact(config, std::move(*built),
            *invocation.artifact_output, diagnostics)) {
        return 1;
    }
    output << "elaborated " << root_count
           << " root(s) into "
           << support::path_to_utf8(*invocation.artifact_output) << '\n';
    if (invocation.aot != std::optional<bool> { true }) {
        return 0;
    }
#if defined(FSIM_HAS_LLVM)
    try {
        auto reloaded = load_design_artifact(
            *invocation.artifact_output, diagnostics);
        if (!reloaded) {
            return 1;
        }
        reloaded->optimization = config.build.optimization;
        if (invocation.cache_directory) {
            reloaded->cache_path = *invocation.cache_directory;
        }
        const auto scope
            = invocation.aot_scope.value_or(cli::AotScope::selected);
        reloaded->compiled_process_selection = scope == cli::AotScope::all
            ? BuiltProject::CompiledProcessSelection::all
            : BuiltProject::CompiledProcessSelection::selected;
        const auto receipt_context = aot_receipt_context(*reloaded);
        const auto receipt_cache_path = reloaded->cache_path;
        Simulation simulation { std::move(*reloaded), config.run.max_deltas,
            SimulationEngine::compiled,
            SystemVerilogVpiRuntimeUpdates::omitted };
        simulation.await_all_native_compilation();
        const auto statistics = simulation.native_cache_statistics(false);
        if (statistics.store_failures != 0U) {
            diagnostics.error("FSIM-AOT-001",
                "native AOT compilation could not publish "
                    + std::to_string(statistics.store_failures)
                    + " cache object(s); the portable design remains valid");
            return 1;
        }
        if (statistics.load_failures != 0U
            || statistics.prune_failures != 0U) {
            diagnostics.warning("FSIM-AOT-002",
                "native AOT recovered from cache read or pruning failures");
        }
        if (!store_aot_receipt(receipt_context, receipt_cache_path, scope,
                simulation.compiled_process_count(),
                simulation.compiled_module_count(), diagnostics)) {
            return 1;
        }
        output << "AOT populated " << simulation.compiled_process_count()
               << " process(es) in " << simulation.compiled_module_count()
               << " module(s), scope="
               << (scope == cli::AotScope::all ? "all" : "selected")
               << ", cache="
               << support::path_to_utf8(receipt_cache_path)
               << ", hits=" << statistics.hits
               << ", misses=" << statistics.misses
               << ", stores=" << statistics.stores << '\n';
    } catch (const std::exception& error) {
        diagnostics.error("FSIM-AOT-001",
            std::string { "native AOT compilation failed; the portable design "
                          "remains valid: " }
                + error.what());
        return 1;
    }
    return 0;
#else
    diagnostics.error("FSIM-AOT-001",
        "native AOT compilation requires an LLVM-enabled fsim build; the "
        "portable design remains valid");
    return 1;
#endif
}

int handle_simulate(
    const cli::Invocation& invocation,
    const project::Config& config,
    diagnostic::Engine& diagnostics,
    std::ostream& output,
    std::ostream&)
{
    if (!invocation.design) {
        return 1;
    }
    const auto metadata = artifact::load_design_metadata(
        *invocation.design, diagnostics);
    if (!metadata) {
        return 1;
    }
    if (invocation.delay_mode
        && metadata->delay_mode
            != project::to_string(*invocation.delay_mode)) {
        diagnostics.error(
            "FSIM-ART-0014",
            "requested delay mode '"
                + std::string { project::to_string(*invocation.delay_mode) }
                + "' does not match the elaborated .fsimdesign delay mode '"
                + metadata->delay_mode + "'");
        return 1;
    }
    auto built = load_design_artifact(*invocation.design, diagnostics);
    if (!built) {
        return 1;
    }
    // Native lowering is a simulation-time choice. The design artifact keeps
    // the elaborated HDL state, while an explicit standalone -O selection
    // must govern the JIT pipeline and native-cache identity for this run.
    built->optimization = config.build.optimization;
    if (invocation.cache_directory) {
        built->cache_path = *invocation.cache_directory;
    }
    if (invocation.file_root) {
        built->file_root = *invocation.file_root;
    }
    if (invocation.random_seed) {
        built->seed = entropy_seed();
        built->entropy_seed = true;
    } else if (invocation.seed) {
        built->seed = *invocation.seed;
        built->entropy_seed = false;
    }
    const auto engine = invocation.engine.value_or("compiled");
    const auto simulation_engine = engine == "interpreter"
        ? SimulationEngine::interpreter
        : engine == "debug" ? SimulationEngine::debug
                            : SimulationEngine::compiled;
    if (simulation_engine == SimulationEngine::compiled) {
        const auto policy = invocation.compiled_processes.value_or(
            cli::CompiledProcessPolicy::automatic);
        if (policy == cli::CompiledProcessPolicy::all) {
            built->compiled_process_selection
                = BuiltProject::CompiledProcessSelection::all;
        } else if (policy == cli::CompiledProcessPolicy::selected) {
            built->compiled_process_selection
                = BuiltProject::CompiledProcessSelection::selected;
        } else {
#if defined(FSIM_HAS_LLVM)
            try {
                const auto receipt = load_aot_receipt(*built, diagnostics);
                if (receipt && receipt->scope == cli::AotScope::all) {
                    built->compiled_process_selection
                        = BuiltProject::CompiledProcessSelection::all;
                    output << "using forced-all AOT cache receipt ("
                           << receipt->processes << " process(es), "
                           << receipt->modules << " module(s))\n";
                }
            } catch (const std::exception& error) {
                diagnostics.warning("FSIM-AOT-002",
                    std::string { "cannot inspect the native AOT receipt; "
                                  "using selected compilation: " }
                        + error.what());
            }
#endif
        }
    }
    return run_built_project(
        std::move(*built), simulation_engine,
        config, invocation.plusargs, diagnostics, output);
}

} // namespace fsim::app::application_detail
