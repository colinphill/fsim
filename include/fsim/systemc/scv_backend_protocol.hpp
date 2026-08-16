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

inline constexpr std::uint32_t scv_backend_protocol_version = 1U;
inline constexpr std::size_t scv_backend_message_header_bytes = 160U;

template <typename Domain>
struct ScvBackendId {
    std::uint64_t high { };
    std::uint64_t low { };

    [[nodiscard]] constexpr bool valid() const noexcept
    {
        return high != 0U || low != 0U;
    }

    friend constexpr auto operator<=>(
        const ScvBackendId&, const ScvBackendId&) = default;
};

struct ScvIslandDomain;
struct ScvHierarchyDomain;
struct ScvObjectDomain;
struct ScvStreamDomain;
struct ScvGeneratorDomain;
struct ScvTransactionDomain;

using ScvIslandId = ScvBackendId<ScvIslandDomain>;
using ScvHierarchyId = ScvBackendId<ScvHierarchyDomain>;
using ScvObjectId = ScvBackendId<ScvObjectDomain>;
using ScvStreamId = ScvBackendId<ScvStreamDomain>;
using ScvGeneratorId = ScvBackendId<ScvGeneratorDomain>;
using ScvTransactionId = ScvBackendId<ScvTransactionDomain>;

struct ScvSequenceId {
    std::uint64_t value { };

    [[nodiscard]] constexpr bool valid() const noexcept { return value != 0U; }

    friend constexpr auto operator<=>(
        const ScvSequenceId&, const ScvSequenceId&) = default;
};

static_assert(std::is_trivially_copyable_v<ScvIslandId>);
static_assert(std::is_trivially_copyable_v<ScvHierarchyId>);
static_assert(std::is_trivially_copyable_v<ScvObjectId>);
static_assert(std::is_trivially_copyable_v<ScvStreamId>);
static_assert(std::is_trivially_copyable_v<ScvGeneratorId>);
static_assert(std::is_trivially_copyable_v<ScvTransactionId>);
static_assert(std::is_trivially_copyable_v<ScvSequenceId>);

struct ScvBackendProtocolLimits {
    std::size_t max_identity_bytes { 4096U };
    std::size_t max_payload_bytes { 1024U * 1024U };
    std::size_t max_message_bytes {
        scv_backend_message_header_bytes + 1024U * 1024U
    };
};

enum class ScvBackendOperation : std::uint16_t {
    handshake = 1,
    create_stream = 2,
    create_generator = 3,
    randomize = 4,
    inspect = 5,
    begin_transaction = 6,
    record_attribute = 7,
    relate_transaction = 8,
    end_transaction = 9,
    flush = 10,
};

enum class ScvBackendDirection : std::uint8_t {
    request = 1,
    receipt = 2,
    event = 3,
};

enum class ScvBackendStatus : std::uint8_t {
    none = 0,
    ok = 1,
    rejected = 2,
    not_found = 3,
    invalid_state = 4,
    resource_exhausted = 5,
    disconnected = 6,
};

enum class ScvBackendFlag : std::uint32_t {
    replayable = 1U << 0U,
    terminal = 1U << 1U,
    selective = 1U << 2U,
};

enum class ScvBackendRegion : std::uint8_t {
    none = 0,
    initialize = 1,
    evaluate = 2,
    update = 3,
    notify = 4,
    quiescent = 5,
    postponed = 6,
};

struct ScvBackendMessageHeader {
    std::uint32_t schema { scv_backend_protocol_version };
    ScvBackendOperation operation { ScvBackendOperation::handshake };
    ScvBackendDirection direction { ScvBackendDirection::request };
    ScvBackendStatus status { ScvBackendStatus::none };
    std::uint32_t flags { };
    ScvIslandId island;
    ScvHierarchyId hierarchy;
    ScvObjectId object;
    ScvStreamId stream;
    ScvGeneratorId generator;
    ScvTransactionId transaction;
    ScvSequenceId sequence;
    ScvSequenceId correlation;
    std::uint64_t time_fs { };
    std::uint64_t delta { };
    ScvBackendRegion region { ScvBackendRegion::none };

    friend bool operator==(
        const ScvBackendMessageHeader&, const ScvBackendMessageHeader&) = default;
};

struct ScvBackendMessage {
    ScvBackendMessageHeader header;
    std::vector<std::byte> payload;

    friend bool operator==(
        const ScvBackendMessage&, const ScvBackendMessage&) = default;
};

[[nodiscard]] std::optional<ScvIslandId> make_scv_island_id(
    std::string_view canonical_identity,
    const ScvBackendProtocolLimits& limits,
    diagnostic::Engine& diagnostics);
[[nodiscard]] std::optional<ScvHierarchyId> make_scv_hierarchy_id(
    ScvIslandId island, std::string_view canonical_path,
    const ScvBackendProtocolLimits& limits,
    diagnostic::Engine& diagnostics);
[[nodiscard]] std::optional<ScvObjectId> make_scv_object_id(
    ScvHierarchyId hierarchy, std::string_view canonical_path,
    const ScvBackendProtocolLimits& limits,
    diagnostic::Engine& diagnostics);
[[nodiscard]] std::optional<ScvStreamId> make_scv_stream_id(
    ScvObjectId object, std::string_view canonical_name,
    const ScvBackendProtocolLimits& limits,
    diagnostic::Engine& diagnostics);
[[nodiscard]] std::optional<ScvGeneratorId> make_scv_generator_id(
    ScvStreamId stream, std::string_view canonical_name,
    const ScvBackendProtocolLimits& limits,
    diagnostic::Engine& diagnostics);
[[nodiscard]] std::optional<ScvSequenceId> make_scv_sequence_id(
    ScvIslandId island, std::uint64_t ordinal,
    diagnostic::Engine& diagnostics);
[[nodiscard]] std::optional<ScvTransactionId> make_scv_transaction_id(
    ScvGeneratorId generator, ScvSequenceId sequence,
    diagnostic::Engine& diagnostics);

[[nodiscard]] bool validate_scv_backend_message(
    const ScvBackendMessage& message,
    const ScvBackendProtocolLimits& limits,
    diagnostic::Engine& diagnostics);
[[nodiscard]] bool scv_backend_message_precedes(
    const ScvBackendMessageHeader& left,
    const ScvBackendMessageHeader& right) noexcept;
[[nodiscard]] std::optional<std::vector<std::byte>>
serialize_scv_backend_message(
    const ScvBackendMessage& message,
    const ScvBackendProtocolLimits& limits,
    diagnostic::Engine& diagnostics);
[[nodiscard]] std::optional<ScvBackendMessage>
deserialize_scv_backend_message(
    std::span<const std::byte> bytes,
    const ScvBackendProtocolLimits& limits,
    diagnostic::Engine& diagnostics);

} // namespace fsim::systemc
