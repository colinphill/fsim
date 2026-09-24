// SPDX-License-Identifier: Apache-2.0

#include "fsim/systemc/kernel_backend_protocol.hpp"

#include "fsim/support/sha256.hpp"

#include <algorithm>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::systemc {
namespace {

    enum class IdentityDomain : std::uint8_t {
        island = 1,
        hierarchy = 2,
        object = 3,
        endpoint = 4,
        transaction = 5,
    };

    void append_u64(std::vector<std::byte>& bytes, const std::uint64_t value)
    {
        for (unsigned shift = 0; shift < 64U; shift += 8U) {
            bytes.push_back(static_cast<std::byte>((value >> shift) & 0xffU));
        }
    }

    void append_u32(std::vector<std::byte>& bytes, const std::uint32_t value)
    {
        for (unsigned shift = 0; shift < 32U; shift += 8U) {
            bytes.push_back(static_cast<std::byte>((value >> shift) & 0xffU));
        }
    }

    bool valid_identity_text(std::string_view text,
        const SystemCKernelIdentityLimits& limits,
        diagnostic::Engine& diagnostics)
    {
        const auto invalid_byte = std::ranges::find_if(text, [](const char value) {
            const auto byte = static_cast<unsigned char>(value);
            return byte < 0x20U || byte == 0x7fU || value == '\\';
        });
        if (text.empty() || text.size() > limits.max_identity_bytes
            || invalid_byte != text.end()
            || text.front() == ' ' || text.back() == ' ') {
            diagnostics.error(
                "FSIM-SC-B001",
                "SystemC backend identity must be bounded canonical text without "
                "control bytes, backslashes, or surrounding spaces");
            return false;
        }
        return true;
    }

    template <typename Result, typename Parent>
    std::optional<Result> make_identity(
        const IdentityDomain domain, const Parent parent,
        const std::string_view canonical_identity,
        const SystemCKernelIdentityLimits& limits,
        diagnostic::Engine& diagnostics,
        const std::optional<std::uint64_t> ordinal = std::nullopt)
    {
        if (limits.max_identity_bytes == 0U
            || limits.max_identity_bytes > static_cast<std::size_t>(
                std::numeric_limits<std::uint32_t>::max())) {
            diagnostics.error("FSIM-SC-B001",
                "SystemC backend identity size limit is outside its supported range");
            return std::nullopt;
        }
        if (!valid_identity_text(canonical_identity, limits, diagnostics)) {
            return std::nullopt;
        }
        if constexpr (requires { parent.valid(); }) {
            if (!parent.valid()) {
                diagnostics.error("FSIM-SC-B001",
                    "SystemC backend child identity has no valid parent");
                return std::nullopt;
            }
        }
        std::vector<std::byte> bytes;
        bytes.reserve(48U + canonical_identity.size());
        constexpr std::string_view prefix = "fsim-systemc-kernel-identity-v1";
        bytes.insert(bytes.end(),
            reinterpret_cast<const std::byte*>(prefix.data()),
            reinterpret_cast<const std::byte*>(prefix.data() + prefix.size()));
        bytes.push_back(static_cast<std::byte>(domain));
        if constexpr (requires { parent.high; parent.low; }) {
            append_u64(bytes, parent.high);
            append_u64(bytes, parent.low);
        } else {
            append_u64(bytes, 0U);
            append_u64(bytes, 0U);
        }
        append_u64(bytes, ordinal.value_or(0U));
        append_u32(bytes, static_cast<std::uint32_t>(canonical_identity.size()));
        bytes.insert(
            bytes.end(),
            reinterpret_cast<const std::byte*>(canonical_identity.data()),
            reinterpret_cast<const std::byte*>(
                canonical_identity.data() + canonical_identity.size()));
        const auto digest = support::Sha256::digest(bytes);
        Result result;
        for (std::size_t index = 0; index < 8U; ++index) {
            result.high = (result.high << 8U) | digest[index];
            result.low = (result.low << 8U) | digest[index + 8U];
        }
        if (!result.valid()) {
            result.low = 1U;
        }
        return result;
    }

} // namespace

std::optional<SystemCIslandId> make_systemc_island_id(
    const std::string_view canonical_identity,
    const SystemCKernelIdentityLimits& limits,
    diagnostic::Engine& diagnostics)
{
    return make_identity<SystemCIslandId>(IdentityDomain::island, 0U,
        canonical_identity, limits,
        diagnostics);
}

std::optional<SystemCHierarchyId> make_systemc_hierarchy_id(
    const SystemCIslandId island, const std::string_view canonical_path,
    const SystemCKernelIdentityLimits& limits,
    diagnostic::Engine& diagnostics)
{
    return make_identity<SystemCHierarchyId>(IdentityDomain::hierarchy, island,
        canonical_path, limits,
        diagnostics);
}

std::optional<SystemCObjectId> make_systemc_object_id(
    const SystemCHierarchyId hierarchy, const std::string_view canonical_path,
    const SystemCKernelIdentityLimits& limits,
    diagnostic::Engine& diagnostics)
{
    return make_identity<SystemCObjectId>(IdentityDomain::object, hierarchy,
        canonical_path, limits, diagnostics);
}

std::optional<SystemCEndpointId> make_systemc_endpoint_id(
    const SystemCObjectId object, const std::string_view canonical_role,
    const SystemCKernelIdentityLimits& limits,
    diagnostic::Engine& diagnostics)
{
    return make_identity<SystemCEndpointId>(IdentityDomain::endpoint, object,
        canonical_role, limits, diagnostics);
}

std::optional<SystemCSequenceId> make_systemc_sequence_id(
    const SystemCIslandId island, const std::uint64_t ordinal,
    diagnostic::Engine& diagnostics)
{
    if (!island.valid() || ordinal == 0U) {
        diagnostics.error("FSIM-SC-B001",
            "SystemC backend sequence requires an island and "
            "nonzero ordinal");
        return std::nullopt;
    }
    return SystemCSequenceId { ordinal };
}

std::optional<SystemCTransactionId> make_systemc_transaction_id(
    const SystemCEndpointId endpoint, const SystemCSequenceId sequence,
    diagnostic::Engine& diagnostics)
{
    if (!endpoint.valid() || !sequence.valid()) {
        diagnostics.error("FSIM-SC-B001",
            "SystemC backend transaction requires an endpoint and "
            "sequence");
        return std::nullopt;
    }
    SystemCKernelIdentityLimits limits;
    return make_identity<SystemCTransactionId>(
        IdentityDomain::transaction, endpoint, "transaction", limits,
        diagnostics, sequence.value);
}

} // namespace fsim::systemc
