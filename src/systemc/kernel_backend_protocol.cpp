// SPDX-License-Identifier: Apache-2.0

#include "fsim/systemc/kernel_backend_protocol.hpp"

#include "fsim/support/sha256.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <string>

namespace fsim::systemc {
namespace {

    constexpr std::array<std::byte, 4> kMagic {
        std::byte { static_cast<unsigned char>('F') },
        std::byte { static_cast<unsigned char>('S') },
        std::byte { static_cast<unsigned char>('C') },
        std::byte { static_cast<unsigned char>('K') }
    };
    constexpr std::uint32_t kKnownFlags = static_cast<std::uint32_t>(SystemCKernelMessageFlag::replayable)
        | static_cast<std::uint32_t>(SystemCKernelMessageFlag::terminal);

    enum class IdentityDomain : std::uint8_t {
        island = 1,
        hierarchy = 2,
        object = 3,
        endpoint = 4,
        transaction = 5,
    };

    void append_u16(std::vector<std::byte>& bytes, const std::uint16_t value)
    {
        bytes.push_back(static_cast<std::byte>(value & 0xffU));
        bytes.push_back(static_cast<std::byte>((value >> 8U) & 0xffU));
    }

    void append_u32(std::vector<std::byte>& bytes, const std::uint32_t value)
    {
        for (unsigned shift = 0; shift < 32U; shift += 8U) {
            bytes.push_back(static_cast<std::byte>((value >> shift) & 0xffU));
        }
    }

    void append_u64(std::vector<std::byte>& bytes, const std::uint64_t value)
    {
        for (unsigned shift = 0; shift < 64U; shift += 8U) {
            bytes.push_back(static_cast<std::byte>((value >> shift) & 0xffU));
        }
    }

    std::uint16_t read_u16(std::span<const std::byte> bytes, std::size_t& offset)
    {
        std::uint32_t value { };
        for (unsigned shift = 0; shift < 16U; shift += 8U) {
            value |= static_cast<std::uint32_t>(
                         std::to_integer<std::uint8_t>(bytes[offset++]))
                << shift;
        }
        return static_cast<std::uint16_t>(value);
    }

    std::uint32_t read_u32(std::span<const std::byte> bytes, std::size_t& offset)
    {
        std::uint32_t value { };
        for (unsigned shift = 0; shift < 32U; shift += 8U) {
            value |= static_cast<std::uint32_t>(
                         std::to_integer<std::uint8_t>(bytes[offset++]))
                << shift;
        }
        return value;
    }

    std::uint64_t read_u64(std::span<const std::byte> bytes, std::size_t& offset)
    {
        std::uint64_t value { };
        for (unsigned shift = 0; shift < 64U; shift += 8U) {
            value |= static_cast<std::uint64_t>(
                         std::to_integer<std::uint8_t>(bytes[offset++]))
                << shift;
        }
        return value;
    }

    template <typename Domain>
    void append_id(std::vector<std::byte>& bytes,
        const SystemCBackendId<Domain> id)
    {
        append_u64(bytes, id.high);
        append_u64(bytes, id.low);
    }

    template <typename Domain>
    SystemCBackendId<Domain> read_id(std::span<const std::byte> bytes,
        std::size_t& offset)
    {
        return { read_u64(bytes, offset), read_u64(bytes, offset) };
    }

    bool valid_limits(const SystemCKernelProtocolLimits& limits,
        diagnostic::Engine& diagnostics)
    {
        if (limits.max_identity_bytes == 0U || limits.max_payload_bytes == 0U
            || limits.max_payload_bytes
                > std::numeric_limits<std::uint32_t>::max()
            || limits.max_message_bytes < kSystemCKernelMessageHeaderBytes
            || limits.max_payload_bytes
                > limits.max_message_bytes - kSystemCKernelMessageHeaderBytes) {
            diagnostics.error("FSIM-SC-B003",
                "SystemC backend protocol limits are inconsistent");
            return false;
        }
        return true;
    }

    bool valid_identity_text(std::string_view text,
        const SystemCKernelProtocolLimits& limits,
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
        const SystemCKernelProtocolLimits& limits,
        diagnostic::Engine& diagnostics,
        const std::optional<std::uint64_t> ordinal = std::nullopt)
    {
        if (!valid_limits(limits, diagnostics)
            || !valid_identity_text(canonical_identity, limits, diagnostics)) {
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

    bool known_operation(const SystemCKernelOperation operation)
    {
        return operation >= SystemCKernelOperation::handshake
            && operation <= SystemCKernelOperation::observe_transaction;
    }

    bool known_direction(const SystemCKernelMessageDirection direction)
    {
        return direction >= SystemCKernelMessageDirection::request
            && direction <= SystemCKernelMessageDirection::event;
    }

    bool known_status(const SystemCKernelMessageStatus status)
    {
        return status >= SystemCKernelMessageStatus::none
            && status <= SystemCKernelMessageStatus::disconnected;
    }

    bool report_message_error(diagnostic::Engine& diagnostics,
        const std::string_view message)
    {
        diagnostics.error("FSIM-SC-B002", std::string { message });
        return false;
    }

} // namespace

std::optional<SystemCIslandId> make_systemc_island_id(
    const std::string_view canonical_identity,
    const SystemCKernelProtocolLimits& limits,
    diagnostic::Engine& diagnostics)
{
    return make_identity<SystemCIslandId>(IdentityDomain::island, 0U,
        canonical_identity, limits,
        diagnostics);
}

std::optional<SystemCHierarchyId> make_systemc_hierarchy_id(
    const SystemCIslandId island, const std::string_view canonical_path,
    const SystemCKernelProtocolLimits& limits,
    diagnostic::Engine& diagnostics)
{
    return make_identity<SystemCHierarchyId>(IdentityDomain::hierarchy, island,
        canonical_path, limits,
        diagnostics);
}

std::optional<SystemCObjectId> make_systemc_object_id(
    const SystemCHierarchyId hierarchy, const std::string_view canonical_path,
    const SystemCKernelProtocolLimits& limits,
    diagnostic::Engine& diagnostics)
{
    return make_identity<SystemCObjectId>(IdentityDomain::object, hierarchy,
        canonical_path, limits, diagnostics);
}

std::optional<SystemCEndpointId> make_systemc_endpoint_id(
    const SystemCObjectId object, const std::string_view canonical_role,
    const SystemCKernelProtocolLimits& limits,
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
    SystemCKernelProtocolLimits limits;
    return make_identity<SystemCTransactionId>(
        IdentityDomain::transaction, endpoint, "transaction", limits,
        diagnostics, sequence.value);
}

bool validate_systemc_kernel_message(
    const SystemCKernelMessage& message,
    const SystemCKernelProtocolLimits& limits,
    diagnostic::Engine& diagnostics)
{
    if (!valid_limits(limits, diagnostics)) {
        return false;
    }
    const auto& header = message.header;
    if (header.schema != kSystemCKernelProtocolVersion
        || !known_operation(header.operation)
        || !known_direction(header.direction) || !known_status(header.status)
        || (header.flags & ~kKnownFlags) != 0U) {
        return report_message_error(
            diagnostics,
            "SystemC backend message has an unsupported schema, operation, "
            "direction, status, or flag");
    }
    if (!header.sequence.valid()) {
        return report_message_error(
            diagnostics, "SystemC backend message requires a nonzero sequence");
    }
    if (header.direction == SystemCKernelMessageDirection::request
        && (header.status != SystemCKernelMessageStatus::none
            || header.correlation.valid())) {
        return report_message_error(
            diagnostics,
            "SystemC backend request cannot carry status or correlation");
    }
    if (header.direction == SystemCKernelMessageDirection::response
        && (header.status == SystemCKernelMessageStatus::none
            || !header.correlation.valid())) {
        return report_message_error(
            diagnostics,
            "SystemC backend response requires status and request correlation");
    }
    if (header.direction == SystemCKernelMessageDirection::event
        && header.status != SystemCKernelMessageStatus::none) {
        return report_message_error(
            diagnostics, "SystemC backend event cannot carry response status");
    }
    if (header.operation == SystemCKernelOperation::handshake) {
        if (header.island.valid() || header.hierarchy.valid()
            || header.object.valid() || header.endpoint.valid()
            || header.transaction.valid()) {
            return report_message_error(
                diagnostics,
                "SystemC backend handshake cannot carry kernel object identities");
        }
    } else if (!header.island.valid()) {
        return report_message_error(
            diagnostics,
            "SystemC backend operation requires a nonzero island identity");
    }
    if ((header.hierarchy.valid() && !header.island.valid())
        || (header.object.valid() && !header.hierarchy.valid())
        || (header.endpoint.valid() && !header.object.valid())
        || (header.transaction.valid() && !header.endpoint.valid())) {
        return report_message_error(
            diagnostics,
            "SystemC backend message contains an incomplete identity chain");
    }
    if (header.operation == SystemCKernelOperation::create_object
        && (!header.hierarchy.valid() || !header.object.valid())) {
        return report_message_error(
            diagnostics,
            "SystemC backend object construction requires hierarchy and object "
            "identities");
    }
    if ((header.operation == SystemCKernelOperation::bind_endpoint
            || header.operation == SystemCKernelOperation::apply_inputs
            || header.operation == SystemCKernelOperation::drain_outputs)
        && !header.endpoint.valid()) {
        return report_message_error(
            diagnostics,
            "SystemC backend endpoint operation requires an endpoint identity");
    }
    if (header.operation == SystemCKernelOperation::observe_transaction
        && !header.transaction.valid()) {
        return report_message_error(
            diagnostics,
            "SystemC backend transaction operation requires a transaction identity");
    }
    if (message.payload.size() > limits.max_payload_bytes
        || message.payload.size()
            > limits.max_message_bytes - kSystemCKernelMessageHeaderBytes) {
        diagnostics.error("FSIM-SC-B003",
            "SystemC backend message exceeds its payload or message "
            "resource limit");
        return false;
    }
    return true;
}

std::optional<std::vector<std::byte>> serialize_systemc_kernel_message(
    const SystemCKernelMessage& message,
    const SystemCKernelProtocolLimits& limits,
    diagnostic::Engine& diagnostics)
{
    if (!validate_systemc_kernel_message(message, limits, diagnostics)) {
        return std::nullopt;
    }
    std::vector<std::byte> bytes;
    bytes.reserve(kSystemCKernelMessageHeaderBytes + message.payload.size());
    bytes.insert(bytes.end(), kMagic.begin(), kMagic.end());
    append_u32(bytes, message.header.schema);
    append_u16(bytes, static_cast<std::uint16_t>(message.header.operation));
    bytes.push_back(static_cast<std::byte>(message.header.direction));
    bytes.push_back(static_cast<std::byte>(message.header.status));
    append_u32(bytes, message.header.flags);
    append_id(bytes, message.header.island);
    append_id(bytes, message.header.hierarchy);
    append_id(bytes, message.header.object);
    append_id(bytes, message.header.endpoint);
    append_id(bytes, message.header.transaction);
    append_u64(bytes, message.header.sequence.value);
    append_u64(bytes, message.header.correlation.value);
    append_u32(bytes, static_cast<std::uint32_t>(message.payload.size()));
    append_u32(bytes, 0U);
    append_u32(bytes, 0U);
    append_u32(bytes, 0U);
    if (bytes.size() != kSystemCKernelMessageHeaderBytes) {
        diagnostics.error("FSIM-SC-B003",
            "SystemC backend encoder header size is inconsistent");
        return std::nullopt;
    }
    bytes.insert(bytes.end(), message.payload.begin(), message.payload.end());
    return bytes;
}

std::optional<SystemCKernelMessage> deserialize_systemc_kernel_message(
    const std::span<const std::byte> bytes,
    const SystemCKernelProtocolLimits& limits,
    diagnostic::Engine& diagnostics)
{
    if (!valid_limits(limits, diagnostics)) {
        return std::nullopt;
    }
    if (bytes.size() < kSystemCKernelMessageHeaderBytes
        || !std::ranges::equal(kMagic, bytes.first(kMagic.size()))) {
        diagnostics.error("FSIM-SC-B002",
            "SystemC backend message is truncated or has bad magic");
        return std::nullopt;
    }
    std::size_t offset = kMagic.size();
    SystemCKernelMessage result;
    result.header.schema = read_u32(bytes, offset);
    result.header.operation = static_cast<SystemCKernelOperation>(read_u16(bytes, offset));
    result.header.direction = static_cast<SystemCKernelMessageDirection>(
        std::to_integer<std::uint8_t>(bytes[offset++]));
    result.header.status = static_cast<SystemCKernelMessageStatus>(
        std::to_integer<std::uint8_t>(bytes[offset++]));
    result.header.flags = read_u32(bytes, offset);
    result.header.island = read_id<SystemCIslandDomain>(bytes, offset);
    result.header.hierarchy = read_id<SystemCHierarchyDomain>(bytes, offset);
    result.header.object = read_id<SystemCObjectDomain>(bytes, offset);
    result.header.endpoint = read_id<SystemCEndpointDomain>(bytes, offset);
    result.header.transaction = read_id<SystemCTransactionDomain>(bytes, offset);
    result.header.sequence.value = read_u64(bytes, offset);
    result.header.correlation.value = read_u64(bytes, offset);
    const auto payload_size = read_u32(bytes, offset);
    const auto reserved0 = read_u32(bytes, offset);
    const auto reserved1 = read_u32(bytes, offset);
    const auto reserved2 = read_u32(bytes, offset);
    if (offset != kSystemCKernelMessageHeaderBytes
        || reserved0 != 0U || reserved1 != 0U || reserved2 != 0U
        || payload_size > limits.max_payload_bytes
        || payload_size > limits.max_message_bytes - offset
        || bytes.size() != offset + payload_size) {
        diagnostics.error(
            "FSIM-SC-B003",
            "SystemC backend message has a mismatched or over-budget payload size");
        return std::nullopt;
    }
    result.payload.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
        bytes.end());
    if (!validate_systemc_kernel_message(result, limits, diagnostics)) {
        return std::nullopt;
    }
    return result;
}

} // namespace fsim::systemc
