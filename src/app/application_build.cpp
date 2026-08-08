// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"


namespace fsim::app {
using namespace application_detail;
namespace {

[[nodiscard]] diagnostic::SourceSpan diagnostic_span(
    const semantic::Model& model,
    const semantic::SourceSpanId source) {
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

} // namespace

std::optional<BuiltProject> build_checked_project(
    const project::Config& config,
    std::optional<CheckedProject> checked,
    const std::span<const std::filesystem::path> incremental_plugins,
    diagnostic::Engine& diagnostics) {
  if (!checked) {
    return std::nullopt;
  }
  // Lowering remains a temporary compatibility projection while the owning
  // semantic HIR is the durable boundary. Moving it out proves that no build,
  // cache, runtime, debugger, trace, or API result can retain an address into
  // CheckedProject's parser workspace.
  auto lowering_adapter = std::move(checked->parsed);
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
      compile_request.scratch_directory =
          entry.request.cache_directory / "systemc-phase-scratch";
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
    link_request.scratch_directory =
        entry.request.cache_directory / "systemc-phase-scratch";
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
        linked.artifact, diagnostics);
    if (!registry) {
      return std::nullopt;
    }
    systemc_plugin_key +=
        entry.library + ":" + plugin_metadata->link_digest + ";";
    systemc_registries.push_back({entry.library, registry});
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
        {plugin_metadata->logical_library, registry});
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
    systemc_registries.push_back({mapped.library, registry});
    if (!systemc_hierarchy) {
      systemc_hierarchy = registry;
    }
    systemc_plugins.push_back(std::move(plugin_path));
  }
  select_delay_alternatives(
      lowering_adapter, config.run.delay_mode);
  const auto resolution = effective_resolution(config, lowering_adapter);
  if (!systemc_registries.empty()) {
    const auto parsed_resolution = magnitude_and_unit(resolution);
    const auto factor =
        parsed_resolution
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
  if (!validate_declared_time_precisions(
          lowering_adapter, resolution, diagnostics)
      || !normalize_delays(
          lowering_adapter, resolution, diagnostics)) {
    return std::nullopt;
  }
  // Reproject after delay-mode selection and time normalization so the HIR
  // consumed by every durable downstream boundary is the exact lowering
  // input, not the pre-normalization parser snapshot created by `check`.
  checked->semantics = build_semantic_model(
      lowering_adapter,
      checked->hdl_sources,
      checked->systemc_sources,
      checked->standard_sources);
  checked->vhdl_hir = build_vhdl_hir(
      lowering_adapter, checked->semantics);
  checked->systemverilog_hir = build_systemverilog_hir(
      lowering_adapter,
      checked->semantics,
      checked->systemverilog_class_specializations);
  if (!checked->semantics.valid()) {
    diagnostics.error(
        "FSIM-SEM-0001",
        "build normalization produced an invalid owning semantic projection");
    return std::nullopt;
  }
  const auto tops = selected_tops(config, lowering_adapter, diagnostics);
  validate_bindings(
      config, lowering_adapter, systemc_registries, diagnostics);
  if (diagnostics.has_error()) {
    return std::nullopt;
  }
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
        {binding.instance, binding.target, binding.resolver});
  }
  std::unique_ptr<ApplicationSystemCFactoryProvider>
      systemc_provider;
  if (!systemc_registries.empty()) {
    systemc_provider =
        std::make_unique<ApplicationSystemCFactoryProvider>(
            systemc_registries,
            *systemc_instances,
            systemc_roots);
  }
  std::vector<elaboration::Root> elaboration_roots;
  elaboration_roots.reserve(tops.size());
  for (const auto& top : tops) {
    elaboration_roots.push_back({top.target, top.alias});
  }
  auto elaborated = elaboration::elaborate(
      lowering_adapter,
      elaboration_roots,
      bindings,
      *systemc_instances,
      systemc_provider.get(),
      config.elaboration.search_libraries);
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
  auto systemverilog_coverage =
      frontend::capture_systemverilog_coverage_state(lowering_adapter);
  lowering_adapter = {};
  if (!systemc_registries.empty()) {
    try {
      const auto bind_object =
          [&](const std::uint64_t handle, const std::uint32_t signal) {
            const auto registry =
                systemc_provider->registry_for_handle(handle);
            if (!registry) {
              throw std::logic_error{
                  "SystemC object has no owning logical-library plug-in"};
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
              + std::string{error.what()});
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
              + std::string{error.what()});
      return std::nullopt;
    }
  }

  auto design_ir = build_design_ir(*checked, *elaborated.design);
  if (!design_ir.valid(checked->semantics)) {
    throw std::logic_error{"constructed an internally invalid DesignIR"};
  }
  if (!valid_runtime_projection(design_ir, *elaborated.design)) {
    throw std::logic_error{"constructed an incomplete DesignIR projection"};
  }
  auto specialization_cache_keys =
      make_specialization_cache_keys(
          config,
          *checked,
          design_ir,
          systemc_plugin_key,
          diagnostics);
  if (!specialization_cache_keys) {
    return std::nullopt;
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
  compiler::ObjectCache cache(config.build.cache_path);
  std::error_code cache_error;
  const auto existing = cache.load(key, cache_error);
  const bool hit = existing.has_value();
  if (!hit) {
    if (cache_error != std::errc::no_such_file_or_directory) {
      std::error_code erase_error;
      (void)cache.erase(key, erase_error);
      diagnostics.warning(
          "FSIM-CACHE-0002",
          "discarded an unreadable or incompatible cache entry: "
              + cache_error.message());
    }
    std::string record = "FSIM-DESIGN-CACHE-V2\n";
    for (const auto& top : tops) {
      record += top.alias + "=" + top.target + "\n";
    }
    record += std::to_string(std::ranges::count_if(
              design_ir.objects(), [](const auto& object) {
                return object.kind == semantic::design::ObjectKind::signal
                    && !object.parent_object;
              })) + "\n"
        + std::to_string(design_ir.processes().size()) + "\n";
    const auto bytes = std::as_bytes(
        std::span<const char>{record.data(), record.size()});
    cache_error.clear();
    if (!cache.store(key, bytes, cache_error)) {
      diagnostics.error(
          "FSIM-CACHE-0003",
          "cannot populate cache: " + cache_error.message());
      return std::nullopt;
    }
  }
  const auto selected_seed =
      config.project.random_seed
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
    mapped_libraries.push_back({
        mapped.library, mapped.metadata_digest, mapped.unit_checksums,
        mapped.native_accepted, mapped.native_kind,
        mapped.native_fingerprint});
  }
  return BuiltProject{
      std::move(*elaborated.design),
      std::move(design_ir),
      std::move(checked->semantics),
      std::move(checked->systemverilog_hir),
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
      std::move(checked->objects), {},
      std::move(checked->systemverilog_class_specializations),
      std::move(systemverilog_coverage), std::nullopt};
}

std::optional<BuiltProject> build_project(
    const project::Config& config,
    diagnostic::Engine& diagnostics) {
  return build_checked_project(
      config, check_project(config, diagnostics), {}, diagnostics);
}

std::optional<BuiltProject> build_objects(
    const project::Config& config,
    const std::span<const std::filesystem::path> objects,
    diagnostic::Engine& diagnostics) {
  return build_objects(config, objects, {}, diagnostics);
}

std::optional<BuiltProject> build_objects(
    const project::Config& config,
    const std::span<const std::filesystem::path> objects,
    const std::span<const std::filesystem::path> systemc_plugins,
    diagnostic::Engine& diagnostics) {
  std::optional<CheckedProject> checked;
  if (objects.empty()) {
    CheckedProject empty;
    inject_vhdl_standard_libraries(empty, diagnostics);
    empty.semantics = build_semantic_model(
        empty.parsed, empty.hdl_sources, empty.systemc_sources,
        empty.standard_sources);
    empty.vhdl_hir = build_vhdl_hir(empty.parsed, empty.semantics);
    empty.systemverilog_hir =
        build_systemverilog_hir(empty.parsed, empty.semantics);
    checked = std::move(empty);
  } else {
    checked = load_objects(objects, diagnostics);
  }
  return build_checked_project(
      config, std::move(checked), systemc_plugins, diagnostics);
}


} // namespace fsim::app
