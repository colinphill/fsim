// SPDX-License-Identifier: Apache-2.0

#include "fsim/systemc/scv_backend_protocol.hpp"

#include "fsim/support/sha256.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <tuple>

namespace fsim::systemc {
namespace {

    constexpr std::array<std::byte, 8> magic {
        std::byte { 'F' }, std::byte { 'S' }, std::byte { 'I' }, std::byte { 'M' },
        std::byte { 'S' }, std::byte { 'C' }, std::byte { 'V' }, std::byte { 'P' }
    };
    constexpr std::uint32_t known_flags = static_cast<std::uint32_t>(ScvBackendFlag::replayable)
        | static_cast<std::uint32_t>(ScvBackendFlag::terminal)
        | static_cast<std::uint32_t>(ScvBackendFlag::selective);

    enum class IdentityDomain : std::uint8_t {
        island = 1,
        hierarchy = 2,
        object = 3,
        stream = 4,
        generator = 5,
        transaction = 6,
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
    void append_id(std::vector<std::byte>& bytes, const ScvBackendId<Domain> id)
    {
        append_u64(bytes, id.high);
        append_u64(bytes, id.low);
    }

    template <typename Domain>
    ScvBackendId<Domain> read_id(
        std::span<const std::byte> bytes, std::size_t& offset)
    {
        return { read_u64(bytes, offset), read_u64(bytes, offset) };
    }

    bool valid_limits(
        const ScvBackendProtocolLimits& limits,
        diagnostic::Engine& diagnostics)
    {
        if (limits.max_identity_bytes == 0U
            || limits.max_identity_bytes > std::numeric_limits<std::uint32_t>::max()
            || limits.max_payload_bytes == 0U
            || limits.max_payload_bytes > std::numeric_limits<std::uint32_t>::max()
            || limits.max_message_bytes < scv_backend_message_header_bytes
            || limits.max_payload_bytes
                > limits.max_message_bytes - scv_backend_message_header_bytes) {
            diagnostics.error(
                "FSIM-SCV-B003", "SCV backend protocol limits are inconsistent");
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
        append_u32(bytes, static_cast<std::uint32_t>(canonical_identity.size()));
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

    bool known_operation(const ScvBackendOperation operation)
    {
        return operation >= ScvBackendOperation::handshake
            && operation <= ScvBackendOperation::flush;
    }

    bool known_direction(const ScvBackendDirection direction)
    {
        return direction >= ScvBackendDirection::request
            && direction <= ScvBackendDirection::event;
    }

    bool known_status(const ScvBackendStatus status)
    {
        return status >= ScvBackendStatus::none
            && status <= ScvBackendStatus::disconnected;
    }

    bool known_region(const ScvBackendRegion region)
    {
        return region >= ScvBackendRegion::none
            && region <= ScvBackendRegion::postponed;
    }

    bool report_message_error(
        diagnostic::Engine& diagnostics, const std::string_view message)
    {
        diagnostics.error("FSIM-SCV-B002", std::string { message });
        return false;
    }

    bool exact_identity_shape(const ScvBackendMessageHeader& header)
    {
        const auto no_hierarchy = !header.hierarchy.valid()
            && !header.object.valid() && !header.stream.valid()
            && !header.generator.valid() && !header.transaction.valid();
        const auto object_only = header.hierarchy.valid() && header.object.valid()
            && !header.stream.valid() && !header.generator.valid()
            && !header.transaction.valid();
        const auto stream_only = header.hierarchy.valid() && header.object.valid()
            && header.stream.valid() && !header.generator.valid()
            && !header.transaction.valid();
        const auto generator_only = header.hierarchy.valid()
            && header.object.valid() && header.stream.valid()
            && header.generator.valid() && !header.transaction.valid();
        const auto transaction = header.hierarchy.valid() && header.object.valid()
            && header.stream.valid() && header.generator.valid()
            && header.transaction.valid();
        switch (header.operation) {
        case ScvBackendOperation::handshake:
            return !header.island.valid() && no_hierarchy;
        case ScvBackendOperation::create_stream:
            return header.island.valid() && stream_only;
        case ScvBackendOperation::create_generator:
            return header.island.valid() && generator_only;
        case ScvBackendOperation::randomize:
        case ScvBackendOperation::inspect:
            return header.island.valid() && object_only;
        case ScvBackendOperation::begin_transaction:
        case ScvBackendOperation::record_attribute:
        case ScvBackendOperation::relate_transaction:
        case ScvBackendOperation::end_transaction:
            return header.island.valid() && transaction;
        case ScvBackendOperation::flush:
            return header.island.valid() && no_hierarchy;
        }
        return false;
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

bool validate_scv_backend_message(
    const ScvBackendMessage& message,
    const ScvBackendProtocolLimits& limits,
    diagnostic::Engine& diagnostics)
{
    if (!valid_limits(limits, diagnostics)) {
        return false;
    }
    const auto& header = message.header;
    if (header.schema != scv_backend_protocol_version
        || !known_operation(header.operation)
        || !known_direction(header.direction) || !known_status(header.status)
        || !known_region(header.region) || (header.flags & ~known_flags) != 0U) {
        return report_message_error(diagnostics,
            "SCV backend message has an unsupported schema, operation, "
            "direction, status, region, or flag");
    }
    if (!header.sequence.valid()) {
        return report_message_error(
            diagnostics, "SCV backend message requires a nonzero sequence");
    }
    if (header.direction == ScvBackendDirection::request
        && (header.status != ScvBackendStatus::none
            || header.correlation.valid())) {
        return report_message_error(
            diagnostics, "SCV backend request cannot carry status or correlation");
    }
    if (header.direction == ScvBackendDirection::receipt
        && (header.status == ScvBackendStatus::none
            || !header.correlation.valid())) {
        return report_message_error(diagnostics,
            "SCV backend receipt requires status and request correlation");
    }
    if (header.direction == ScvBackendDirection::event
        && header.status != ScvBackendStatus::none) {
        return report_message_error(
            diagnostics, "SCV backend event cannot carry receipt status");
    }
    if (!exact_identity_shape(header)) {
        return report_message_error(diagnostics,
            "SCV backend operation carries missing or extraneous identities");
    }
    if (header.operation == ScvBackendOperation::handshake) {
        if (header.time_fs != 0U || header.delta != 0U
            || header.region != ScvBackendRegion::none) {
            return report_message_error(diagnostics,
                "SCV backend handshake cannot carry simulation coordinates");
        }
    } else if (header.region == ScvBackendRegion::none) {
        return report_message_error(diagnostics,
            "SCV backend operation requires an Accellera scheduling region");
    }
    if (message.payload.size() > limits.max_payload_bytes
        || message.payload.size()
            > limits.max_message_bytes - scv_backend_message_header_bytes) {
        diagnostics.error(
            "FSIM-SCV-B003", "SCV backend message exceeds its payload budget");
        return false;
    }
    return true;
}

bool scv_backend_message_precedes(
    const ScvBackendMessageHeader& left,
    const ScvBackendMessageHeader& right) noexcept
{
    return std::tuple { left.time_fs, left.delta,
        static_cast<std::uint8_t>(left.region), left.sequence.value,
        left.island.high, left.island.low }
    < std::tuple { right.time_fs, right.delta,
          static_cast<std::uint8_t>(right.region), right.sequence.value,
          right.island.high, right.island.low };
}

std::optional<std::vector<std::byte>> serialize_scv_backend_message(
    const ScvBackendMessage& message,
    const ScvBackendProtocolLimits& limits,
    diagnostic::Engine& diagnostics)
{
    if (!validate_scv_backend_message(message, limits, diagnostics)) {
        return std::nullopt;
    }
    std::vector<std::byte> bytes;
    bytes.reserve(scv_backend_message_header_bytes + message.payload.size());
    bytes.insert(bytes.end(), magic.begin(), magic.end());
    append_u32(bytes, message.header.schema);
    append_u16(bytes, static_cast<std::uint16_t>(message.header.operation));
    bytes.push_back(static_cast<std::byte>(message.header.direction));
    bytes.push_back(static_cast<std::byte>(message.header.status));
    append_u32(bytes, message.header.flags);
    append_id(bytes, message.header.island);
    append_id(bytes, message.header.hierarchy);
    append_id(bytes, message.header.object);
    append_id(bytes, message.header.stream);
    append_id(bytes, message.header.generator);
    append_id(bytes, message.header.transaction);
    append_u64(bytes, message.header.sequence.value);
    append_u64(bytes, message.header.correlation.value);
    append_u64(bytes, message.header.time_fs);
    append_u64(bytes, message.header.delta);
    bytes.push_back(static_cast<std::byte>(message.header.region));
    bytes.insert(bytes.end(), 3U, std::byte { 0U });
    append_u32(bytes, static_cast<std::uint32_t>(message.payload.size()));
    append_u32(bytes, 0U);
    if (bytes.size() != scv_backend_message_header_bytes) {
        diagnostics.error(
            "FSIM-SCV-B003", "SCV backend encoder header size is inconsistent");
        return std::nullopt;
    }
    bytes.insert(bytes.end(), message.payload.begin(), message.payload.end());
    return bytes;
}

std::optional<ScvBackendMessage> deserialize_scv_backend_message(
    const std::span<const std::byte> bytes,
    const ScvBackendProtocolLimits& limits,
    diagnostic::Engine& diagnostics)
{
    if (!valid_limits(limits, diagnostics)) {
        return std::nullopt;
    }
    if (bytes.size() < scv_backend_message_header_bytes
        || !std::ranges::equal(magic, bytes.first(magic.size()))) {
        diagnostics.error(
            "FSIM-SCV-B002", "SCV backend message is truncated or has bad magic");
        return std::nullopt;
    }
    std::size_t offset = magic.size();
    ScvBackendMessage result;
    result.header.schema = read_u32(bytes, offset);
    result.header.operation = static_cast<ScvBackendOperation>(
        read_u16(bytes, offset));
    result.header.direction = static_cast<ScvBackendDirection>(
        std::to_integer<std::uint8_t>(bytes[offset++]));
    result.header.status = static_cast<ScvBackendStatus>(
        std::to_integer<std::uint8_t>(bytes[offset++]));
    result.header.flags = read_u32(bytes, offset);
    result.header.island = read_id<ScvIslandDomain>(bytes, offset);
    result.header.hierarchy = read_id<ScvHierarchyDomain>(bytes, offset);
    result.header.object = read_id<ScvObjectDomain>(bytes, offset);
    result.header.stream = read_id<ScvStreamDomain>(bytes, offset);
    result.header.generator = read_id<ScvGeneratorDomain>(bytes, offset);
    result.header.transaction = read_id<ScvTransactionDomain>(bytes, offset);
    result.header.sequence.value = read_u64(bytes, offset);
    result.header.correlation.value = read_u64(bytes, offset);
    result.header.time_fs = read_u64(bytes, offset);
    result.header.delta = read_u64(bytes, offset);
    result.header.region = static_cast<ScvBackendRegion>(
        std::to_integer<std::uint8_t>(bytes[offset++]));
    const auto reserved_byte0 = bytes[offset++];
    const auto reserved_byte1 = bytes[offset++];
    const auto reserved_byte2 = bytes[offset++];
    const auto payload_size = read_u32(bytes, offset);
    const auto reserved = read_u32(bytes, offset);
    if (offset != scv_backend_message_header_bytes
        || reserved_byte0 != std::byte { 0U }
        || reserved_byte1 != std::byte { 0U }
        || reserved_byte2 != std::byte { 0U } || reserved != 0U
        || payload_size > limits.max_payload_bytes
        || payload_size > limits.max_message_bytes - offset
        || bytes.size() != offset + payload_size) {
        diagnostics.error("FSIM-SCV-B003",
            "SCV backend message has reserved data or a mismatched payload size");
        return std::nullopt;
    }
    result.payload.assign(
        bytes.begin() + static_cast<std::ptrdiff_t>(offset), bytes.end());
    if (!validate_scv_backend_message(result, limits, diagnostics)) {
        return std::nullopt;
    }
    return result;
}

} // namespace fsim::systemc
