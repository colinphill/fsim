// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/systemc/kernel_backend_value_codec.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace fsim::systemc {

inline constexpr std::uint32_t kSystemCKernelInventoryVersion = 1U;

enum class SystemCKernelChannelKind : std::uint8_t {
    signal = 1,
    buffer = 2,
    clock = 3,
    resolved_signal = 4,
    resolved_vector = 5,
    mutex = 6,
    semaphore = 7,
    event_queue = 8,
    custom = 9,
};

enum class SystemCKernelWriterPolicy : std::uint8_t {
    none = 1,
    one = 2,
    many = 3,
    unchecked = 4,
};

enum class SystemCKernelUpdateOwner : std::uint8_t {
    signal_kernel = 1,
    primitive_kernel = 2,
    custom = 3,
};

enum class SystemCKernelObservationMode : std::uint8_t {
    value_changed = 1,
    clock_edges = 2,
    primitive_events = 3,
    unsupported = 4,
};

enum class SystemCKernelInventoryCode : std::uint16_t {
    none = 0,
    metadata = 1,
    lifecycle = 2,
    unsupported = 3,
    resource = 4,
};

struct SystemCKernelInventoryLimits {
    std::size_t max_channels { 1U << 20U };
    std::size_t max_path_bytes { 4096U };
    std::size_t max_type_name_bytes { 4096U };
    std::size_t max_encoded_bytes { 64U * 1024U * 1024U };
};

struct SystemCKernelChannelValueProfile {
    SystemCKernelValueKind kind { SystemCKernelValueKind::bit2 };
    std::uint32_t width { };
    bool is_signed { };

    friend bool operator==(const SystemCKernelChannelValueProfile&,
        const SystemCKernelChannelValueProfile&) = default;
};

struct SystemCKernelChannelDescriptor {
    std::string canonical_path;
    std::string type_name;
    SystemCKernelChannelKind kind { SystemCKernelChannelKind::custom };
    std::optional<SystemCKernelChannelValueProfile> value;
    SystemCKernelWriterPolicy writer { SystemCKernelWriterPolicy::none };
    SystemCKernelUpdateOwner update_owner { SystemCKernelUpdateOwner::custom };
    SystemCKernelObservationMode observation {
        SystemCKernelObservationMode::unsupported
    };
    bool supported { };

    friend bool operator==(const SystemCKernelChannelDescriptor&,
        const SystemCKernelChannelDescriptor&) = default;
};

struct SystemCKernelChannelInventoryEntry {
    SystemCObjectId object;
    SystemCEndpointId channel;
    SystemCKernelChannelDescriptor descriptor;

    friend bool operator==(const SystemCKernelChannelInventoryEntry&,
        const SystemCKernelChannelInventoryEntry&) = default;
};

struct SystemCKernelChannelInventorySnapshot {
    SystemCIslandId island;
    SystemCHierarchyId hierarchy;
    std::vector<SystemCKernelChannelInventoryEntry> channels;

    friend bool operator==(const SystemCKernelChannelInventorySnapshot&,
        const SystemCKernelChannelInventorySnapshot&) = default;
};

class SystemCKernelChannelInventory {
public:
    SystemCKernelChannelInventory(SystemCIslandId island,
        SystemCHierarchyId hierarchy, SystemCKernelInventoryLimits limits = { });

    [[nodiscard]] bool register_channel(SystemCKernelChannelDescriptor descriptor,
        diagnostic::Engine& diagnostics);
    [[nodiscard]] bool freeze(diagnostic::Engine& diagnostics);
    [[nodiscard]] bool validate_live_snapshot(
        std::span<const SystemCKernelChannelDescriptor> live,
        diagnostic::Engine& diagnostics) const;

    [[nodiscard]] bool frozen() const noexcept;
    [[nodiscard]] const SystemCKernelChannelInventorySnapshot& snapshot()
        const noexcept;

private:
    SystemCKernelInventoryLimits limits_;
    SystemCKernelChannelInventorySnapshot snapshot_;
    bool frozen_ { };
};

[[nodiscard]] bool validate_systemc_kernel_channel_descriptor(
    const SystemCKernelChannelDescriptor& descriptor,
    const SystemCKernelInventoryLimits& limits,
    diagnostic::Engine& diagnostics);

[[nodiscard]] bool validate_systemc_kernel_channel_inventory(
    const SystemCKernelChannelInventorySnapshot& snapshot,
    const SystemCKernelInventoryLimits& limits,
    diagnostic::Engine& diagnostics);

[[nodiscard]] std::optional<std::vector<std::byte>>
serialize_systemc_kernel_channel_inventory(
    const SystemCKernelChannelInventorySnapshot& snapshot,
    const SystemCKernelInventoryLimits& limits,
    diagnostic::Engine& diagnostics);

[[nodiscard]] std::optional<SystemCKernelChannelInventorySnapshot>
deserialize_systemc_kernel_channel_inventory(std::span<const std::byte> bytes,
    const SystemCKernelInventoryLimits& limits,
    diagnostic::Engine& diagnostics);

[[nodiscard]] const char* systemc_kernel_inventory_diagnostic_code(
    SystemCKernelInventoryCode code) noexcept;

} // namespace fsim::systemc
