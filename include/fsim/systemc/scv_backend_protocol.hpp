// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/diagnostic/diagnostic.hpp"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <type_traits>

namespace fsim::systemc {

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

} // namespace fsim::systemc
