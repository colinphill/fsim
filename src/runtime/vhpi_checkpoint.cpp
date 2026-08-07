// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/vhpi_checkpoint.hpp"

#include <algorithm>
#include <climits>
#include <unordered_set>
#include <utility>

namespace fsim::runtime {

namespace {

constexpr std::size_t maximum_identity_size = 4096;
constexpr std::size_t maximum_full_name_size = 1U << 20U;
constexpr std::size_t maximum_provenance_entries = 1024;
constexpr std::size_t maximum_checkpoint_objects = 1U << 20U;

bool valid_identity(const std::string& value) noexcept {
  return !value.empty() && value.size() <= maximum_identity_size
      && value.find('\0') == std::string::npos;
}

bool valid_artifact_path(const std::string& value) noexcept {
  return !value.empty() && value.size() <= maximum_full_name_size
      && value.find('\0') == std::string::npos;
}

bool valid_compatibility(
    const VhdlVhpiCheckpointCompatibility& compatibility) {
  if (!valid_identity(compatibility.content_fingerprint)
      || !valid_identity(compatibility.cache_fingerprint)
      || compatibility.mapped_libraries.size()
          > maximum_provenance_entries
      || compatibility.plugins.size() > maximum_provenance_entries) {
    return false;
  }
  std::unordered_set<std::string> libraries;
  std::unordered_set<std::string> plugins;
  try {
    libraries.reserve(compatibility.mapped_libraries.size());
    plugins.reserve(compatibility.plugins.size());
    for (const auto& library : compatibility.mapped_libraries) {
      if (!valid_identity(library.identity)
          || !valid_artifact_path(library.artifact)
          || !valid_identity(library.content_fingerprint)
          || !libraries.insert(library.identity).second) {
        return false;
      }
    }
    for (const auto& plugin : compatibility.plugins) {
      if (!valid_identity(plugin.name)
          || !valid_artifact_path(plugin.artifact)
          || !valid_identity(plugin.mapped_library)
          || !valid_identity(plugin.content_fingerprint)
          || !valid_identity(plugin.host_fingerprint)
          || !libraries.contains(plugin.mapped_library)
          || !plugins.insert(plugin.name).second) {
        return false;
      }
    }
  } catch (...) {
    return false;
  }
  return true;
}

const VhdlVhpiMappedLibraryProvenance* find_library(
    const VhdlVhpiCheckpointCompatibility& compatibility,
    const std::string& identity) noexcept {
  const auto found = std::ranges::find(
      compatibility.mapped_libraries,
      identity,
      &VhdlVhpiMappedLibraryProvenance::identity);
  return found == compatibility.mapped_libraries.end()
      ? nullptr
      : &*found;
}

const VhdlVhpiPluginProvenance* find_plugin(
    const VhdlVhpiCheckpointCompatibility& compatibility,
    const std::string& name) noexcept {
  const auto found = std::ranges::find(
      compatibility.plugins, name, &VhdlVhpiPluginProvenance::name);
  return found == compatibility.plugins.end() ? nullptr : &*found;
}

VhdlVhpiCheckpointError compatibility_error(
    const VhdlVhpiCheckpointCompatibility& saved,
    const VhdlVhpiCheckpointCompatibility& expected) noexcept {
  if (saved.content_fingerprint != expected.content_fingerprint) {
    return VhdlVhpiCheckpointError::ContentMismatch;
  }
  if (saved.cache_fingerprint != expected.cache_fingerprint) {
    return VhdlVhpiCheckpointError::CacheMismatch;
  }
  if (saved.mapped_libraries.size()
      != expected.mapped_libraries.size()) {
    return VhdlVhpiCheckpointError::MappedLibraryMismatch;
  }
  for (const auto& library : saved.mapped_libraries) {
    const auto* match = find_library(expected, library.identity);
    if (match == nullptr
        || match->content_fingerprint != library.content_fingerprint) {
      return VhdlVhpiCheckpointError::MappedLibraryMismatch;
    }
  }
  if (saved.plugins.size() != expected.plugins.size()) {
    return VhdlVhpiCheckpointError::PluginMismatch;
  }
  for (const auto& plugin : saved.plugins) {
    const auto* match = find_plugin(expected, plugin.name);
    if (match == nullptr
        || match->mapped_library != plugin.mapped_library
        || match->content_fingerprint != plugin.content_fingerprint
        || match->host_fingerprint != plugin.host_fingerprint) {
      return VhdlVhpiCheckpointError::PluginMismatch;
    }
  }
  return VhdlVhpiCheckpointError::None;
}

bool valid_objects(
    const std::vector<VhdlVhpiCheckpointObject>& objects) {
  if (objects.empty() || objects.size() > maximum_checkpoint_objects) {
    return false;
  }
  std::unordered_set<fsim_vhpi_handle_v1> handles;
  std::unordered_set<std::string> names;
  try {
    handles.reserve(objects.size());
    names.reserve(objects.size());
    for (const auto& object : objects) {
      if (object.source == 0U || object.full_name.empty()
          || object.full_name.size() > maximum_full_name_size
          || object.full_name.find('\0') != std::string::npos
          || static_cast<std::uint32_t>(object.kind)
              > static_cast<std::uint32_t>(VhdlVhpiObjectKind::Subtype)
          || !handles.insert(object.source).second
          || !names.insert(object.full_name).second) {
        return false;
      }
    }
  } catch (...) {
    return false;
  }
  return true;
}

void add_invalidation(
    VhdlVhpiCheckpointRestoreResult& result,
    const VhdlVhpiNativeStateKind kind,
    const std::uint64_t count,
    std::string reason) {
  if (count != 0U) {
    result.invalidations.push_back({kind, count, std::move(reason)});
  }
}

void add_relocations(
    VhdlVhpiCheckpointRestoreResult& result,
    const VhdlVhpiCheckpointCompatibility& saved,
    const VhdlVhpiCheckpointCompatibility& expected) {
  for (const auto& library : saved.mapped_libraries) {
    const auto* match = find_library(expected, library.identity);
    if (match != nullptr && match->artifact != library.artifact) {
      result.relocations.push_back({
          "library:" + library.identity,
          library.artifact,
          match->artifact,
      });
    }
  }
  for (const auto& plugin : saved.plugins) {
    const auto* match = find_plugin(expected, plugin.name);
    if (match != nullptr && match->artifact != plugin.artifact) {
      result.relocations.push_back({
          "plugin:" + plugin.name,
          plugin.artifact,
          match->artifact,
      });
    }
  }
}

void add_invalidations(
    VhdlVhpiCheckpointRestoreResult& result,
    const VhdlVhpiNativeStateSummary& state) {
  add_invalidation(result, VhdlVhpiNativeStateKind::CallbackClosure,
      state.callback_closures,
      "native callback closures require plug-in re-registration");
  add_invalidation(result, VhdlVhpiNativeStateKind::CallbackUserData,
      state.callback_user_data,
      "pointer-valued callback user data is process-local");
  add_invalidation(result, VhdlVhpiNativeStateKind::ForeignSubprogram,
      state.foreign_subprograms,
      "native foreign subprograms require plug-in re-registration");
  add_invalidation(result, VhdlVhpiNativeStateKind::ForeignModel,
      state.foreign_models,
      "native foreign models require plug-in re-registration");
  add_invalidation(result, VhdlVhpiNativeStateKind::ForeignUserData,
      state.foreign_user_data,
      "pointer-valued foreign user data is process-local");
  add_invalidation(result, VhdlVhpiNativeStateKind::ActiveForeignCall,
      state.active_foreign_calls,
      "active foreign calls and transient values cannot cross artifacts");
  add_invalidation(result, VhdlVhpiNativeStateKind::OpenFile,
      state.open_files, "native file descriptors cannot cross artifacts");
  add_invalidation(result, VhdlVhpiNativeStateKind::ProtectedLease,
      state.protected_leases,
      "native protected-object leases cannot cross artifacts");
  add_invalidation(result, VhdlVhpiNativeStateKind::ScheduledTransaction,
      state.scheduled_transactions,
      "native scheduler closures require reconstruction");
  add_invalidation(result, VhdlVhpiNativeStateKind::IoSinkContext,
      state.io_sink_contexts,
      "native output sink contexts require re-registration");
  add_invalidation(result, VhdlVhpiNativeStateKind::PluginContext,
      state.plugin_contexts,
      "dynamic-library contexts require verified plug-in reload");
}

}  // namespace

VhdlVhpiPluginProvenance make_vhdl_vhpi_plugin_provenance(
    const VhdlVhpiLoadedPlugin& plugin,
    std::string mapped_library,
    std::string content_fingerprint,
    std::string host_fingerprint) {
  return {
      plugin.name(),
      plugin.path().generic_string(),
      std::move(mapped_library),
      std::move(content_fingerprint),
      std::move(host_fingerprint),
  };
}

VhdlVhpiCheckpointCaptureResult capture_vhdl_vhpi_checkpoint(
    const VhdlVhpiObjectRegistry& objects,
    const std::span<const fsim_vhpi_handle_v1> exported_objects,
    VhdlVhpiCheckpointCompatibility compatibility,
    VhdlVhpiNativeStateSummary native_state) {
  VhdlVhpiCheckpointCaptureResult result;
  if (!objects.valid()) {
    result.error = VhdlVhpiCheckpointError::InvalidSimulation;
    return result;
  }
  if (!valid_compatibility(compatibility)
      || exported_objects.empty()
      || exported_objects.size() > maximum_checkpoint_objects) {
    result.error = VhdlVhpiCheckpointError::InvalidArtifact;
    return result;
  }
  try {
    result.artifact.objects.reserve(exported_objects.size());
    for (const auto handle : exported_objects) {
      const auto metadata = objects.lookup_object(handle);
      if (!metadata || metadata.value.full_name.empty()) {
        result.artifact.objects.clear();
        result.error = VhdlVhpiCheckpointError::ObjectMismatch;
        return result;
      }
      result.artifact.objects.push_back({
          handle, metadata.value.kind, metadata.value.full_name});
    }
  } catch (...) {
    result.artifact.objects.clear();
    result.error = VhdlVhpiCheckpointError::ResourceLimit;
    return result;
  }
  if (!valid_objects(result.artifact.objects)) {
    result.artifact.objects.clear();
    result.error = VhdlVhpiCheckpointError::InvalidArtifact;
    return result;
  }
  native_state.plugin_contexts = compatibility.plugins.size();
  result.artifact.source_simulation_identity =
      objects.simulation_identity();
  result.artifact.compatibility = std::move(compatibility);
  result.artifact.native_state = native_state;
  return result;
}

VhdlVhpiCheckpointRestoreResult restore_vhdl_vhpi_checkpoint(
    const VhdlVhpiCheckpointArtifact& artifact,
    const VhdlVhpiCheckpointFlow flow,
    const VhdlVhpiObjectRegistry& objects,
    const VhdlVhpiCheckpointCompatibility& expected,
    const VhdlVhpiNativeStateSummary& current_native_state) {
  VhdlVhpiCheckpointRestoreResult result;
  if (!objects.valid()) {
    result.error = VhdlVhpiCheckpointError::InvalidSimulation;
    return result;
  }
  if (artifact.schema != vhdl_vhpi_checkpoint_schema) {
    result.error = VhdlVhpiCheckpointError::SchemaMismatch;
    return result;
  }
  if (artifact.host_abi != FSIM_VHPI_HOST_ABI_VERSION
      || artifact.host_size != sizeof(fsim_vhpi_host_v1)) {
    result.error = VhdlVhpiCheckpointError::HostAbiMismatch;
    return result;
  }
  if (artifact.plugin_abi != FSIM_VHPI_PLUGIN_ABI_VERSION
      || artifact.plugin_size != sizeof(fsim_vhpi_plugin_v1)) {
    result.error = VhdlVhpiCheckpointError::PluginAbiMismatch;
    return result;
  }
  if (artifact.pointer_bits != sizeof(void*) * CHAR_BIT) {
    result.error = VhdlVhpiCheckpointError::PointerWidthMismatch;
    return result;
  }
  if (artifact.source_simulation_identity == 0U
      || !valid_compatibility(artifact.compatibility)
      || !valid_compatibility(expected)
      || !valid_objects(artifact.objects)) {
    result.error = VhdlVhpiCheckpointError::InvalidArtifact;
    return result;
  }
  result.error = compatibility_error(artifact.compatibility, expected);
  if (result.error != VhdlVhpiCheckpointError::None) {
    return result;
  }
  if (flow == VhdlVhpiCheckpointFlow::InProcessRestart) {
    if (artifact.source_simulation_identity
            != objects.simulation_identity()) {
      result.error = VhdlVhpiCheckpointError::InvalidSimulation;
      return result;
    }
    if (artifact.native_state != current_native_state) {
      result.error = VhdlVhpiCheckpointError::NativeStateMismatch;
      return result;
    }
  } else if (flow == VhdlVhpiCheckpointFlow::PortableArtifact) {
    if (current_native_state != VhdlVhpiNativeStateSummary{}) {
      result.error = VhdlVhpiCheckpointError::NativeStateMismatch;
      return result;
    }
  } else {
    result.error = VhdlVhpiCheckpointError::InvalidArtifact;
    return result;
  }

  try {
    result.handles.reserve(artifact.objects.size());
    for (const auto& saved : artifact.objects) {
      VhdlVhpiObjectLookupResult target;
      if (flow == VhdlVhpiCheckpointFlow::InProcessRestart) {
        target = objects.lookup_object(saved.source);
      } else {
        target = objects.find(saved.full_name);
      }
      if (!target || target.value.kind != saved.kind
          || target.value.full_name != saved.full_name) {
        result.handles.clear();
        result.error = VhdlVhpiCheckpointError::ObjectMismatch;
        return result;
      }
      if (flow == VhdlVhpiCheckpointFlow::InProcessRestart
          && target.value.handle != saved.source) {
        result.handles.clear();
        result.error = VhdlVhpiCheckpointError::HandleMismatch;
        return result;
      }
      result.handles.push_back(
          {saved.source, target.value.handle, saved.full_name});
    }
    if (flow == VhdlVhpiCheckpointFlow::PortableArtifact) {
      result.invalidations.reserve(11);
      result.relocations.reserve(
          artifact.compatibility.mapped_libraries.size()
          + artifact.compatibility.plugins.size());
      add_invalidations(result, artifact.native_state);
      add_relocations(result, artifact.compatibility, expected);
    }
  } catch (...) {
    result.handles.clear();
    result.invalidations.clear();
    result.relocations.clear();
    result.error = VhdlVhpiCheckpointError::ResourceLimit;
  }
  return result;
}

}  // namespace fsim::runtime
