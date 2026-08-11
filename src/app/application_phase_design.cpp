// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"

#include "fsim/app/artifact_phase.hpp"
#include "fsim/app/design_artifact.hpp"
#include "fsim/artifact/design.hpp"
#include "fsim/artifact/object.hpp"
#include "fsim/support/path.hpp"
#include "fsim/support/sha256.hpp"
#include "fsim/version.hpp"

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
        std::ostringstream contents;
        contents << input.rdbuf();
        if (!input.good() && !input.eof()) {
            diagnostics.error(
                "FSIM-ART-0014",
                "cannot read .fsimdesign payload: " + support::path_to_utf8(path));
            return std::nullopt;
        }
        auto bytes = contents.str();
        if (support::Sha256::hex(support::Sha256::digest(bytes)) != checksum) {
            diagnostics.error(
                "FSIM-ART-0014",
                ".fsimdesign payload checksum mismatch: "
                    + support::path_to_utf8(path));
            return std::nullopt;
        }
        return bytes;
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
        auto state = runtime.state();
        if (metadata.systemc_plugins.empty()) {
            if (!state.systemc_instances.empty() || !state.systemc_processes.empty()
                || !state.systemc_objects.empty()) {
                diagnostics.error(
                    "FSIM-ART-0014",
                    ".fsimdesign contains SystemC runtime state without embedded plug-ins");
                return std::nullopt;
            }
            return RehydratedSystemC { };
        }

        RehydratedSystemC result;
        std::map<std::string, std::shared_ptr<systemc::HierarchyRegistry>> by_library;
        for (const auto& record : metadata.systemc_plugins) {
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
                || plugin_metadata->library_checksum != record.library_checksum
                || support::Sha256::hex(support::Sha256::digest(metadata_bytes))
                    != record.metadata_checksum
                || factories != record.factories) {
                diagnostics.error(
                    "FSIM-ART-0014",
                    "embedded SystemC plug-in metadata disagrees with design provenance");
                return std::nullopt;
            }
            auto registry = systemc::load_incremental_plugin(
                plugin_directory, diagnostics);
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

bool publish_design_artifact(
    const project::Config& config,
    const BuiltProject& project,
    const std::filesystem::path& destination,
    diagnostic::Engine& diagnostics)
{
    auto runtime = serialize_runtime_state(project.design, diagnostics);
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
    if (!runtime || !semantics || !design_ir || !classes || !constraint_hir
        || !vhdl_hir || !coverage) {
        return false;
    }

    artifact::DesignMetadata metadata;
    metadata.producer = std::string { "fsim " } + std::string { version };
    metadata.time_resolution = project.time_resolution;
    metadata.delay_mode = std::string { project::to_string(config.run.delay_mode) };
    metadata.optimization = std::string { project::to_string(project.optimization) };
    metadata.cache_key = project.cache_key;
    metadata.uvm_release = std::string { project::to_string(
        project.systemverilog_uvm_provenance.release) };
    metadata.uvm_source_identity = project.systemverilog_uvm_provenance.source_identity;
    metadata.seed = project.seed;
    metadata.entropy_seed = project.entropy_seed;
    metadata.search_libraries = config.elaboration.search_libraries;
    for (const auto& root : config.project.tops) {
        metadata.roots.push_back({ root.alias, root.target,
            selected_root_identity(project, root.alias, root.target) });
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
        metadata.objects.push_back({ object.metadata_digest, object.compilation_digest, object.language,
            object.standard, object.library, object.unit_checksums });
    }
    const auto add_payload = [&](
                                 const std::string_view kind,
                                 const std::filesystem::path& path,
                                 const std::string& bytes) {
        metadata.payloads.push_back({ std::string { kind }, path,
            support::Sha256::hex(support::Sha256::digest(bytes)) });
    };
    add_payload("runtime", "state/runtime.bin", *runtime);
    add_payload("semantics", "state/semantics.bin", *semantics);
    add_payload("design-ir", "state/design-ir.bin", *design_ir);
    add_payload("classes", "state/classes.bin", *classes);
    add_payload(
        "sv-constraint-hir", "state/sv-constraint-hir.bin", *constraint_hir);
    add_payload("vhdl-hir", "state/vhdl-hir.bin", *vhdl_hir);
    add_payload("sv-coverage", "state/sv-coverage.bin", *coverage);
    add_payload("sv-uvm", "state/sv-uvm.bin", *uvm_state);
    std::vector<library::PortablePayload> payloads {
        { metadata.payloads[0].artifact, std::move(*runtime) },
        { metadata.payloads[1].artifact, std::move(*semantics) },
        { metadata.payloads[2].artifact, std::move(*design_ir) },
        { metadata.payloads[3].artifact, std::move(*classes) },
        { metadata.payloads[4].artifact, std::move(*constraint_hir) },
        { metadata.payloads[5].artifact, std::move(*vhdl_hir) },
        { metadata.payloads[6].artifact, std::move(*coverage) },
        { metadata.payloads[7].artifact, std::move(*uvm_state) }
    };
    std::set<std::string> selected_plugin_libraries;
    for (const auto& instance : project.design.systemc_instances()) {
        if (const auto target = systemc_target(instance.target)) {
            selected_plugin_libraries.insert(target->first);
        }
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
    metadata.specialization_count = project.design.specializations().size();
    metadata.signal_count = project.design.signals().size();
    metadata.process_count = project.design.processes().size();
    metadata.design_digest = artifact::compute_design_digest(metadata);
    return artifact::publish_design(
        destination, metadata, payloads, diagnostics);
}

std::optional<BuiltProject> load_design_artifact(
    const std::filesystem::path& directory,
    diagnostic::Engine& diagnostics)
{
    auto metadata = artifact::load_design_metadata(directory, diagnostics);
    if (!metadata) {
        return std::nullopt;
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
    auto runtime_bytes = read_design_payload(
        directory / runtime_index->artifact, runtime_index->checksum,
        diagnostics);
    auto semantic_bytes = read_design_payload(
        directory / semantic_index->artifact, semantic_index->checksum,
        diagnostics);
    auto design_ir_bytes = read_design_payload(
        directory / design_ir_index->artifact, design_ir_index->checksum,
        diagnostics);
    auto class_bytes = read_design_payload(
        directory / class_index->artifact, class_index->checksum,
        diagnostics);
    auto constraint_hir_bytes = read_design_payload(
        directory / constraint_hir_index->artifact,
        constraint_hir_index->checksum,
        diagnostics);
    auto vhdl_hir_bytes = read_design_payload(
        directory / vhdl_hir_index->artifact, vhdl_hir_index->checksum,
        diagnostics);
    auto coverage_bytes = read_design_payload(
        directory / coverage_index->artifact, coverage_index->checksum,
        diagnostics);
    auto uvm_bytes = read_design_payload(
        directory / uvm_index->artifact, uvm_index->checksum, diagnostics);
    if (!runtime_bytes || !semantic_bytes || !design_ir_bytes || !class_bytes
        || !constraint_hir_bytes || !vhdl_hir_bytes || !coverage_bytes
        || !uvm_bytes) {
        return std::nullopt;
    }
    auto runtime = deserialize_runtime_state(
        *runtime_bytes, support::path_to_utf8(runtime_index->artifact),
        diagnostics);
    auto semantics = deserialize_semantic_state(
        *semantic_bytes, support::path_to_utf8(semantic_index->artifact),
        diagnostics);
    auto design_ir = deserialize_design_ir_state(
        *design_ir_bytes, support::path_to_utf8(design_ir_index->artifact),
        diagnostics);
    auto classes = deserialize_class_state(
        *class_bytes, support::path_to_utf8(class_index->artifact),
        diagnostics);
    auto constraint_hir = semantics
        ? deserialize_systemverilog_constraint_hir_state(
              *constraint_hir_bytes,
              support::path_to_utf8(constraint_hir_index->artifact),
              *semantics, diagnostics)
        : std::optional<semantic::sv::Hir> { };
    auto vhdl_hir = semantics
        ? deserialize_vhdl_hir_state(
              *vhdl_hir_bytes,
              support::path_to_utf8(vhdl_hir_index->artifact), *semantics,
              diagnostics)
        : std::optional<semantic::vhdl::Hir> { };
    auto coverage = deserialize_systemverilog_coverage_state(
        *coverage_bytes, support::path_to_utf8(coverage_index->artifact),
        diagnostics);
    auto uvm_state = deserialize_systemverilog_uvm_state(
        *uvm_bytes, support::path_to_utf8(uvm_index->artifact), diagnostics);
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
        provenance.library = object.library;
        provenance.unit_checksums = object.unit_checksums;
        objects.push_back(std::move(provenance));
    }
    const auto optimization = metadata->optimization == "O0"
        ? project::Optimization::o0
        : project::Optimization::o2;
    auto primary_hierarchy = live_systemc->registries.empty()
        ? std::shared_ptr<systemc::HierarchyRegistry> { }
        : live_systemc->registries.front();
    return BuiltProject {
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
        std::move(*coverage), std::move(*uvm_state)
    };
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
            config, *built, destination, diagnostics);
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
            config, *built, destination, diagnostics);
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
    if (!built || !publish_design_artifact(config, *built, *invocation.artifact_output, diagnostics)) {
        return 1;
    }
    output << "elaborated " << built->design.roots().size()
           << " root(s) into "
           << support::path_to_utf8(*invocation.artifact_output) << '\n';
    return 0;
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
    return run_built_project(
        std::move(*built), simulation_engine,
        config, invocation.plusargs, diagnostics, output);
}

} // namespace fsim::app::application_detail
