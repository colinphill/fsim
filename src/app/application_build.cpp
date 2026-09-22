// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"

#include "fsim/app/design_artifact.hpp"
#include "fsim/elaboration/coverage_external_exclusions.hpp"
#include "fsim/semantic/compiled_design_linker.hpp"
#include "fsim/semantic/compiled_design_normalization.hpp"


namespace fsim::app {
using namespace application_detail;
namespace {

    constexpr std::string_view kCompiledHirCacheEnvelope
        = "FSIM-COMPILED-HIR-CACHE-V2\n";

    [[nodiscard]] std::string compiled_hir_cache_payload(
        const std::string_view key,
        const std::string_view bundle)
    {
        std::string payload;
        payload.reserve(kCompiledHirCacheEnvelope.size()
            + key.size() + 1U + bundle.size());
        payload.append(kCompiledHirCacheEnvelope);
        payload.append(key);
        payload.push_back('\n');
        payload.append(bundle);
        return payload;
    }

    [[nodiscard]] std::optional<std::string_view>
    compiled_hir_cache_bundle(
        const std::span<const std::byte> payload,
        const std::string_view expected_key,
        std::string& failure)
    {
        const auto header_size = kCompiledHirCacheEnvelope.size()
            + expected_key.size() + 1U;
        if (payload.size() < header_size) {
            failure = "compiled-HIR cache envelope is truncated";
            return std::nullopt;
        }
        const auto bytes = std::string_view {
            reinterpret_cast<const char*>(payload.data()), payload.size()
        };
        if (!bytes.starts_with(kCompiledHirCacheEnvelope)) {
            failure = "compiled-HIR cache envelope version is unsupported";
            return std::nullopt;
        }
        const auto stored_key = bytes.substr(
            kCompiledHirCacheEnvelope.size(), expected_key.size());
        if (stored_key != expected_key
            || bytes[header_size - 1U] != '\n') {
            failure = "compiled-HIR cache payload belongs to another key";
            return std::nullopt;
        }
        return bytes.substr(header_size);
    }

    [[nodiscard]] std::vector<VhdlUnitProvenance>
    vhdl_unit_provenance(const CheckedProject& checked)
    {
        const auto packages = application_detail::vhdl_package_dependencies(
            checked);
        std::vector<VhdlUnitProvenance> result;
        result.reserve(checked.vhdl_hir.units().size());
        for (const auto& unit : checked.vhdl_hir.units()) {
            if (!unit.standard_package_revision.empty()) {
                continue;
            }
            VhdlUnitProvenance item;
            item.unit = unit.id;
            item.standard = unit.standard;
            item.predefined_environment
                = unit.predefined_environment.identity;
            item.compatibility_profile = unit.compatibility_profile;
            for (const auto& dependency : packages) {
                if (dependency.standard != item.standard) {
                    continue;
                }
                const auto selected = std::ranges::any_of(
                    unit.context, [&](const auto& context) {
                        if (context.kind
                            != semantic::vhdl::ContextKind::use_clause) {
                            return false;
                        }
                        return std::ranges::any_of(
                            context.selected_names,
                            [&](const semantic::vhdl::Name& name) {
                                return name.spelling == dependency.package
                                    || name.spelling.starts_with(
                                        dependency.package + ".");
                            });
                    });
                if (selected) {
                    item.package_dependencies.push_back(dependency);
                }
            }
            result.push_back(std::move(item));
        }
        return result;
    }

    [[nodiscard]] diagnostic::SourceSpan diagnostic_span(
        const semantic::Model& model,
        const semantic::SourceSpanId source)
    {
        const auto& span = model.source_spans().at(source.value());
        const auto& file = model.source_files().at(span.file.value());
        diagnostic::SourceSpan result;
        result.path = span.logical_name.empty()
            ? file.physical_name
            : span.logical_name;
        result.begin.line = span.begin.line;
        result.begin.column = span.begin.column;
        result.begin.offset = span.begin.offset;
        result.end.line = span.end.line;
        result.end.column = span.end.column;
        result.end.offset = span.end.offset;
        return result;
    }

    [[nodiscard]] std::optional<frontend::StandardRevision>
    verilog_standard_revision(
        const semantic::Language language,
        const std::string_view standard)
    {
        using Revision = frontend::StandardRevision;
        if (language == semantic::Language::verilog) {
            if (standard == "1995") {
                return Revision::Verilog1995;
            }
            if (standard == "2001") {
                return Revision::Verilog2001;
            }
            if (standard == "2001-noconfig") {
                return Revision::Verilog2001NoConfig;
            }
            if (standard == "2005") {
                return Revision::Verilog2005;
            }
            return std::nullopt;
        }
        if (language != semantic::Language::system_verilog) {
            return std::nullopt;
        }
        if (standard == "2005") {
            return Revision::SystemVerilog2005;
        }
        if (standard == "2009") {
            return Revision::SystemVerilog2009;
        }
        if (standard == "2012") {
            return Revision::SystemVerilog2012;
        }
        if (standard == "2017") {
            return Revision::SystemVerilog2017;
        }
        if (standard == "2023") {
            return Revision::SystemVerilog2023;
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::string> canonical_cache_payload(
        const semantic::CompiledDesign& design,
        const std::span<const library::SourceNameMapping> mappings,
        diagnostic::Engine& diagnostics)
    {
        auto relocated = design;
        if (!relocate_compiled_design_sources(
                relocated, mappings, diagnostics)) {
            return std::nullopt;
        }
        return serialize_compiled_hir_bundle(relocated, diagnostics);
    }

} // namespace

std::optional<BuiltProject> build_checked_project(
    const project::Config& config,
    std::optional<CompilationWorkspace> workspace,
    const std::span<const std::filesystem::path> incremental_plugins,
    diagnostic::Engine& diagnostics)
{
    if (!workspace) {
        return std::nullopt;
    }
    auto checked = std::optional<CheckedProject> {
        release_compiled_project(std::move(*workspace))
    };
    // Moving the CheckedProject base does not move the derived parser fields.
    // Destroy the compile-local workspace before cache linking, specialization,
    // hierarchy construction, or SimIR lowering begins.
    workspace.reset();
    const auto coverage_exclusions
        = elaboration::make_coverage_external_exclusion_plan(
            config.coverage.exclusions);
    if (!coverage_exclusions.ok()) {
        diagnostics.error(
            std::string {
                elaboration::kCoverageExternalExclusionDiagnostic },
            "external coverage exclusion #"
                + std::to_string(coverage_exclusions.index + 1U)
                + " is invalid: "
                + std::string {
                    elaboration::coverage_external_exclusion_error_name(
                        coverage_exclusions.error) });
        return std::nullopt;
    }
    const auto coverage_identity = artifact::make_code_coverage_artifact_identity(
        code_coverage_enabled(config));
    if (!coverage_identity.ok()) {
        diagnostics.error(
            std::string { artifact::kCodeCoverageArtifactDiagnostic },
            "could not construct the v3 build code-coverage identity");
        return std::nullopt;
    }
    for (const auto& object : checked->objects) {
        if (object.code_coverage != coverage_identity.identity) {
            diagnostics.error(
                std::string { artifact::kCodeCoverageArtifactDiagnostic },
                ".fsimobj code-coverage configuration does not match the "
                "current v3 elaboration request");
            return std::nullopt;
        }
    }
    for (const auto& source_set : config.source_sets) {
        if (source_set.uvm_release != project::SystemVerilogUvmRelease::none
            && source_set.uvm_release
                != checked->systemverilog_uvm_provenance.release) {
            diagnostics.error(
                "FSIM-UVM-VERSION-001",
                "requested UVM release does not match the checked source/object "
                "provenance");
            return std::nullopt;
        }
    }
    std::vector<std::filesystem::path> systemc_plugins;
    std::vector<SystemCLibraryRegistry> systemc_registries;
    std::shared_ptr<systemc::HierarchyRegistry> systemc_hierarchy;
    std::string systemc_plugin_key;
    std::set<std::string> systemc_libraries;
    for (const auto& entry : systemc_requests(config)) {
        systemc_libraries.insert(entry.library);
        std::vector<std::filesystem::path> objects;
        objects.reserve(entry.request.sources.size());
        for (const auto& source : entry.request.sources) {
            systemc::IncrementalCompileRequest compile_request;
            compile_request.source = source;
            compile_request.settings = entry.request.settings;
            compile_request.working_directory = entry.request.working_directory;
            auto compiled = systemc::compile_incremental_object_cached(
                std::move(compile_request), entry.request.cache_directory,
                diagnostics);
            if (!compiled.success) {
                return std::nullopt;
            }
            objects.push_back(std::move(compiled.artifact));
        }
        systemc::IncrementalLinkRequest link_request;
        link_request.objects = objects;
        link_request.logical_library = entry.library;
        link_request.settings = entry.request.settings;
        link_request.working_directory = entry.request.working_directory;
        auto linked = systemc::link_incremental_plugin_cached(
            std::move(link_request), entry.request.cache_directory, diagnostics);
        if (!linked.success) {
            return std::nullopt;
        }
        auto plugin_metadata = systemc::load_incremental_plugin_metadata(
            linked.artifact, diagnostics);
        if (!plugin_metadata) {
            return std::nullopt;
        }
        auto registry = systemc::load_incremental_plugin(
            linked.artifact, diagnostics,
            plugin_metadata->compiler_fingerprint);
        if (!registry) {
            return std::nullopt;
        }
        systemc_plugin_key += entry.library + ":" + plugin_metadata->link_digest + ";";
        systemc_registries.push_back({ entry.library, registry });
        if (!systemc_hierarchy) {
            systemc_hierarchy = registry;
        }
        systemc_plugins.push_back(
            linked.artifact / plugin_metadata->library);
    }
    for (const auto& artifact : incremental_plugins) {
        auto plugin_metadata = systemc::load_incremental_plugin_metadata(
            artifact, diagnostics);
        if (!plugin_metadata) {
            return std::nullopt;
        }
        if (!systemc_libraries.insert(plugin_metadata->logical_library).second) {
            diagnostics.error(
                "FSIM-SC-I006",
                "duplicate SystemC logical-library plug-in '"
                    + plugin_metadata->logical_library + "'");
            return std::nullopt;
        }
        auto registry = systemc::load_incremental_plugin(artifact, diagnostics);
        if (!registry) {
            return std::nullopt;
        }
        systemc_plugin_key += plugin_metadata->logical_library + ":"
            + plugin_metadata->link_digest + ";";
        systemc_registries.push_back(
            { plugin_metadata->logical_library, registry });
        if (!systemc_hierarchy) {
            systemc_hierarchy = registry;
        }
        systemc_plugins.push_back(artifact / plugin_metadata->library);
    }
    for (const auto& mapped : checked->mapped_libraries) {
        if (mapped.systemc_plugin.empty() && mapped.systemc_sources.empty()) {
            continue;
        }
        auto plugin_path = mapped.systemc_plugin;
        std::string plugin_identity = mapped.native_fingerprint;
        if (plugin_path.empty()) {
            systemc::PluginCompileRequest request;
            request.sources = mapped.systemc_sources;
            request.logical_library = mapped.library;
            request.settings = config.systemc;
            request.working_directory = config.base_directory;
            request.cache_directory = config.build.cache_path;
            auto compiled = systemc::compile_plugin(request, diagnostics);
            if (!compiled.success) {
                return std::nullopt;
            }
            plugin_path = std::move(compiled.library_path);
            plugin_identity = "portable-source:" + compiled.cache_key;
        }
        auto registry = load_systemc_plugin(plugin_path, diagnostics);
        if (!registry) {
            return std::nullopt;
        }
        systemc_plugin_key += "mapped:" + mapped.library + ":"
            + plugin_identity + ";";
        systemc_registries.push_back({ mapped.library, registry });
        if (!systemc_hierarchy) {
            systemc_hierarchy = registry;
        }
        systemc_plugins.push_back(std::move(plugin_path));
    }
    auto resolution = effective_resolution(
        config, static_cast<const semantic::CompiledDesign&>(*checked));
    const auto tops = selected_tops(config, *checked, diagnostics);
    validate_bindings(
        config, *checked, systemc_registries, diagnostics);
    if (diagnostics.has_error()) {
        return std::nullopt;
    }
    if (config.run.trace_file && config.run.trace_enabled) {
        const auto object_phase = !checked->objects.empty();
        auto request = trace_control_request(config.run,
            object_phase ? TraceControlSurface::NonProjectElaborate
                         : TraceControlSurface::ProjectCli,
            object_phase ? TraceControlPhase::Elaborate
                         : TraceControlPhase::Simulate);
        auto control = apply_trace_control(std::move(request));
        if (!control.ok()) {
            for (const auto& diagnostic : control.diagnostics) {
                application_detail::import_diagnostic(
                    diagnostics, diagnostic);
            }
            return std::nullopt;
        }
        auto snapshot = make_trace_archive_snapshot(
            *control.application, config.base_directory);
        if (checked->trace_archive
            && !trace_archive_profiles_compatible(
                *checked->trace_archive, snapshot)) {
            diagnostics.error(
                "FSIM-TRACE-ARCHIVE-003",
                "current trace request conflicts with archived object or "
                "library profile");
            return std::nullopt;
        }
        checked->trace_archive = std::make_shared<const TraceArchiveSnapshot>(
            std::move(snapshot));
    }
    const auto key = make_cache_key(
        config,
        *checked,
        tops,
        resolution,
        systemc_plugin_key,
        diagnostics);
    if (key.empty()) {
        return std::nullopt;
    }

    const auto source_mappings = compiled_cache_source_mappings(
        *checked, config.base_directory, diagnostics);
    if (!source_mappings) {
        return std::nullopt;
    }
    std::vector<library::SourceNameMapping> consumer_mappings;
    consumer_mappings.reserve(source_mappings->size());
    for (const auto& mapping : *source_mappings) {
        consumer_mappings.push_back(
            { mapping.logical_name, mapping.producer_name });
    }

    compiler::ObjectCache cache(config.build.cache_path);
    std::error_code cache_error;
    const auto existing = cache.load(key, cache_error);
    bool hit = false;
    std::string cache_failure;
    if (existing) {
        diagnostic::Engine cache_diagnostics;
        const auto bundle = compiled_hir_cache_bundle(
            *existing, key, cache_failure);
        auto cached = bundle
            ? deserialize_compiled_hir_bundle(
                  *bundle, cache.path_for(key).string(), cache_diagnostics)
            : std::nullopt;
        if (cached
            && !relocate_compiled_design_sources(
                *cached, consumer_mappings, cache_diagnostics)) {
            cached.reset();
        }
        if (cached
            && effective_resolution(config, *cached) != resolution) {
            cache_failure = "compiled-HIR cache time resolution is inconsistent";
            cached.reset();
        }
        if (cached) {
            // The cache envelope binds the decoded bundle to the complete
            // checked-input/configuration key used for root and binding
            // selection.
            std::vector<semantic::CompiledDesign> inputs;
            inputs.push_back(std::move(*cached));
            auto linked = install_linked_compiled_design(
                *checked, std::move(inputs));
            hit = linked.ok();
            cache_failure = std::move(linked.error);
        }
        if (!hit && cache_failure.empty()
            && !cache_diagnostics.empty()) {
            cache_failure = cache_diagnostics.diagnostics().front().message;
        }
        if (!hit && cache_failure.empty()) {
            cache_failure = "compiled-HIR cache payload is invalid";
        }
    } else if (cache_error != std::errc::no_such_file_or_directory) {
        cache_failure = cache_error.message();
    }
    if (!cache_failure.empty()) {
        std::error_code erase_error;
        (void)cache.erase(key, erase_error);
        diagnostics.warning(
            "FSIM-CACHE-0002",
            "discarded an unreadable or incompatible cache entry: "
                + cache_failure);
    }
    if (!hit) {
        diagnostic::Engine canonical_diagnostics;
        auto canonical_fresh = canonical_cache_payload(
            static_cast<const semantic::CompiledDesign&>(*checked),
            *source_mappings, canonical_diagnostics);
        if (!canonical_fresh) {
            const auto reason = canonical_diagnostics.empty()
                ? std::string { "compiled-HIR serialization failed" }
                : canonical_diagnostics.diagnostics().front().message;
            diagnostics.error(
                "FSIM-CACHE-0003",
                "cannot prepare cache payload: " + reason);
            return std::nullopt;
        }
        const auto cache_payload = compiled_hir_cache_payload(
            key, *canonical_fresh);
        const auto payload_bytes = std::as_bytes(
            std::span<const char> {
                cache_payload.data(), cache_payload.size() });
        if (compiler::object_cache_payload_fits(payload_bytes.size())) {
            cache_error.clear();
            if (!cache.store(key, payload_bytes, cache_error)) {
                diagnostics.error(
                    "FSIM-CACHE-0003",
                    "cannot populate cache: " + cache_error.message());
                return std::nullopt;
            }
        }
    }

    resolution = effective_resolution(
        config, static_cast<const semantic::CompiledDesign&>(*checked));
    if (!validate_declared_time_precisions(
            static_cast<const semantic::CompiledDesign&>(*checked),
            resolution,
            diagnostics)) {
        return std::nullopt;
    }
    if (!systemc_registries.empty()) {
        const auto parsed_resolution = magnitude_and_unit(resolution);
        const auto factor = parsed_resolution
            ? unit_femtoseconds(parsed_resolution->unit)
            : std::nullopt;
        if (!parsed_resolution || !factor
            || parsed_resolution->magnitude == 0
            || parsed_resolution->magnitude
                > std::numeric_limits<std::uint64_t>::max() / *factor) {
            diagnostics.error(
                "FSIM-SC-A007",
                "cannot configure SystemC with project time resolution '"
                    + resolution + "'");
            return std::nullopt;
        }
        for (const auto& entry : systemc_registries) {
            entry.registry->set_time_resolution(
                parsed_resolution->magnitude * *factor);
        }
    }

    auto vhdl_provenance = vhdl_unit_provenance(*checked);
    std::map<std::string, frontend::StandardRevision, std::less<>>
        verilog_unit_revisions;
    std::map<std::string, std::string, std::less<>>
        verilog_unit_compatibility_profiles;
    for (const auto& unit : checked->systemverilog_hir.units()) {
        const auto& semantic_unit = checked->semantics.units().at(
            unit.id.value());
        const auto revision = verilog_standard_revision(
            semantic_unit.language, unit.standard);
        if (!revision) {
            diagnostics.error(
                "FSIM-ELAB-HIR-001",
                "compiled-HIR unit '" + unit.name
                    + "' has an invalid Verilog/SystemVerilog standard '"
                    + unit.standard + "'");
            return std::nullopt;
        }
        const auto identity = unit.library + "::" + unit.name;
        verilog_unit_revisions.insert_or_assign(identity, *revision);
        verilog_unit_compatibility_profiles.insert_or_assign(
            identity, unit.compatibility_profile);
    }
    auto systemverilog_coverage = make_systemverilog_coverage_state(
        checked->systemverilog_hir, checked->semantics);
    select_delay_alternatives(*checked, config.run.delay_mode);
    if (!normalize_delays(
            *checked, resolution, diagnostics)) {
        return std::nullopt;
    }
    checked->refresh_lookup_indexes();
    auto systemc_instances = construct_systemc_instances(
        tops, systemc_registries, diagnostics);
    if (!systemc_instances) {
        return std::nullopt;
    }
    std::vector<std::uint64_t> systemc_roots;
    systemc_roots.reserve(systemc_instances->size());
    for (const auto& instance : *systemc_instances) {
        systemc_roots.push_back(instance.handle);
    }
    std::vector<elaboration::Binding> bindings;
    bindings.reserve(config.bindings.size());
    for (const auto& binding : config.bindings) {
        bindings.push_back(
            { binding.instance, binding.target, binding.resolver });
    }
    std::unique_ptr<ApplicationSystemCFactoryProvider>
        systemc_provider;
    if (!systemc_registries.empty()) {
        systemc_provider = std::make_unique<ApplicationSystemCFactoryProvider>(
            systemc_registries,
            *systemc_instances,
            systemc_roots);
    }
    std::vector<elaboration::Root> elaboration_roots;
    elaboration_roots.reserve(tops.size());
    for (const auto& top : tops) {
        elaboration_roots.push_back({ top.target, top.alias });
    }
    auto elaborated = elaboration::elaborate(
        *checked,
        elaboration_roots,
        bindings,
        *systemc_instances,
        systemc_provider.get(),
        config.elaboration.search_libraries);
    for (const auto& input : elaborated.messages) {
        application_detail::import_diagnostic(diagnostics, input);
    }
    for (const auto& input : elaborated.diagnostics) {
        const auto source = intern_semantic_span(
            checked->semantics, input.span);
        diagnostics.error(
            input.code,
            input.message,
            diagnostic_span(checked->semantics, source));
    }
    if (!elaborated.design || diagnostics.has_error()) {
        return std::nullopt;
    }
    const auto generated_class_is_selected =
        [&](const semantic::sv::ClassSpecialization& specialization) {
          const auto declaration = std::ranges::find_if(
              checked->systemverilog_hir.classes(),
              [&](const auto& candidate) {
                return semantic::sv::class_declaration_identity(candidate)
                    == specialization.declaration_identity;
              });
          if (declaration == checked->systemverilog_hir.classes().end()
              || !declaration->generate_owner) {
              return true;
          }
          return std::ranges::any_of(
              elaborated.selected_systemverilog_classes,
              [&](const auto& selection) {
                return selection.declaration_identity
                        == specialization.declaration_identity
                    && selection.generate_owner
                        == *declaration->generate_owner
                    && selection.declaration_scope == declaration->scope
                    && selection.origin == declaration->origin;
              });
        };
    std::erase_if(
        checked->compiled_systemverilog_class_specializations,
        [&](const auto& specialization) {
          return !generated_class_is_selected(specialization);
        });
    for (const auto& selection :
        elaborated.selected_systemverilog_classes) {
        if (std::ranges::none_of(
                checked->compiled_systemverilog_class_specializations,
                [&](const auto& specialization) {
                  return specialization.declaration_identity
                      == selection.declaration_identity;
                })) {
            diagnostics.error(
                "FSIM-ELAB-GEN-015",
                "selected generated class '"
                    + selection.declaration_identity
                    + "' has no compiled-HIR specialization");
        }
    }
    if (diagnostics.has_error()) {
        return std::nullopt;
    }
    if (!systemc_registries.empty()) {
        try {
            const auto bind_object =
                [&](const std::uint64_t handle, const std::uint32_t signal) {
                    const auto registry = systemc_provider->registry_for_handle(handle);
                    if (!registry) {
                        throw std::logic_error {
                            "SystemC object has no owning logical-library plug-in"
                        };
                    }
                    registry->bind_runtime_object(handle, signal);
                };
            for (const auto& instance :
                elaborated.design->systemc_instances()) {
                for (const auto& port : instance.ports) {
                    bind_object(port.native_handle, port.signal);
                }
                for (const auto& event : instance.events) {
                    bind_object(event.native_handle, event.signal);
                }
                for (const auto& signal : instance.internal_signals) {
                    bind_object(signal.native_handle, signal.signal);
                }
                for (const auto& export_object : instance.exports) {
                    bind_object(
                        export_object.native_handle, export_object.signal);
                }
            }
        } catch (const std::exception& error) {
            diagnostics.error(
                "FSIM-SC-A006",
                "cannot bind SystemC objects to the common runtime: "
                    + std::string { error.what() });
            return std::nullopt;
        }
        try {
            for (const auto& entry : systemc_registries) {
                std::vector<std::uint64_t> roots;
                std::ranges::copy_if(
                    systemc_roots,
                    std::back_inserter(roots),
                    [&](const auto handle) {
                        return entry.registry->owns_handle(handle);
                    });
                entry.registry->complete_elaboration(roots);
            }
        } catch (const std::exception& error) {
            diagnostics.error(
                "FSIM-SC-A008",
                "cannot complete SystemC elaboration: "
                    + std::string { error.what() });
            return std::nullopt;
        }
    }

    auto design_ir = build_design_ir(*checked, *elaborated.design);
    if (!design_ir.valid(checked->semantics)) {
        throw std::logic_error { "constructed an internally invalid DesignIR" };
    }
    if (!valid_runtime_projection(design_ir, *elaborated.design)) {
        throw std::logic_error { "constructed an incomplete DesignIR projection" };
    }
    auto specialization_cache_keys = make_specialization_cache_keys(
        config,
        *checked,
        design_ir,
        systemc_plugin_key,
        diagnostics);
    if (!specialization_cache_keys) {
        return std::nullopt;
    }
    const auto selected_seed = config.project.random_seed
        ? entropy_seed()
        : config.project.seed;
    std::vector<std::shared_ptr<systemc::HierarchyRegistry>>
        systemc_hierarchies;
    systemc_hierarchies.reserve(systemc_registries.size());
    for (const auto& entry : systemc_registries) {
        systemc_hierarchies.push_back(entry.registry);
    }
    std::vector<MappedLibraryProvenance> mapped_libraries;
    mapped_libraries.reserve(checked->mapped_libraries.size());
    for (const auto& mapped : checked->mapped_libraries) {
        mapped_libraries.push_back({ mapped.library, mapped.metadata_digest, mapped.unit_checksums,
            mapped.native_accepted, mapped.native_kind,
            mapped.native_fingerprint });
    }
    auto result = BuiltProject {
        std::move(*elaborated.design),
        std::move(design_ir),
        std::move(checked->semantics),
        std::move(checked->systemverilog_hir),
        std::move(checked->vhdl_hir),
        key,
        resolution,
        config.build.cache_path,
        config.build.optimization,
        std::move(*specialization_cache_keys),
        std::move(systemc_plugins),
        std::move(systemc_hierarchy),
        std::move(systemc_roots),
        selected_seed,
        config.project.random_seed,
        hit,
        config.base_directory,
        std::move(systemc_hierarchies),
        std::move(mapped_libraries),
        std::move(checked->objects),
        std::move(checked->systemverilog_uvm_provenance), { },
        std::move(checked->compiled_systemverilog_class_specializations),
        std::move(systemverilog_coverage), std::nullopt, { },
        std::move(verilog_unit_revisions),
        std::move(verilog_unit_compatibility_profiles), { }, { }
    };
    result.vhdl_unit_provenance = std::move(vhdl_provenance);
    result.trace_archive = std::move(checked->trace_archive);
    result.code_coverage_enabled = code_coverage_enabled(config);
    return result;
}

std::optional<BuiltProject> build_project(
    const project::Config& config,
    diagnostic::Engine& diagnostics)
{
    return build_checked_project(
        config, check_project_workspace(config, diagnostics), { }, diagnostics);
}

std::optional<BuiltProject> build_objects(
    const project::Config& config,
    const std::span<const std::filesystem::path> objects,
    diagnostic::Engine& diagnostics)
{
    return build_objects(config, objects, { }, diagnostics);
}

std::optional<BuiltProject> build_objects(
    const project::Config& config,
    const std::span<const std::filesystem::path> objects,
    const std::span<const std::filesystem::path> systemc_plugins,
    diagnostic::Engine& diagnostics)
{
    std::optional<CompilationWorkspace> checked;
    if (objects.empty()) {
        CompilationWorkspace empty;
        inject_vhdl_standard_libraries(empty, diagnostics);
        empty.semantics = build_semantic_model(
            empty.parsed, empty.hdl_sources, empty.systemc_sources,
            empty.standard_sources);
        empty.vhdl_hir = build_vhdl_hir(empty.parsed, empty.semantics);
        empty.systemverilog_hir = build_systemverilog_hir(empty.parsed, empty.semantics);
        checked = std::move(empty);
    } else {
        checked = load_object_workspace(objects, diagnostics);
    }
    return build_checked_project(
        config, std::move(checked), systemc_plugins, diagnostics);
}

} // namespace fsim::app
