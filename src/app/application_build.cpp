// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"

namespace fsim::app {
using namespace application_detail;

std::optional<BuiltProject> build_project(
    const project::Config& config,
    diagnostic::Engine& diagnostics) {
  auto checked = check_project(config, diagnostics);
  if (!checked) {
    return std::nullopt;
  }
  std::vector<std::filesystem::path> systemc_plugins;
  std::shared_ptr<systemc::HierarchyRegistry> systemc_hierarchy;
  std::string systemc_plugin_key;
  if (const auto request = systemc_request(config)) {
    auto compiled = systemc::compile_plugin(*request, diagnostics);
    if (!compiled.success) {
      return std::nullopt;
    }
    systemc_hierarchy =
        load_systemc_plugin(compiled.library_path, diagnostics);
    if (!systemc_hierarchy) {
      return std::nullopt;
    }
    systemc_plugin_key = compiled.cache_key;
    systemc_plugins.push_back(std::move(compiled.library_path));
  }
  select_delay_alternatives(
      checked->parsed, config.run.delay_mode);
  const auto resolution = effective_resolution(config, checked->parsed);
  if (systemc_hierarchy) {
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
    systemc_hierarchy->set_time_resolution(
        parsed_resolution->magnitude * *factor);
  }
  if (!validate_declared_time_precisions(
          checked->parsed, resolution, diagnostics)
      || !normalize_delays(
          checked->parsed, resolution, diagnostics)) {
    return std::nullopt;
  }
  const auto top = selected_top(config, checked->parsed, diagnostics);
  validate_bindings(
      config, checked->parsed, systemc_hierarchy.get(), diagnostics);
  if (diagnostics.has_error()) {
    return std::nullopt;
  }
  auto systemc_instances = construct_systemc_instances(
      top, systemc_hierarchy.get(), diagnostics);
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
  if (systemc_hierarchy) {
    systemc_provider =
        std::make_unique<ApplicationSystemCFactoryProvider>(
            *systemc_hierarchy,
            *systemc_instances,
            systemc_roots);
  }
  auto elaborated = elaboration::elaborate(
      checked->parsed,
      top,
      bindings,
      *systemc_instances,
      systemc_provider.get());
  for (const auto& input : elaborated.diagnostics) {
    diagnostics.error(input.code, input.message, span(input.span));
  }
  if (!elaborated.design || diagnostics.has_error()) {
    return std::nullopt;
  }
  if (systemc_hierarchy) {
    try {
      for (const auto& instance :
           elaborated.design->systemc_instances()) {
        for (const auto& port : instance.ports) {
          systemc_hierarchy->bind_runtime_object(
              port.native_handle, port.signal);
        }
        for (const auto& event : instance.events) {
          systemc_hierarchy->bind_runtime_object(
              event.native_handle, event.signal);
        }
        for (const auto& signal : instance.internal_signals) {
          systemc_hierarchy->bind_runtime_object(
              signal.native_handle, signal.signal);
        }
        for (const auto& export_object : instance.exports) {
          systemc_hierarchy->bind_runtime_object(
              export_object.native_handle,
              export_object.signal);
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
      systemc_hierarchy->complete_elaboration(systemc_roots);
    } catch (const std::exception& error) {
      diagnostics.error(
          "FSIM-SC-A008",
          "cannot complete SystemC elaboration: "
              + std::string{error.what()});
      return std::nullopt;
    }
  }

  auto specialization_cache_keys =
      make_specialization_cache_keys(
          config, *checked, *elaborated.design, diagnostics);
  if (!specialization_cache_keys) {
    return std::nullopt;
  }
  const auto key = make_cache_key(
      config,
      *checked,
      top,
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
    const std::string record =
        "FSIM-DESIGN-CACHE-V1\n" + top + "\n"
        + std::to_string(elaborated.design->signals().size()) + "\n"
        + std::to_string(elaborated.design->processes().size()) + "\n";
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
  return BuiltProject{
      std::move(*elaborated.design),
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
      config.base_directory};
}


} // namespace fsim::app
