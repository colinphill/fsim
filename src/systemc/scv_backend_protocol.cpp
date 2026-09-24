// SPDX-License-Identifier: Apache-2.0

#include "fsim/systemc/scv_backend_protocol.hpp"

#include "fsim/support/sha256.hpp"

#include <algorithm>
#include <limits>
#include <vector>

namespace fsim::systemc {
namespace {

    enum class IdentityDomain : std::uint8_t {
        island = 1,
        hierarchy = 2,
        object = 3,
        stream = 4,
        generator = 5,
        transaction = 6,
    };

    void append_u64(std::vector<std::byte>& bytes, const std::uint64_t value)
    {
        for (unsigned shift = 0; shift < 64U; shift += 8U) {
            bytes.push_back(static_cast<std::byte>((value >> shift) & 0xffU));
        }
    }

    bool valid_limits(
        const ScvBackendProtocolLimits& limits,
        diagnostic::Engine& diagnostics)
    {
        if (limits.max_identity_bytes == 0U
            || limits.max_identity_bytes > std::numeric_limits<std::uint32_t>::max()) {
            diagnostics.error(
                "FSIM-SCV-B003", "SCV backend identity limit is inconsistent");
            return false;
        }
        return true;
    }

    bool valid_identity_text(
        const std::string_view text,
        const ScvBackendProtocolLimits& limits,
        diagnostic::Engine& diagnostics)
    {
        const auto invalid = std::ranges::find_if(text, [](const char value) {
            const auto byte = static_cast<unsigned char>(value);
            return byte < 0x20U || byte == 0x7fU || value == '\\';
        });
        if (text.empty() || text.size() > limits.max_identity_bytes
            || invalid != text.end() || text.front() == ' ' || text.back() == ' ') {
            diagnostics.error(
                "FSIM-SCV-B001",
                "SCV backend identity must be bounded canonical text without "
                "control bytes, backslashes, or surrounding spaces");
            return false;
        }
        return true;
    }

    template <typename Result>
    std::optional<Result> make_identity(
        const IdentityDomain domain,
        const std::uint64_t parent_high,
        const std::uint64_t parent_low,
        const std::string_view canonical_identity,
        const ScvBackendProtocolLimits& limits,
        diagnostic::Engine& diagnostics,
        const std::uint64_t ordinal = 0U)
    {
        if (!valid_limits(limits, diagnostics)
            || !valid_identity_text(canonical_identity, limits, diagnostics)) {
            return std::nullopt;
        }
        std::vector<std::byte> bytes;
        constexpr std::string_view prefix = "fsim-scv-backend-identity-v1";
        bytes.reserve(prefix.size() + 37U + canonical_identity.size());
        bytes.insert(bytes.end(),
            reinterpret_cast<const std::byte*>(prefix.data()),
            reinterpret_cast<const std::byte*>(prefix.data() + prefix.size()));
        bytes.push_back(static_cast<std::byte>(domain));
        append_u64(bytes, parent_high);
        append_u64(bytes, parent_low);
        append_u64(bytes, ordinal);
        const auto identity_size
            = static_cast<std::uint32_t>(canonical_identity.size());
        for (unsigned shift = 0U; shift < 32U; shift += 8U) {
            bytes.push_back(static_cast<std::byte>((identity_size >> shift) & 0xffU));
        }
        bytes.insert(bytes.end(),
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

std::optional<ScvIslandId> make_scv_island_id(
    const std::string_view canonical_identity,
    const ScvBackendProtocolLimits& limits,
    diagnostic::Engine& diagnostics)
{
    return make_identity<ScvIslandId>(
        IdentityDomain::island, 0U, 0U, canonical_identity, limits, diagnostics);
}

std::optional<ScvHierarchyId> make_scv_hierarchy_id(
    const ScvIslandId island,
    const std::string_view canonical_path,
    const ScvBackendProtocolLimits& limits,
    diagnostic::Engine& diagnostics)
{
    if (!island.valid()) {
        diagnostics.error("FSIM-SCV-B001", "SCV hierarchy has no valid island");
        return std::nullopt;
    }
    return make_identity<ScvHierarchyId>(IdentityDomain::hierarchy,
        island.high, island.low, canonical_path, limits, diagnostics);
}

std::optional<ScvObjectId> make_scv_object_id(
    const ScvHierarchyId hierarchy,
    const std::string_view canonical_path,
    const ScvBackendProtocolLimits& limits,
    diagnostic::Engine& diagnostics)
{
    if (!hierarchy.valid()) {
        diagnostics.error("FSIM-SCV-B001", "SCV object has no valid hierarchy");
        return std::nullopt;
    }
    return make_identity<ScvObjectId>(IdentityDomain::object,
        hierarchy.high, hierarchy.low, canonical_path, limits, diagnostics);
}

std::optional<ScvStreamId> make_scv_stream_id(
    const ScvObjectId object,
    const std::string_view canonical_name,
    const ScvBackendProtocolLimits& limits,
    diagnostic::Engine& diagnostics)
{
    if (!object.valid()) {
        diagnostics.error("FSIM-SCV-B001", "SCV stream has no valid object");
        return std::nullopt;
    }
    return make_identity<ScvStreamId>(IdentityDomain::stream,
        object.high, object.low, canonical_name, limits, diagnostics);
}

std::optional<ScvGeneratorId> make_scv_generator_id(
    const ScvStreamId stream,
    const std::string_view canonical_name,
    const ScvBackendProtocolLimits& limits,
    diagnostic::Engine& diagnostics)
{
    if (!stream.valid()) {
        diagnostics.error("FSIM-SCV-B001", "SCV generator has no valid stream");
        return std::nullopt;
    }
    return make_identity<ScvGeneratorId>(IdentityDomain::generator,
        stream.high, stream.low, canonical_name, limits, diagnostics);
}

std::optional<ScvSequenceId> make_scv_sequence_id(
    const ScvIslandId island,
    const std::uint64_t ordinal,
    diagnostic::Engine& diagnostics)
{
    if (!island.valid() || ordinal == 0U) {
        diagnostics.error(
            "FSIM-SCV-B001",
            "SCV sequence requires a valid island and nonzero ordinal");
        return std::nullopt;
    }
    return ScvSequenceId { ordinal };
}

std::optional<ScvTransactionId> make_scv_transaction_id(
    const ScvGeneratorId generator,
    const ScvSequenceId sequence,
    diagnostic::Engine& diagnostics)
{
    if (!generator.valid() || !sequence.valid()) {
        diagnostics.error(
            "FSIM-SCV-B001",
            "SCV transaction requires a valid generator and sequence");
        return std::nullopt;
    }
    ScvBackendProtocolLimits limits;
    return make_identity<ScvTransactionId>(IdentityDomain::transaction,
        generator.high, generator.low, "transaction", limits, diagnostics,
        sequence.value);
}

} // namespace fsim::systemc
