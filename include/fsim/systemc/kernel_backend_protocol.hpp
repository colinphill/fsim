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

struct SystemCKernelIdentityLimits {
    std::size_t max_identity_bytes { 4096U };
};

// Retained for source compatibility with session factory signatures while the
// remaining limit is only used to validate semantic identities.
using SystemCKernelProtocolLimits = SystemCKernelIdentityLimits;

[[nodiscard]] std::optional<SystemCIslandId> make_systemc_island_id(
    std::string_view canonical_identity,
    const SystemCKernelIdentityLimits& limits,
    diagnostic::Engine& diagnostics);
[[nodiscard]] std::optional<SystemCHierarchyId> make_systemc_hierarchy_id(
    SystemCIslandId island, std::string_view canonical_path,
    const SystemCKernelIdentityLimits& limits,
    diagnostic::Engine& diagnostics);
[[nodiscard]] std::optional<SystemCObjectId> make_systemc_object_id(
    SystemCHierarchyId hierarchy, std::string_view canonical_path,
    const SystemCKernelIdentityLimits& limits,
    diagnostic::Engine& diagnostics);
[[nodiscard]] std::optional<SystemCEndpointId> make_systemc_endpoint_id(
    SystemCObjectId object, std::string_view canonical_role,
    const SystemCKernelIdentityLimits& limits,
    diagnostic::Engine& diagnostics);
[[nodiscard]] std::optional<SystemCSequenceId> make_systemc_sequence_id(
    SystemCIslandId island, std::uint64_t ordinal,
    diagnostic::Engine& diagnostics);
[[nodiscard]] std::optional<SystemCTransactionId> make_systemc_transaction_id(
    SystemCEndpointId endpoint, SystemCSequenceId sequence,
    diagnostic::Engine& diagnostics);

} // namespace fsim::systemc
