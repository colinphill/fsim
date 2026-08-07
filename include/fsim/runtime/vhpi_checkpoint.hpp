// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/vhpi_object.hpp"
#include "fsim/runtime/vhpi_plugin.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace fsim::runtime {

inline constexpr std::uint32_t vhdl_vhpi_checkpoint_schema = 1;

enum class VhdlVhpiCheckpointFlow {
  InProcessRestart,
  PortableArtifact,
};

enum class VhdlVhpiCheckpointError {
  None,
  InvalidSimulation,
  InvalidArtifact,
  SchemaMismatch,
  HostAbiMismatch,
  PluginAbiMismatch,
  PointerWidthMismatch,
  ContentMismatch,
  CacheMismatch,
  MappedLibraryMismatch,
  PluginMismatch,
  NativeStateMismatch,
  ObjectMismatch,
  HandleMismatch,
  ResourceLimit,
};

enum class VhdlVhpiNativeStateKind {
  CallbackClosure,
  CallbackUserData,
  ForeignSubprogram,
  ForeignModel,
  ForeignUserData,
  ActiveForeignCall,
  OpenFile,
  ProtectedLease,
  ScheduledTransaction,
  IoSinkContext,
  PluginContext,
};

struct VhdlVhpiMappedLibraryProvenance {
  std::string identity;
  std::string artifact;
  std::string content_fingerprint;

  friend bool operator==(
      const VhdlVhpiMappedLibraryProvenance&,
      const VhdlVhpiMappedLibraryProvenance&) = default;
};

struct VhdlVhpiPluginProvenance {
  std::string name;
  std::string artifact;
  std::string mapped_library;
  std::string content_fingerprint;
  std::string host_fingerprint;

  friend bool operator==(
      const VhdlVhpiPluginProvenance&,
      const VhdlVhpiPluginProvenance&) = default;
};

struct VhdlVhpiCheckpointCompatibility {
  std::string content_fingerprint;
  std::string cache_fingerprint;
  std::vector<VhdlVhpiMappedLibraryProvenance> mapped_libraries;
  std::vector<VhdlVhpiPluginProvenance> plugins;
};

struct VhdlVhpiNativeStateSummary {
  std::uint64_t callback_closures{};
  std::uint64_t callback_user_data{};
  std::uint64_t foreign_subprograms{};
  std::uint64_t foreign_models{};
  std::uint64_t foreign_user_data{};
  std::uint64_t active_foreign_calls{};
  std::uint64_t open_files{};
  std::uint64_t protected_leases{};
  std::uint64_t scheduled_transactions{};
  std::uint64_t io_sink_contexts{};
  std::uint64_t plugin_contexts{};

  friend bool operator==(
      const VhdlVhpiNativeStateSummary&,
      const VhdlVhpiNativeStateSummary&) = default;
};

struct VhdlVhpiCheckpointObject {
  fsim_vhpi_handle_v1 source{};
  VhdlVhpiObjectKind kind{VhdlVhpiObjectKind::Root};
  std::string full_name;
};

struct VhdlVhpiCheckpointArtifact {
  std::uint32_t schema{vhdl_vhpi_checkpoint_schema};
  std::uint32_t host_abi{FSIM_VHPI_HOST_ABI_VERSION};
  std::uint32_t host_size{sizeof(fsim_vhpi_host_v1)};
  std::uint32_t plugin_abi{FSIM_VHPI_PLUGIN_ABI_VERSION};
  std::uint32_t plugin_size{sizeof(fsim_vhpi_plugin_v1)};
  std::uint32_t pointer_bits{sizeof(void*) * 8U};
  std::uint64_t source_simulation_identity{};
  VhdlVhpiCheckpointCompatibility compatibility;
  std::vector<VhdlVhpiCheckpointObject> objects;
  VhdlVhpiNativeStateSummary native_state;
};

struct VhdlVhpiCheckpointCaptureResult {
  VhdlVhpiCheckpointArtifact artifact;
  VhdlVhpiCheckpointError error{VhdlVhpiCheckpointError::None};

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == VhdlVhpiCheckpointError::None;
  }
};

struct VhdlVhpiHandleRemap {
  fsim_vhpi_handle_v1 source{};
  fsim_vhpi_handle_v1 target{};
  std::string full_name;
};

struct VhdlVhpiNativeStateInvalidation {
  VhdlVhpiNativeStateKind kind{
      VhdlVhpiNativeStateKind::CallbackClosure};
  std::uint64_t count{};
  std::string reason;
};

struct VhdlVhpiArtifactRelocation {
  std::string identity;
  std::string source_artifact;
  std::string target_artifact;
};

struct VhdlVhpiCheckpointRestoreResult {
  std::vector<VhdlVhpiHandleRemap> handles;
  std::vector<VhdlVhpiNativeStateInvalidation> invalidations;
  std::vector<VhdlVhpiArtifactRelocation> relocations;
  VhdlVhpiCheckpointError error{VhdlVhpiCheckpointError::None};

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == VhdlVhpiCheckpointError::None;
  }
};

[[nodiscard]] VhdlVhpiPluginProvenance
make_vhdl_vhpi_plugin_provenance(
    const VhdlVhpiLoadedPlugin& plugin,
    std::string mapped_library,
    std::string content_fingerprint,
    std::string host_fingerprint);

[[nodiscard]] VhdlVhpiCheckpointCaptureResult
capture_vhdl_vhpi_checkpoint(
    const VhdlVhpiObjectRegistry& objects,
    std::span<const fsim_vhpi_handle_v1> exported_objects,
    VhdlVhpiCheckpointCompatibility compatibility,
    VhdlVhpiNativeStateSummary native_state = {});

[[nodiscard]] VhdlVhpiCheckpointRestoreResult
restore_vhdl_vhpi_checkpoint(
    const VhdlVhpiCheckpointArtifact& artifact,
    VhdlVhpiCheckpointFlow flow,
    const VhdlVhpiObjectRegistry& objects,
    const VhdlVhpiCheckpointCompatibility& expected,
    const VhdlVhpiNativeStateSummary& current_native_state = {});

}  // namespace fsim::runtime
