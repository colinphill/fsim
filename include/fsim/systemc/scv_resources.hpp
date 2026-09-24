// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/diagnostic/diagnostic.hpp"
#include "fsim/systemc/scv_backend_protocol.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace fsim::systemc {

struct ScvResourceLimits {
    std::size_t max_transactions { 1024U * 1024U };
    std::size_t max_attributes_per_transaction { 4096U };
    std::size_t max_queue_records { 65536U };
    // Queue bytes are record-payload bytes plus a fixed 64-byte entry reservation.
    std::size_t max_queue_bytes { 64U * 1024U * 1024U };
    std::uint64_t max_solver_search_steps { 1024U * 1024U };
};

struct ScvResourceWorkload {
    std::size_t transactions { };
    std::size_t attributes_per_transaction { };
    std::optional<std::size_t> producer_failure_at;
    std::optional<std::size_t> consumer_failure_at;
    bool recording_enabled { true };
};

struct ScvResourceMetrics {
    std::uint64_t attempted_transactions { };
    std::uint64_t recorded_transactions { };
    std::uint64_t transported_transactions { };
    std::uint64_t native_callbacks { };
    // Bytes in the standalone persisted TransactionRecord encoding.
    std::uint64_t record_payload_bytes { };
    std::uint64_t backpressure_events { };
    std::uint64_t producer_failures { };
    std::uint64_t consumer_failures { };
    std::uint64_t solver_resource_exhaustions { };
    std::size_t peak_queue_records { };
    // Per-entry 64-byte reservation plus record payload bytes; never wire bytes.
    std::size_t peak_queue_bytes { };
    std::size_t approximate_peak_memory_bytes { };
    std::uint64_t elapsed_nanoseconds { };
    // SHA-256 typed-record-v1: domain tag, then ordered LE island/sequence/size
    // values and standalone TransactionRecord payload bytes.
    std::array<std::uint8_t, 32> output_digest { };
};

[[nodiscard]] std::optional<ScvResourceMetrics> run_scv_resource_probe(
    ScvIslandId island,
    const ScvResourceWorkload& workload,
    const ScvResourceLimits& limits,
    diagnostic::Engine& diagnostics);

} // namespace fsim::systemc
