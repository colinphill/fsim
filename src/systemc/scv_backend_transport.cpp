// SPDX-License-Identifier: Apache-2.0

#include "fsim/systemc/scv_backend_transport.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <tuple>
#include <utility>

namespace fsim::systemc {
namespace {

    constexpr std::array<std::byte, 8> magic {
        std::byte { 'F' }, std::byte { 'S' }, std::byte { 'I' }, std::byte { 'M' },
        std::byte { 'S' }, std::byte { 'C' }, std::byte { 'V' }, std::byte { '1' }
    };

    class Writer {
    public:
        void u8(const std::uint8_t value) { bytes.push_back(std::byte { value }); }
        void u32(const std::uint32_t value)
        {
            for (unsigned shift = 0U; shift < 32U; shift += 8U)
                u8(static_cast<std::uint8_t>(value >> shift));
        }
        void u64(const std::uint64_t value)
        {
            for (unsigned shift = 0U; shift < 64U; shift += 8U)
                u8(static_cast<std::uint8_t>(value >> shift));
        }
        std::vector<std::byte> bytes;
    };

    class Reader {
    public:
        explicit Reader(const std::span<const std::byte> value)
            : bytes(value)
        {
        }
        std::optional<std::uint8_t> u8()
        {
            if (offset >= bytes.size())
                return std::nullopt;
            return std::to_integer<std::uint8_t>(bytes[offset++]);
        }
        std::optional<std::uint32_t> u32()
        {
            std::uint32_t result { };
            for (unsigned shift = 0U; shift < 32U; shift += 8U) {
                const auto value = u8();
                if (!value)
                    return std::nullopt;
                result |= static_cast<std::uint32_t>(*value) << shift;
            }
            return result;
        }
        std::optional<std::uint64_t> u64()
        {
            std::uint64_t result { };
            for (unsigned shift = 0U; shift < 64U; shift += 8U) {
                const auto value = u8();
                if (!value)
                    return std::nullopt;
                result |= static_cast<std::uint64_t>(*value) << shift;
            }
            return result;
        }
        std::span<const std::byte> bytes;
        std::size_t offset { };
    };

    bool valid_limits(const ScvBackendTransportLimits& limits)
    {
        const auto bounded = [](const std::size_t value) {
            return value != 0U
                && value <= std::numeric_limits<std::uint32_t>::max();
        };
        return bounded(limits.max_message_bytes)
            && bounded(limits.max_queued_bytes)
            && bounded(limits.max_queued_records)
            && limits.max_message_bytes >= scv_transport_header_bytes
            && limits.max_queued_bytes >= limits.max_message_bytes;
    }

} // namespace

std::optional<std::vector<std::byte>> serialize_scv_transport_envelope(
    const ScvTransportEnvelope& envelope,
    const ScvBackendTransportLimits& limits,
    diagnostic::Engine& diagnostics)
{
    if (!valid_limits(limits)) {
        diagnostics.error(
            "FSIM-SCV-W003", "SCV transport limits are inconsistent");
        return std::nullopt;
    }
    if (!envelope.island.valid() || !envelope.sequence.valid()) {
        diagnostics.error(
            "FSIM-SCV-W001", "SCV transport envelope identity is invalid");
        return std::nullopt;
    }
    const auto record = runtime::serialize_transaction_record(
        envelope.record, limits.record_limits, diagnostics);
    if (!record || record->size() > limits.max_message_bytes - scv_transport_header_bytes) {
        diagnostics.error(
            "FSIM-SCV-W001", "SCV transport record is invalid or oversized");
        return std::nullopt;
    }
    Writer writer;
    writer.bytes.insert(writer.bytes.end(), magic.begin(), magic.end());
    writer.u32(scv_transport_schema_version);
    writer.u32(0U);
    writer.u64(envelope.island.high);
    writer.u64(envelope.island.low);
    writer.u64(envelope.sequence.value);
    writer.u64(static_cast<std::uint64_t>(record->size()));
    writer.u64(0U);
    writer.u64(0U);
    writer.bytes.insert(writer.bytes.end(), record->begin(), record->end());
    return writer.bytes;
}

std::optional<ScvTransportEnvelope> deserialize_scv_transport_envelope(
    const std::span<const std::byte> bytes,
    const ScvBackendTransportLimits& limits,
    diagnostic::Engine& diagnostics)
{
    if (!valid_limits(limits)) {
        diagnostics.error(
            "FSIM-SCV-W003", "SCV transport limits are inconsistent");
        return std::nullopt;
    }
    if (bytes.size() < scv_transport_header_bytes
        || bytes.size() > limits.max_message_bytes
        || !std::ranges::equal(magic, bytes.first(magic.size()))) {
        diagnostics.error(
            "FSIM-SCV-W001", "SCV transport magic or framing is invalid");
        return std::nullopt;
    }
    Reader reader(bytes.subspan(magic.size()));
    const auto schema = reader.u32();
    const auto reserved = reader.u32();
    const auto island_high = reader.u64();
    const auto island_low = reader.u64();
    const auto sequence = reader.u64();
    const auto size = reader.u64();
    const auto reserved2 = reader.u64();
    const auto reserved3 = reader.u64();
    if (!schema || !reserved || !island_high || !island_low || !sequence || !size
        || !reserved2 || !reserved3 || *schema != scv_transport_schema_version
        || *reserved != 0U || *reserved2 != 0U || *reserved3 != 0U
        || *size != bytes.size() - scv_transport_header_bytes) {
        diagnostics.error(
            "FSIM-SCV-W001", "SCV transport header or payload size is invalid");
        return std::nullopt;
    }
    ScvTransportEnvelope result;
    result.island = { *island_high, *island_low };
    result.sequence = { *sequence };
    if (!result.island.valid() || !result.sequence.valid()) {
        diagnostics.error(
            "FSIM-SCV-W001", "SCV transport header identity is invalid");
        return std::nullopt;
    }
    auto record = runtime::deserialize_transaction_record(
        bytes.subspan(scv_transport_header_bytes), limits.record_limits,
        diagnostics);
    if (!record) {
        diagnostics.error(
            "FSIM-SCV-W001", "SCV transport payload record is invalid");
        return std::nullopt;
    }
    result.record = std::move(*record);
    return result;
}

bool scv_transport_envelope_precedes(const ScvTransportEnvelope& left,
    const ScvTransportEnvelope& right) noexcept
{
    return std::tie(left.record.begin_time_fs, left.record.begin_delta,
               left.record.begin_region, left.island, left.sequence)
        < std::tie(right.record.begin_time_fs, right.record.begin_delta,
            right.record.begin_region, right.island, right.sequence);
}

std::optional<std::vector<ScvTransportEnvelope>> merge_scv_transport_envelopes(
    const std::span<const ScvTransportEnvelope> envelopes,
    const ScvBackendTransportLimits& limits,
    diagnostic::Engine& diagnostics)
{
    if (!valid_limits(limits) || envelopes.size() > limits.max_queued_records) {
        diagnostics.error(
            "FSIM-SCV-W003", "SCV transport merge limit is inconsistent or exceeded");
        return std::nullopt;
    }
    std::vector<ScvTransportEnvelope> result(envelopes.begin(), envelopes.end());
    std::map<ScvIslandId, std::vector<std::uint64_t>> sequences;
    for (const auto& envelope : result) {
        if (!envelope.island.valid() || !envelope.sequence.valid()
            || !runtime::validate_transaction_record(
                envelope.record, limits.record_limits, diagnostics)) {
            diagnostics.error(
                "FSIM-SCV-W002", "SCV transport merge input is invalid");
            return std::nullopt;
        }
        auto& island_sequences = sequences[envelope.island];
        if (std::ranges::find(island_sequences, envelope.sequence.value)
            != island_sequences.end()) {
            diagnostics.error("FSIM-SCV-W002",
                "SCV transport merge has a duplicate island sequence");
            return std::nullopt;
        }
        island_sequences.push_back(envelope.sequence.value);
    }
    std::ranges::sort(result, scv_transport_envelope_precedes);
    return result;
}

struct ScvBackendRecordTransport::Impl {
    ScvBackendTransportReceipt accept(std::vector<std::byte> bytes,
        diagnostic::Engine& diagnostics)
    {
        if (!connected) {
            diagnostics.error(
                "FSIM-SCV-W003", "SCV transport is disconnected");
            return { ScvBackendTransportStatus::disconnected, { } };
        }
        if (worker_crashed) {
            diagnostics.error(
                "FSIM-SCV-W003", "SCV worker loopback is crashed");
            return { ScvBackendTransportStatus::worker_crashed, { } };
        }
        auto envelope
            = deserialize_scv_transport_envelope(bytes, limits, diagnostics);
        if (!envelope)
            return { ScvBackendTransportStatus::rejected, { } };
        if (kind == ScvBackendTransportKind::worker_loopback
            && (!worker_island || envelope->island != *worker_island)) {
            diagnostics.error(
                "FSIM-SCV-W002", "SCV loopback received a foreign island");
            return { ScvBackendTransportStatus::rejected, { } };
        }
        const auto last = last_sequence.find(envelope->island);
        if (last != last_sequence.end() && envelope->sequence.value <= last->second) {
            diagnostics.error("FSIM-SCV-W002",
                "SCV transport island sequence is duplicate or regressing");
            return { ScvBackendTransportStatus::rejected, { } };
        }
        if (queue.size() >= limits.max_queued_records
            || bytes.size() > limits.max_queued_bytes - queued_byte_count) {
            diagnostics.error(
                "FSIM-SCV-W003", "SCV transport queue is applying backpressure");
            return { ScvBackendTransportStatus::backpressure, { } };
        }
        queued_byte_count += bytes.size();
        last_sequence.insert_or_assign(
            envelope->island, envelope->sequence.value);
        queue.push_back(std::move(*envelope));
        return { ScvBackendTransportStatus::accepted, std::move(bytes) };
    }

    ScvBackendTransportKind kind { ScvBackendTransportKind::direct };
    std::optional<ScvIslandId> worker_island;
    ScvBackendTransportLimits limits;
    std::vector<ScvTransportEnvelope> queue;
    std::map<ScvIslandId, std::uint64_t> last_sequence;
    std::size_t queued_byte_count { };
    bool connected { true };
    bool worker_crashed { };
};

ScvBackendRecordTransport::ScvBackendRecordTransport(
    const ScvBackendTransportKind kind,
    const std::optional<ScvIslandId> worker_island,
    ScvBackendTransportLimits limits)
    : impl_(std::make_unique<Impl>())
{
    impl_->kind = kind;
    impl_->worker_island = worker_island;
    impl_->limits = std::move(limits);
}

ScvBackendRecordTransport::~ScvBackendRecordTransport() = default;
ScvBackendRecordTransport::ScvBackendRecordTransport(
    ScvBackendRecordTransport&&) noexcept
    = default;
ScvBackendRecordTransport& ScvBackendRecordTransport::operator=(
    ScvBackendRecordTransport&&) noexcept
    = default;

ScvBackendTransportReceipt ScvBackendRecordTransport::send(
    const ScvTransportEnvelope& envelope, diagnostic::Engine& diagnostics)
{
    if (!impl_ || (impl_->kind != ScvBackendTransportKind::direct && impl_->kind != ScvBackendTransportKind::worker_loopback)
        || (impl_->kind == ScvBackendTransportKind::worker_loopback
            && (!impl_->worker_island || !impl_->worker_island->valid()))) {
        diagnostics.error(
            "FSIM-SCV-W002", "SCV transport kind or worker ownership is invalid");
        return { ScvBackendTransportStatus::rejected, { } };
    }
    auto bytes
        = serialize_scv_transport_envelope(envelope, impl_->limits, diagnostics);
    if (!bytes)
        return { ScvBackendTransportStatus::rejected, { } };
    return impl_->accept(std::move(*bytes), diagnostics);
}

ScvBackendTransportReceipt ScvBackendRecordTransport::replay(
    const std::span<const std::byte> bytes, diagnostic::Engine& diagnostics)
{
    if (!impl_ || (impl_->kind != ScvBackendTransportKind::direct && impl_->kind != ScvBackendTransportKind::worker_loopback)
        || (impl_->kind == ScvBackendTransportKind::worker_loopback
            && (!impl_->worker_island || !impl_->worker_island->valid()))) {
        diagnostics.error(
            "FSIM-SCV-W002", "SCV transport kind or worker ownership is invalid");
        return { ScvBackendTransportStatus::rejected, { } };
    }
    return impl_->accept(std::vector<std::byte>(bytes.begin(), bytes.end()), diagnostics);
}

std::vector<ScvTransportEnvelope> ScvBackendRecordTransport::drain()
{
    if (!impl_)
        return { };
    auto result = std::move(impl_->queue);
    impl_->queue.clear();
    impl_->queued_byte_count = 0U;
    return result;
}

void ScvBackendRecordTransport::set_connected(const bool connected) noexcept
{
    if (impl_)
        impl_->connected = connected;
}

void ScvBackendRecordTransport::set_worker_crashed(const bool crashed) noexcept
{
    if (impl_)
        impl_->worker_crashed = crashed;
}

std::size_t ScvBackendRecordTransport::queued_records() const noexcept
{
    return impl_ ? impl_->queue.size() : 0U;
}

std::size_t ScvBackendRecordTransport::queued_bytes() const noexcept
{
    return impl_ ? impl_->queued_byte_count : 0U;
}

} // namespace fsim::systemc
