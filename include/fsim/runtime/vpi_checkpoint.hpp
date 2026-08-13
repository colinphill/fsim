// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/vpi_callback.hpp"
#include "fsim/runtime/vpi_io.hpp"
#include "fsim/runtime/vpi_object.hpp"
#include "fsim/runtime/vpi_plugin.hpp"
#include "fsim/runtime/vpi_system.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace fsim::runtime {

inline constexpr std::uint32_t systemverilog_vpi_checkpoint_schema = 2;

enum class SystemVerilogVpiCheckpointFlow {
    InProcessRestart,
    PortableArtifact,
};

enum class SystemVerilogVpiCheckpointError {
    None,
    InvalidSimulation,
    InvalidArtifact,
    SchemaMismatch,
    HostAbiMismatch,
    PluginAbiMismatch,
    ContentMismatch,
    CacheMismatch,
    PluginMismatch,
    ExternalStateMismatch,
    ObjectStateMismatch,
    HandleMismatch,
    ResourceLimit,
};

enum class SystemVerilogVpiExternalStateKind {
    CallbackClosure,
    CallbackUserData,
    SystemRegistration,
    SystemUserData,
    ActiveSystemCall,
    TransientHandle,
    OpenDescriptor,
    PluginContext,
};

struct SystemVerilogVpiPluginProvenance {
    std::string name;
    std::string artifact;
    std::string content_fingerprint;
    std::string host_fingerprint;

    friend bool operator==(
        const SystemVerilogVpiPluginProvenance&,
        const SystemVerilogVpiPluginProvenance&) = default;
};

struct SystemVerilogVpiCheckpointCompatibility {
    std::string content_fingerprint;
    std::string cache_fingerprint;
    std::vector<SystemVerilogVpiPluginProvenance> plugins;
};

struct SystemVerilogVpiExternalStateSummary {
    std::size_t callbacks { };
    std::size_t system_registrations { };
    std::size_t system_calls { };
    std::size_t open_descriptors { };

    friend bool operator==(
        const SystemVerilogVpiExternalStateSummary&,
        const SystemVerilogVpiExternalStateSummary&) = default;
};

struct SystemVerilogVpiCheckpointArtifact {
    std::uint32_t schema { systemverilog_vpi_checkpoint_schema };
    std::uint32_t host_abi { FSIM_VPI_HOST_ABI_VERSION };
    std::uint32_t plugin_abi { FSIM_VPI_PLUGIN_ABI_VERSION };
    SystemVerilogVpiCheckpointCompatibility compatibility;
    SystemVerilogVpiObjectStateSnapshot objects;
    SystemVerilogVpiExternalStateSummary external_state;
};

struct SystemVerilogVpiCheckpointCaptureResult {
    SystemVerilogVpiCheckpointArtifact artifact;
    SystemVerilogVpiCheckpointError error {
        SystemVerilogVpiCheckpointError::None
    };

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == SystemVerilogVpiCheckpointError::None;
    }
};

struct SystemVerilogVpiExternalStateInvalidation {
    SystemVerilogVpiExternalStateKind kind {
        SystemVerilogVpiExternalStateKind::TransientHandle
    };
    std::size_t count { };
    std::string reason;
};

struct SystemVerilogVpiCheckpointRestoreResult {
    std::vector<SystemVerilogVpiObjectHandleRemap> handles;
    std::vector<SystemVerilogVpiExternalStateInvalidation> invalidations;
    SystemVerilogVpiCheckpointError error {
        SystemVerilogVpiCheckpointError::None
    };

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == SystemVerilogVpiCheckpointError::None;
    }
};

[[nodiscard]] SystemVerilogVpiPluginProvenance
make_systemverilog_vpi_plugin_provenance(
    const SystemVerilogVpiLoadedPlugin& plugin,
    std::string content_fingerprint,
    std::string host_fingerprint);

[[nodiscard]] SystemVerilogVpiCheckpointCaptureResult
capture_systemverilog_vpi_checkpoint(
    const SystemVerilogVpiObjectRegistry& objects,
    const SystemVerilogVpiCallbackManager& callbacks,
    const SystemVerilogVpiSystemRegistry& systems,
    const SystemVerilogVpiIoService& io,
    SystemVerilogVpiCheckpointCompatibility compatibility);

[[nodiscard]] SystemVerilogVpiCheckpointRestoreResult
restore_systemverilog_vpi_checkpoint(
    const SystemVerilogVpiCheckpointArtifact& artifact,
    SystemVerilogVpiCheckpointFlow flow,
    SystemVerilogVpiObjectRegistry& objects,
    const SystemVerilogVpiCallbackManager& callbacks,
    const SystemVerilogVpiSystemRegistry& systems,
    const SystemVerilogVpiIoService& io,
    const SystemVerilogVpiCheckpointCompatibility& expected);

} // namespace fsim::runtime
