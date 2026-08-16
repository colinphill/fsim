// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/diagnostic/diagnostic.hpp"
#include "fsim/runtime/transaction_record.hpp"
#include "fsim/systemc/scv_backend_protocol.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace fsim::systemc {

inline constexpr std::uint32_t scv_transport_schema_version = 1U;
inline constexpr std::size_t scv_transport_header_bytes = 64U;

struct ScvTransportEnvelope {
    ScvIslandId island;
    ScvSequenceId sequence;
    runtime::TransactionRecord record;
    friend bool operator==(
        const ScvTransportEnvelope&, const ScvTransportEnvelope&) = default;
};

struct ScvBackendTransportLimits {
    std::size_t max_message_bytes { 4U * 1024U * 1024U };
    std::size_t max_queued_bytes { 64U * 1024U * 1024U };
    std::size_t max_queued_records { 65536U };
    runtime::TransactionRecordLimits record_limits;
};

enum class ScvBackendTransportKind : std::uint8_t {
    direct = 1,
    worker_loopback = 2,
};

enum class ScvBackendTransportStatus : std::uint8_t {
    accepted = 1,
    backpressure = 2,
    disconnected = 3,
    worker_crashed = 4,
    rejected = 5,
};

struct ScvBackendTransportReceipt {
    ScvBackendTransportStatus status { ScvBackendTransportStatus::rejected };
    std::vector<std::byte> bytes;
};

[[nodiscard]] std::optional<std::vector<std::byte>>
serialize_scv_transport_envelope(
    const ScvTransportEnvelope& envelope,
    const ScvBackendTransportLimits& limits,
    diagnostic::Engine& diagnostics);
[[nodiscard]] std::optional<ScvTransportEnvelope>
deserialize_scv_transport_envelope(
    std::span<const std::byte> bytes,
    const ScvBackendTransportLimits& limits,
    diagnostic::Engine& diagnostics);
[[nodiscard]] bool scv_transport_envelope_precedes(
    const ScvTransportEnvelope& left,
    const ScvTransportEnvelope& right) noexcept;
[[nodiscard]] std::optional<std::vector<ScvTransportEnvelope>>
merge_scv_transport_envelopes(
    std::span<const ScvTransportEnvelope> envelopes,
    const ScvBackendTransportLimits& limits,
    diagnostic::Engine& diagnostics);

class ScvBackendRecordTransport {
public:
    ScvBackendRecordTransport(
        ScvBackendTransportKind kind,
        std::optional<ScvIslandId> worker_island,
        ScvBackendTransportLimits limits = { });
    ~ScvBackendRecordTransport();
    ScvBackendRecordTransport(ScvBackendRecordTransport&&) noexcept;
    ScvBackendRecordTransport& operator=(
        ScvBackendRecordTransport&&) noexcept;
    ScvBackendRecordTransport(const ScvBackendRecordTransport&) = delete;
    ScvBackendRecordTransport& operator=(
        const ScvBackendRecordTransport&) = delete;

    [[nodiscard]] ScvBackendTransportReceipt send(
        const ScvTransportEnvelope& envelope,
        diagnostic::Engine& diagnostics);
    [[nodiscard]] ScvBackendTransportReceipt replay(
        std::span<const std::byte> bytes,
        diagnostic::Engine& diagnostics);
    [[nodiscard]] std::vector<ScvTransportEnvelope> drain();
    void set_connected(bool connected) noexcept;
    void set_worker_crashed(bool crashed) noexcept;

    [[nodiscard]] std::size_t queued_records() const noexcept;
    [[nodiscard]] std::size_t queued_bytes() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace fsim::systemc
