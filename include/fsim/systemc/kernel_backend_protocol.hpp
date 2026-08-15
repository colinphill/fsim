// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/diagnostic/diagnostic.hpp"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

namespace fsim::systemc {

inline constexpr std::uint32_t kSystemCKernelProtocolVersion = 1U;
inline constexpr std::size_t kSystemCKernelMessageHeaderBytes = 128U;

template <typename Domain>
struct SystemCBackendId {
    std::uint64_t high { };
    std::uint64_t low { };

    [[nodiscard]] constexpr bool valid() const noexcept
    {
        return high != 0U || low != 0U;
    }

    friend constexpr auto operator<=>(const SystemCBackendId&,
        const SystemCBackendId&) = default;
};

struct SystemCIslandDomain;
struct SystemCHierarchyDomain;
struct SystemCObjectDomain;
struct SystemCEndpointDomain;
struct SystemCTransactionDomain;

using SystemCIslandId = SystemCBackendId<SystemCIslandDomain>;
using SystemCHierarchyId = SystemCBackendId<SystemCHierarchyDomain>;
using SystemCObjectId = SystemCBackendId<SystemCObjectDomain>;
using SystemCEndpointId = SystemCBackendId<SystemCEndpointDomain>;
using SystemCTransactionId = SystemCBackendId<SystemCTransactionDomain>;

struct SystemCSequenceId {
    std::uint64_t value { };

    [[nodiscard]] constexpr bool valid() const noexcept { return value != 0U; }

    friend constexpr auto operator<=>(const SystemCSequenceId&,
        const SystemCSequenceId&) = default;
};

static_assert(std::is_trivially_copyable_v<SystemCIslandId>);
static_assert(std::is_trivially_copyable_v<SystemCHierarchyId>);
static_assert(std::is_trivially_copyable_v<SystemCObjectId>);
static_assert(std::is_trivially_copyable_v<SystemCEndpointId>);
static_assert(std::is_trivially_copyable_v<SystemCTransactionId>);
static_assert(std::is_trivially_copyable_v<SystemCSequenceId>);

struct SystemCKernelProtocolLimits {
    std::size_t max_identity_bytes { 4096U };
    std::size_t max_payload_bytes { 1024U * 1024U };
    std::size_t max_message_bytes {
        kSystemCKernelMessageHeaderBytes + 1024U * 1024U
    };
};

enum class SystemCKernelOperation : std::uint16_t {
    handshake = 1,
    create_session = 2,
    create_object = 3,
    bind_endpoint = 4,
    elaborate = 5,
    start = 6,
    apply_inputs = 7,
    advance = 8,
    next_activity = 9,
    drain_outputs = 10,
    report = 11,
    inspect = 12,
    snapshot = 13,
    teardown = 14,
    observe_transaction = 15,
};

enum class SystemCKernelMessageDirection : std::uint8_t {
    request = 1,
    response = 2,
    event = 3,
};

enum class SystemCKernelMessageStatus : std::uint8_t {
    none = 0,
    ok = 1,
    rejected = 2,
    failed = 3,
    disconnected = 4,
};

enum class SystemCKernelMessageFlag : std::uint32_t {
    replayable = 1U << 0U,
    terminal = 1U << 1U,
};

struct SystemCKernelMessageHeader {
    std::uint32_t schema { kSystemCKernelProtocolVersion };
    SystemCKernelOperation operation { SystemCKernelOperation::handshake };
    SystemCKernelMessageDirection direction {
        SystemCKernelMessageDirection::request
    };
    SystemCKernelMessageStatus status { SystemCKernelMessageStatus::none };
    std::uint32_t flags { };
    SystemCIslandId island;
    SystemCHierarchyId hierarchy;
    SystemCObjectId object;
    SystemCEndpointId endpoint;
    SystemCTransactionId transaction;
    SystemCSequenceId sequence;
    SystemCSequenceId correlation;

    friend bool operator==(const SystemCKernelMessageHeader&,
        const SystemCKernelMessageHeader&) = default;
};

struct SystemCKernelMessage {
    SystemCKernelMessageHeader header;
    std::vector<std::byte> payload;

    friend bool operator==(const SystemCKernelMessage&,
        const SystemCKernelMessage&) = default;
};

enum class SystemCKernelTransportStatus : std::uint8_t {
    ok = 1,
    rejected = 2,
    disconnected = 3,
};

struct SystemCKernelTransportResult {
    SystemCKernelTransportStatus status { SystemCKernelTransportStatus::rejected };
    std::vector<std::byte> bytes;
};

class SystemCKernelBackend {
public:
    virtual ~SystemCKernelBackend() = default;

    [[nodiscard]] virtual SystemCKernelTransportResult exchange(
        std::span<const std::byte> request) noexcept = 0;
    virtual void close() noexcept = 0;
};

[[nodiscard]] std::optional<SystemCIslandId> make_systemc_island_id(
    std::string_view canonical_identity,
    const SystemCKernelProtocolLimits& limits,
    diagnostic::Engine& diagnostics);
[[nodiscard]] std::optional<SystemCHierarchyId> make_systemc_hierarchy_id(
    SystemCIslandId island, std::string_view canonical_path,
    const SystemCKernelProtocolLimits& limits,
    diagnostic::Engine& diagnostics);
[[nodiscard]] std::optional<SystemCObjectId> make_systemc_object_id(
    SystemCHierarchyId hierarchy, std::string_view canonical_path,
    const SystemCKernelProtocolLimits& limits,
    diagnostic::Engine& diagnostics);
[[nodiscard]] std::optional<SystemCEndpointId> make_systemc_endpoint_id(
    SystemCObjectId object, std::string_view canonical_role,
    const SystemCKernelProtocolLimits& limits,
    diagnostic::Engine& diagnostics);
[[nodiscard]] std::optional<SystemCSequenceId> make_systemc_sequence_id(
    SystemCIslandId island, std::uint64_t ordinal,
    diagnostic::Engine& diagnostics);
[[nodiscard]] std::optional<SystemCTransactionId> make_systemc_transaction_id(
    SystemCEndpointId endpoint, SystemCSequenceId sequence,
    diagnostic::Engine& diagnostics);

[[nodiscard]] bool validate_systemc_kernel_message(
    const SystemCKernelMessage& message,
    const SystemCKernelProtocolLimits& limits,
    diagnostic::Engine& diagnostics);
[[nodiscard]] std::optional<std::vector<std::byte>>
serialize_systemc_kernel_message(
    const SystemCKernelMessage& message,
    const SystemCKernelProtocolLimits& limits,
    diagnostic::Engine& diagnostics);
[[nodiscard]] std::optional<SystemCKernelMessage>
deserialize_systemc_kernel_message(
    std::span<const std::byte> bytes,
    const SystemCKernelProtocolLimits& limits,
    diagnostic::Engine& diagnostics);

} // namespace fsim::systemc
