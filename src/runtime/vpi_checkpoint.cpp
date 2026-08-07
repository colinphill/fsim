// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/vpi_checkpoint.hpp"

#include <algorithm>
#include <unordered_set>
#include <utility>

namespace fsim::runtime {

namespace {

constexpr std::size_t maximum_identity_size = 4096;
constexpr std::size_t maximum_plugins = 1024;

SystemVerilogVpiExternalStateSummary external_state(
    const SystemVerilogVpiCallbackManager& callbacks,
    const SystemVerilogVpiSystemRegistry& systems,
    const SystemVerilogVpiIoService& io) {
  return {
      callbacks.registrations(),
      systems.registrations(),
      systems.calls(),
      io.open_files(),
  };
}

bool valid_compatibility(
    const SystemVerilogVpiCheckpointCompatibility& compatibility) {
  if (compatibility.content_fingerprint.empty()
      || compatibility.cache_fingerprint.empty()
      || compatibility.content_fingerprint.size() > maximum_identity_size
      || compatibility.cache_fingerprint.size() > maximum_identity_size
      || compatibility.plugins.size() > maximum_plugins) {
    return false;
  }
  std::unordered_set<std::string> artifacts;
  try {
    artifacts.reserve(compatibility.plugins.size());
    for (const auto& plugin : compatibility.plugins) {
      if (plugin.name.empty() || plugin.artifact.empty()
          || plugin.content_fingerprint.empty()
          || plugin.host_fingerprint.empty()
          || plugin.name.size() > maximum_identity_size
          || plugin.artifact.size() > maximum_identity_size
          || plugin.content_fingerprint.size() > maximum_identity_size
          || plugin.host_fingerprint.size() > maximum_identity_size
          || !artifacts.insert(plugin.artifact).second) {
        return false;
      }
    }
  } catch (...) {
    return false;
  }
  return true;
}

SystemVerilogVpiCheckpointError compatibility_error(
    const SystemVerilogVpiCheckpointCompatibility& saved,
    const SystemVerilogVpiCheckpointCompatibility& expected) {
  if (saved.content_fingerprint != expected.content_fingerprint) {
    return SystemVerilogVpiCheckpointError::ContentMismatch;
  }
  if (saved.cache_fingerprint != expected.cache_fingerprint) {
    return SystemVerilogVpiCheckpointError::CacheMismatch;
  }
  if (saved.plugins != expected.plugins) {
    return SystemVerilogVpiCheckpointError::PluginMismatch;
  }
  return SystemVerilogVpiCheckpointError::None;
}

void add_invalidation(
    SystemVerilogVpiCheckpointRestoreResult& result,
    const SystemVerilogVpiExternalStateKind kind,
    const std::size_t count,
    std::string reason) {
  if (count != 0) {
    result.invalidations.push_back(
        {kind, count, std::move(reason)});
  }
}

}  // namespace

SystemVerilogVpiPluginProvenance
make_systemverilog_vpi_plugin_provenance(
    const SystemVerilogVpiLoadedPlugin& plugin,
    std::string content_fingerprint,
    std::string host_fingerprint) {
  return {
      plugin.name(),
      plugin.path().generic_string(),
      std::move(content_fingerprint),
      std::move(host_fingerprint),
  };
}

SystemVerilogVpiCheckpointCaptureResult
capture_systemverilog_vpi_checkpoint(
    const SystemVerilogVpiObjectRegistry& objects,
    const SystemVerilogVpiCallbackManager& callbacks,
    const SystemVerilogVpiSystemRegistry& systems,
    const SystemVerilogVpiIoService& io,
    SystemVerilogVpiCheckpointCompatibility compatibility) {
  SystemVerilogVpiCheckpointCaptureResult result;
  if (!objects.valid() || !callbacks.valid() || !systems.valid()
      || !io.valid()
      || objects.simulation_identity() != systems.simulation_identity()
      || objects.simulation_identity() != io.simulation_identity()) {
    result.error = SystemVerilogVpiCheckpointError::InvalidSimulation;
    return result;
  }
  if (!valid_compatibility(compatibility)) {
    result.error = SystemVerilogVpiCheckpointError::InvalidArtifact;
    return result;
  }
  result.artifact.compatibility = std::move(compatibility);
  result.artifact.objects = objects.snapshot_values();
  if (!result.artifact.objects) {
    result.error =
        result.artifact.objects.error
            == SystemVerilogVpiObjectStateError::ResourceLimit
        ? SystemVerilogVpiCheckpointError::ResourceLimit
        : SystemVerilogVpiCheckpointError::ObjectStateMismatch;
    return result;
  }
  result.artifact.external_state =
      external_state(callbacks, systems, io);
  return result;
}

SystemVerilogVpiCheckpointRestoreResult
restore_systemverilog_vpi_checkpoint(
    const SystemVerilogVpiCheckpointArtifact& artifact,
    const SystemVerilogVpiCheckpointFlow flow,
    SystemVerilogVpiObjectRegistry& objects,
    const SystemVerilogVpiCallbackManager& callbacks,
    const SystemVerilogVpiSystemRegistry& systems,
    const SystemVerilogVpiIoService& io,
    const SystemVerilogVpiCheckpointCompatibility& expected) {
  SystemVerilogVpiCheckpointRestoreResult result;
  if (!objects.valid() || !callbacks.valid() || !systems.valid()
      || !io.valid()
      || objects.simulation_identity() != systems.simulation_identity()
      || objects.simulation_identity() != io.simulation_identity()) {
    result.error = SystemVerilogVpiCheckpointError::InvalidSimulation;
    return result;
  }
  if (artifact.schema != systemverilog_vpi_checkpoint_schema) {
    result.error = SystemVerilogVpiCheckpointError::SchemaMismatch;
    return result;
  }
  if (artifact.host_abi != FSIM_VPI_HOST_ABI_VERSION) {
    result.error = SystemVerilogVpiCheckpointError::HostAbiMismatch;
    return result;
  }
  if (artifact.plugin_abi != FSIM_VPI_PLUGIN_ABI_VERSION) {
    result.error = SystemVerilogVpiCheckpointError::PluginAbiMismatch;
    return result;
  }
  if (!valid_compatibility(artifact.compatibility)
      || !valid_compatibility(expected)
      || !artifact.objects) {
    result.error = SystemVerilogVpiCheckpointError::InvalidArtifact;
    return result;
  }
  result.error = compatibility_error(artifact.compatibility, expected);
  if (result.error != SystemVerilogVpiCheckpointError::None) {
    return result;
  }

  const auto current_external = external_state(callbacks, systems, io);
  if (flow == SystemVerilogVpiCheckpointFlow::InProcessRestart) {
    if (artifact.objects.simulation_identity
            != objects.simulation_identity()
        || artifact.external_state != current_external) {
      result.error =
          SystemVerilogVpiCheckpointError::ExternalStateMismatch;
      return result;
    }
    for (const auto& state : artifact.objects.objects) {
      const auto object = objects.lookup(state.source_handle);
      if (!object || object.value->full_name != state.full_name) {
        result.error = SystemVerilogVpiCheckpointError::HandleMismatch;
        return result;
      }
    }
  } else if (current_external != SystemVerilogVpiExternalStateSummary{}) {
    result.error = SystemVerilogVpiCheckpointError::ExternalStateMismatch;
    return result;
  }

  if (flow == SystemVerilogVpiCheckpointFlow::PortableArtifact) {
    try {
      result.invalidations.reserve(7);
      add_invalidation(result,
          SystemVerilogVpiExternalStateKind::CallbackClosure,
          artifact.external_state.callbacks,
          "native callback closures require plug-in re-registration");
      add_invalidation(result,
          SystemVerilogVpiExternalStateKind::CallbackUserData,
          artifact.external_state.callbacks,
          "pointer-valued callback user data is process-local");
      add_invalidation(result,
          SystemVerilogVpiExternalStateKind::SystemRegistration,
          artifact.external_state.system_registrations,
          "native system callables require plug-in re-registration");
      add_invalidation(result,
          SystemVerilogVpiExternalStateKind::SystemUserData,
          artifact.external_state.system_registrations,
          "pointer-valued system user data is process-local");
      add_invalidation(result,
          SystemVerilogVpiExternalStateKind::ActiveSystemCall,
          artifact.external_state.system_calls,
          "active calls and argument handles cannot cross artifacts");
      add_invalidation(result,
          SystemVerilogVpiExternalStateKind::OpenDescriptor,
          artifact.external_state.open_descriptors,
          "native streams cannot cross artifacts");
      add_invalidation(result,
          SystemVerilogVpiExternalStateKind::PluginContext,
          artifact.compatibility.plugins.size(),
          "dynamic-library contexts require verified plug-in reload");
    } catch (...) {
      result.invalidations.clear();
      result.error = SystemVerilogVpiCheckpointError::ResourceLimit;
      return result;
    }
  }

  auto restored = objects.restore_values(artifact.objects);
  if (!restored) {
    result.error =
        restored.error == SystemVerilogVpiObjectStateError::ResourceLimit
        ? SystemVerilogVpiCheckpointError::ResourceLimit
        : SystemVerilogVpiCheckpointError::ObjectStateMismatch;
    return result;
  }
  result.handles = std::move(restored.handles);

  if (flow == SystemVerilogVpiCheckpointFlow::InProcessRestart) {
    const bool exact = std::ranges::all_of(
        result.handles,
        [](const SystemVerilogVpiObjectHandleRemap& handle) {
          return handle.source == handle.target;
        });
    if (!exact) {
      result.handles.clear();
      result.error = SystemVerilogVpiCheckpointError::HandleMismatch;
    }
    return result;
  }

  return result;
}

}  // namespace fsim::runtime
