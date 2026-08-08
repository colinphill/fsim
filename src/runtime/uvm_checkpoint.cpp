// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/uvm_checkpoint.hpp"

#include "fsim/runtime/class_heap.hpp"
#include "fsim/runtime/uvm_component.hpp"
#include "fsim/runtime/uvm_object.hpp"
#include "fsim/runtime/uvm_phase.hpp"

#include <algorithm>
#include <set>
#include <utility>

namespace fsim::runtime {
namespace {

bool valid_provenance(const SystemVerilogUvmCheckpointProvenance &value,
                      const SystemVerilogUvmCheckpointLimits &limits) {
  if (value.content_identity.empty() || value.cache_identity.empty() ||
      value.content_identity.size() > limits.maximum_identity_bytes ||
      value.cache_identity.size() > limits.maximum_identity_bytes ||
      value.artifact_identity.size() > limits.maximum_identity_bytes ||
      value.roots.size() > limits.maximum_roots) {
    return false;
  }
  std::set<std::string, std::less<>> roots;
  for (const auto &root : value.roots) {
    if (root.empty() || root.size() > limits.maximum_identity_bytes ||
        !roots.insert(root).second) {
      return false;
    }
  }
  return true;
}

bool provenance_exceeds_limits(
    const SystemVerilogUvmCheckpointProvenance &value,
    const SystemVerilogUvmCheckpointLimits &limits) {
  if (value.content_identity.size() > limits.maximum_identity_bytes ||
      value.cache_identity.size() > limits.maximum_identity_bytes ||
      value.artifact_identity.size() > limits.maximum_identity_bytes ||
      value.roots.size() > limits.maximum_roots) {
    return true;
  }
  return std::ranges::any_of(value.roots, [&](const auto &root) {
    return root.size() > limits.maximum_identity_bytes;
  });
}

bool valid_records(const SystemVerilogUvmCheckpointArtifact &artifact,
                   const SystemVerilogUvmCheckpointLimits &limits) {
  if (artifact.records.size() > limits.maximum_records ||
      artifact.external_state.phase_processes >
          limits.maximum_external_phase_processes ||
      artifact.external_state.callbacks > limits.maximum_external_callbacks) {
    return false;
  }
  std::size_t text_bytes{};
  std::size_t payload_bytes{};
  for (const auto &record : artifact.records) {
    if (record.kind < FSIM_UVM_FOREIGN_PHASE ||
        record.kind > FSIM_UVM_FOREIGN_TLM2_TRANSACTION ||
        record.kind == FSIM_UVM_FOREIGN_PHASE_PROCESS ||
        record.identity.empty() ||
        record.identity.size() > limits.maximum_text_bytes - text_bytes ||
        record.detail.size() >
            limits.maximum_text_bytes - text_bytes - record.identity.size() ||
        record.payload.size() > limits.maximum_payload_bytes - payload_bytes) {
      return false;
    }
    text_bytes += record.identity.size() + record.detail.size();
    payload_bytes += record.payload.size();
  }
  return true;
}

SystemVerilogUvmCheckpointError
provenance_error(const SystemVerilogUvmCheckpointProvenance &saved,
                 const SystemVerilogUvmCheckpointProvenance &expected) {
  if (saved.content_identity != expected.content_identity) {
    return SystemVerilogUvmCheckpointError::ContentMismatch;
  }
  if (saved.cache_identity != expected.cache_identity) {
    return SystemVerilogUvmCheckpointError::CacheMismatch;
  }
  if (saved.artifact_identity != expected.artifact_identity) {
    return SystemVerilogUvmCheckpointError::ArtifactMismatch;
  }
  if (saved.roots != expected.roots) {
    return SystemVerilogUvmCheckpointError::RootMismatch;
  }
  return SystemVerilogUvmCheckpointError::None;
}

} // namespace

SystemVerilogUvmCheckpointCaptureResult capture_systemverilog_uvm_checkpoint(
    SystemVerilogUvmForeignService &foreign,
    SystemVerilogUvmCheckpointProvenance provenance,
    const SystemVerilogUvmCheckpointLimits limits) {
  SystemVerilogUvmCheckpointCaptureResult result;
  if (provenance_exceeds_limits(provenance, limits)) {
    result.error = SystemVerilogUvmCheckpointError::ResourceLimit;
    return result;
  }
  if (!valid_provenance(provenance, limits)) {
    result.error = SystemVerilogUvmCheckpointError::InvalidArtifact;
    return result;
  }
  fsim_uvm_foreign_snapshot_v1 snapshot{};
  const auto capture = foreign.capture(snapshot);
  if (capture != FSIM_UVM_FOREIGN_OK) {
    result.error = capture == FSIM_UVM_FOREIGN_RESOURCE_LIMIT
                       ? SystemVerilogUvmCheckpointError::ResourceLimit
                       : SystemVerilogUvmCheckpointError::CaptureFailure;
    return result;
  }
  struct Release final {
    SystemVerilogUvmForeignService *service;
    fsim_uvm_foreign_snapshot_v1 snapshot;
    ~Release() { (void)service->release(snapshot); }
  } release{&foreign, snapshot};
  if (snapshot.record_count > limits.maximum_records) {
    result.error = SystemVerilogUvmCheckpointError::ResourceLimit;
    return result;
  }
  result.artifact.time = snapshot.time;
  result.artifact.delta = snapshot.delta;
  result.artifact.provenance = std::move(provenance);
  result.artifact.external_state.callbacks = foreign.callback_count();
  if (result.artifact.external_state.callbacks >
      limits.maximum_external_callbacks) {
    result.error = SystemVerilogUvmCheckpointError::ResourceLimit;
    return result;
  }
  try {
    result.artifact.records.reserve(
        static_cast<std::size_t>(snapshot.record_count));
    std::size_t text_bytes{};
    std::size_t payload_bytes{};
    for (std::size_t index = 0; index < snapshot.record_count; ++index) {
      fsim_uvm_foreign_record_v1 value{};
      const auto sizing =
          foreign.copy_record(snapshot, index, value, {}, {}, {});
      if (sizing != FSIM_UVM_FOREIGN_OK &&
          sizing != FSIM_UVM_FOREIGN_BUFFER_TOO_SMALL) {
        result.error = SystemVerilogUvmCheckpointError::CaptureFailure;
        return result;
      }
      if (value.kind == FSIM_UVM_FOREIGN_PHASE_PROCESS) {
        if (result.artifact.external_state.phase_processes ==
            limits.maximum_external_phase_processes) {
          result.error = SystemVerilogUvmCheckpointError::ResourceLimit;
          return result;
        }
        ++result.artifact.external_state.phase_processes;
        continue;
      }
      if (value.identity_size > limits.maximum_text_bytes - text_bytes ||
          value.detail_size >
              limits.maximum_text_bytes - text_bytes - value.identity_size ||
          value.payload_size > limits.maximum_payload_bytes - payload_bytes) {
        result.error = SystemVerilogUvmCheckpointError::ResourceLimit;
        return result;
      }
      SystemVerilogUvmCheckpointRecord record;
      record.kind = value.kind;
      record.state = value.state;
      record.flags = value.flags;
      record.root = value.root;
      record.value = value.value;
      record.auxiliary = value.auxiliary;
      record.identity.resize(static_cast<std::size_t>(value.identity_size));
      record.detail.resize(static_cast<std::size_t>(value.detail_size));
      record.payload.resize(static_cast<std::size_t>(value.payload_size));
      const auto copied = foreign.copy_record(
          snapshot, index, value, std::span<char>{record.identity},
          std::span<char>{record.detail},
          std::span<std::uint8_t>{record.payload});
      if (copied != FSIM_UVM_FOREIGN_OK) {
        result.error = SystemVerilogUvmCheckpointError::CaptureFailure;
        return result;
      }
      text_bytes += record.identity.size() + record.detail.size();
      payload_bytes += record.payload.size();
      result.artifact.records.push_back(std::move(record));
    }
  } catch (...) {
    result.artifact.records.clear();
    result.error = SystemVerilogUvmCheckpointError::ResourceLimit;
    return result;
  }
  return result;
}

SystemVerilogUvmCheckpointCaptureResult
make_systemverilog_uvm_bootstrap_checkpoint(
    SystemVerilogUvmCheckpointProvenance provenance,
    const SystemVerilogUvmCheckpointLimits limits) {
  try {
    SystemVerilogClassHeap heap;
    SystemVerilogUvmObjectService objects{
        heap, [](std::string_view, std::string_view, std::string_view) {
          return SystemVerilogClassHandle{};
        }};
    SystemVerilogUvmComponentService components{heap, objects};
    SystemVerilogUvmActivityService activity;
    SystemVerilogUvmPhaseService phases{components};
    SystemVerilogUvmObjectionService objections{objects, components, phases};
    SystemVerilogUvmTlm1Service tlm1{heap, components};
    SystemVerilogUvmTlm2Service tlm2{components};
    SystemVerilogUvmForeignService foreign{phases, objections, tlm1, tlm2,
                                           activity};
    (void)phases.create_standard_schedule();
    return capture_systemverilog_uvm_checkpoint(foreign, std::move(provenance),
                                                limits);
  } catch (...) {
    SystemVerilogUvmCheckpointCaptureResult result;
    result.error = SystemVerilogUvmCheckpointError::CaptureFailure;
    return result;
  }
}

SystemVerilogUvmCheckpointError validate_systemverilog_uvm_checkpoint(
    const SystemVerilogUvmCheckpointArtifact &artifact,
    const SystemVerilogUvmCheckpointProvenance &expected,
    const SystemVerilogUvmCheckpointLimits limits) noexcept {
  try {
    if (artifact.schema != systemverilog_uvm_checkpoint_schema) {
      return SystemVerilogUvmCheckpointError::SchemaMismatch;
    }
    if (artifact.foreign_abi != FSIM_UVM_FOREIGN_ABI_VERSION) {
      return SystemVerilogUvmCheckpointError::AbiMismatch;
    }
    if (!valid_provenance(artifact.provenance, limits) ||
        !valid_provenance(expected, limits) ||
        !valid_records(artifact, limits)) {
      return SystemVerilogUvmCheckpointError::InvalidArtifact;
    }
    return provenance_error(artifact.provenance, expected);
  } catch (...) {
    return SystemVerilogUvmCheckpointError::ResourceLimit;
  }
}

SystemVerilogUvmCheckpointError verify_systemverilog_uvm_checkpoint(
    const SystemVerilogUvmCheckpointArtifact &artifact,
    SystemVerilogUvmForeignService &foreign,
    const SystemVerilogUvmCheckpointProvenance &expected,
    const SystemVerilogUvmCheckpointLimits limits) {
  const auto valid =
      validate_systemverilog_uvm_checkpoint(artifact, expected, limits);
  if (valid != SystemVerilogUvmCheckpointError::None) {
    return valid;
  }
  auto current =
      capture_systemverilog_uvm_checkpoint(foreign, expected, limits);
  if (!current) {
    return current.error;
  }
  current.artifact.schema = artifact.schema;
  current.artifact.foreign_abi = artifact.foreign_abi;
  return current.artifact == artifact
             ? SystemVerilogUvmCheckpointError::None
             : SystemVerilogUvmCheckpointError::StateMismatch;
}

} // namespace fsim::runtime
