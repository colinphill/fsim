// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/systemc/kernel_backend_inventory.hpp"
#include "fsim/systemc/kernel_backend_synchronization.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace fsim::systemc {

inline constexpr std::uint32_t kSystemCKernelBindingInventoryVersion = 1U;

enum class SystemCKernelBindingKind : std::uint8_t {
    port = 1,
    export_interface = 2,
};

enum class SystemCKernelBindingDirection : std::uint8_t {
    none = 1,
    input = 2,
    output = 3,
    inout = 4,
};

enum class SystemCKernelBindingCode : std::uint16_t {
    none = 0,
    metadata = 1,
    topology = 2,
    lifecycle = 3,
    resource = 4,
};

struct SystemCKernelBindingLimits {
    std::size_t max_bindings { 1U << 20U };
    std::size_t max_targets_per_binding { 4096U };
    std::size_t max_chain_depth { 1024U };
    std::size_t max_path_bytes { 4096U };
    std::size_t max_interface_name_bytes { 4096U };
    std::size_t max_encoded_bytes { 64U * 1024U * 1024U };
};

struct SystemCKernelBindingTarget {
    std::vector<std::string> chain;
    std::string final_channel_path;
    SystemCEndpointId final_channel;
    std::optional<SystemCKernelHostLanguage> foreign_language;
    std::optional<SystemCEndpointId> foreign_endpoint;

    friend bool operator==(const SystemCKernelBindingTarget&,
        const SystemCKernelBindingTarget&) = default;
};

struct SystemCKernelBindingDescriptor {
    std::string declared_path;
    std::string interface_name;
    SystemCKernelBindingKind kind { SystemCKernelBindingKind::port };
    SystemCKernelBindingDirection direction {
        SystemCKernelBindingDirection::none
    };
    std::vector<SystemCKernelBindingTarget> targets;

    friend bool operator==(const SystemCKernelBindingDescriptor&,
        const SystemCKernelBindingDescriptor&) = default;
};

struct SystemCKernelBindingEntry {
    SystemCEndpointId declaration;
    SystemCKernelBindingDescriptor descriptor;

    friend bool operator==(const SystemCKernelBindingEntry&,
        const SystemCKernelBindingEntry&) = default;
};

struct SystemCKernelBindingInventorySnapshot {
    SystemCIslandId island;
    SystemCHierarchyId hierarchy;
    std::vector<SystemCKernelBindingEntry> bindings;

    friend bool operator==(const SystemCKernelBindingInventorySnapshot&,
        const SystemCKernelBindingInventorySnapshot&) = default;
};

class SystemCKernelBindingInventory {
public:
    SystemCKernelBindingInventory(
        const SystemCKernelChannelInventorySnapshot& channels,
        SystemCKernelBindingLimits limits = { });

    [[nodiscard]] bool register_binding(SystemCKernelBindingDescriptor descriptor,
        diagnostic::Engine& diagnostics);
    [[nodiscard]] bool freeze(diagnostic::Engine& diagnostics);

    [[nodiscard]] bool frozen() const noexcept;
    [[nodiscard]] const SystemCKernelBindingInventorySnapshot& snapshot()
        const noexcept;

private:
    const SystemCKernelChannelInventorySnapshot* channels_;
    SystemCKernelBindingLimits limits_;
    SystemCKernelBindingInventorySnapshot snapshot_;
    bool frozen_ { };
};

[[nodiscard]] bool validate_systemc_kernel_binding_inventory(
    const SystemCKernelBindingInventorySnapshot& snapshot,
    const SystemCKernelBindingLimits& limits,
    diagnostic::Engine& diagnostics);

[[nodiscard]] std::optional<std::vector<std::byte>>
serialize_systemc_kernel_binding_inventory(
    const SystemCKernelBindingInventorySnapshot& snapshot,
    const SystemCKernelBindingLimits& limits,
    diagnostic::Engine& diagnostics);

[[nodiscard]] std::optional<SystemCKernelBindingInventorySnapshot>
deserialize_systemc_kernel_binding_inventory(std::span<const std::byte> bytes,
    const SystemCKernelBindingLimits& limits,
    diagnostic::Engine& diagnostics);

[[nodiscard]] const char* systemc_kernel_binding_diagnostic_code(
    SystemCKernelBindingCode code) noexcept;

} // namespace fsim::systemc
