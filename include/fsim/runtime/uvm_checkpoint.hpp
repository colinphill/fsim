// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/uvm_foreign.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fsim::runtime {

inline constexpr std::uint32_t systemverilog_uvm_checkpoint_schema = 1;

struct SystemVerilogUvmCheckpointLimits {
  std::size_t maximum_records{1U << 20U};
  std::size_t maximum_text_bytes{1U << 24U};
  std::size_t maximum_payload_bytes{1U << 26U};
  std::size_t maximum_roots{64};
  std::size_t maximum_identity_bytes{4'096};
  std::size_t maximum_external_phase_processes{1U << 20U};
  std::size_t maximum_external_callbacks{1U << 20U};
};

struct SystemVerilogUvmCheckpointProvenance {
  std::string content_identity;
  std::string cache_identity;
  std::string artifact_identity;
  std::vector<std::string> roots;

  friend bool
  operator==(const SystemVerilogUvmCheckpointProvenance &,
             const SystemVerilogUvmCheckpointProvenance &) = default;
};

struct SystemVerilogUvmCheckpointRecord {
  std::uint32_t kind{};
  std::uint32_t state{};
  std::uint32_t flags{};
  std::uint64_t root{};
  std::uint64_t value{};
  std::uint64_t auxiliary{};
  std::string identity;
  std::string detail;
  std::vector<std::uint8_t> payload;

  friend bool operator==(const SystemVerilogUvmCheckpointRecord &,
                         const SystemVerilogUvmCheckpointRecord &) = default;
};

struct SystemVerilogUvmCheckpointExternalState {
  std::uint64_t phase_processes{};
  std::uint64_t callbacks{};

  friend bool
  operator==(const SystemVerilogUvmCheckpointExternalState &,
             const SystemVerilogUvmCheckpointExternalState &) = default;
};

struct SystemVerilogUvmCheckpointArtifact {
  std::uint32_t schema{systemverilog_uvm_checkpoint_schema};
  std::uint32_t foreign_abi{FSIM_UVM_FOREIGN_ABI_VERSION};
  SimulationTick time{};
  std::uint64_t delta{};
  SystemVerilogUvmCheckpointProvenance provenance;
  std::vector<SystemVerilogUvmCheckpointRecord> records;
  SystemVerilogUvmCheckpointExternalState external_state;

  friend bool operator==(const SystemVerilogUvmCheckpointArtifact &,
                         const SystemVerilogUvmCheckpointArtifact &) = default;
};

enum class SystemVerilogUvmCheckpointError {
  None,
  CaptureFailure,
  InvalidArtifact,
  SchemaMismatch,
  AbiMismatch,
  ContentMismatch,
  CacheMismatch,
  ArtifactMismatch,
  RootMismatch,
  StateMismatch,
  ResourceLimit,
};

struct SystemVerilogUvmCheckpointCaptureResult {
  SystemVerilogUvmCheckpointArtifact artifact;
  SystemVerilogUvmCheckpointError error{SystemVerilogUvmCheckpointError::None};

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == SystemVerilogUvmCheckpointError::None;
  }
};

[[nodiscard]] SystemVerilogUvmCheckpointCaptureResult
capture_systemverilog_uvm_checkpoint(
    SystemVerilogUvmForeignService &foreign,
    SystemVerilogUvmCheckpointProvenance provenance,
    SystemVerilogUvmCheckpointLimits limits = {});

[[nodiscard]] SystemVerilogUvmCheckpointCaptureResult
make_systemverilog_uvm_bootstrap_checkpoint(
    SystemVerilogUvmCheckpointProvenance provenance,
    SystemVerilogUvmCheckpointLimits limits = {});

[[nodiscard]] SystemVerilogUvmCheckpointError
validate_systemverilog_uvm_checkpoint(
    const SystemVerilogUvmCheckpointArtifact &artifact,
    const SystemVerilogUvmCheckpointProvenance &expected,
    SystemVerilogUvmCheckpointLimits limits = {}) noexcept;

[[nodiscard]] SystemVerilogUvmCheckpointError
verify_systemverilog_uvm_checkpoint(
    const SystemVerilogUvmCheckpointArtifact &artifact,
    SystemVerilogUvmForeignService &foreign,
    const SystemVerilogUvmCheckpointProvenance &expected,
    SystemVerilogUvmCheckpointLimits limits = {});

} // namespace fsim::runtime
