// SPDX-License-Identifier: Apache-2.0

#include "fsim/systemc/scv_resources.hpp"

#include "fsim/runtime/transaction_record.hpp"
#include "fsim/support/sha256.hpp"
#include "fsim/systemc/scv_constraints.hpp"
#include "fsim/systemc/scv_recording.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <deque>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::systemc {
namespace {

    constexpr std::size_t transaction_adapter_attributes = 2U;
    // Retain the established byte-budget reservation without emitting a frame.
    constexpr std::size_t queue_entry_reservation_bytes = 64U;
    constexpr std::string_view typed_record_digest_domain
        = "fsim-scv-typed-record-v1|";

    struct ResourceQueueEntry {
        ScvIslandId island;
        ScvSequenceId sequence;
        runtime::TransactionRecord record;
        std::size_t accounted_bytes { };
    };

    enum class ResourceQueueSendStatus : std::uint8_t {
        accepted,
        backpressure,
        disconnected,
        rejected,
    };

    struct ResourceQueueSendResult {
        ResourceQueueSendStatus status { ResourceQueueSendStatus::rejected };
        std::vector<std::byte> record_payload;
    };

    class ResourceQueue {
    public:
        ResourceQueue(const std::size_t max_records,
            const std::size_t max_bytes,
            const runtime::TransactionRecordLimits& record_limits)
            : max_records_(max_records)
            , max_bytes_(max_bytes)
            , record_limits_(record_limits)
        {
        }

        ResourceQueueSendResult send(const ScvIslandId island,
            const ScvSequenceId sequence,
            runtime::TransactionRecord& record,
            diagnostic::Engine& diagnostics)
        {
            if (!connected_) {
                diagnostics.error("FSIM-SCV-E002",
                    "SCV resource queue consumer is disconnected");
                return { ResourceQueueSendStatus::disconnected, { } };
            }
            if (!island.valid() || !sequence.valid()) {
                diagnostics.error("FSIM-SCV-E002",
                    "SCV resource queue identity is invalid");
                return { ResourceQueueSendStatus::rejected, { } };
            }

            auto payload = runtime::serialize_transaction_record(
                record, record_limits_, diagnostics);
            if (!payload)
                return { ResourceQueueSendStatus::rejected, { } };
            if (payload->size()
                > std::numeric_limits<std::size_t>::max()
                    - queue_entry_reservation_bytes) {
                diagnostics.error("FSIM-SCV-E003",
                    "SCV resource queue byte accounting overflowed");
                return { ResourceQueueSendStatus::rejected, { } };
            }
            const auto accounted_bytes
                = queue_entry_reservation_bytes + payload->size();
            if (accounted_bytes > max_bytes_)
                return { ResourceQueueSendStatus::rejected, { } };
            if (queue_.size() >= max_records_
                || accounted_bytes > max_bytes_ - queued_bytes_) {
                return { ResourceQueueSendStatus::backpressure, { } };
            }

            queue_.push_back({ island, sequence, std::move(record),
                accounted_bytes });
            queued_bytes_ += accounted_bytes;
            return { ResourceQueueSendStatus::accepted, std::move(*payload) };
        }

        void set_connected(const bool connected) noexcept
        {
            connected_ = connected;
        }

        void drain() noexcept
        {
            queue_.clear();
            queued_bytes_ = 0U;
        }

        [[nodiscard]] std::size_t queued_records() const noexcept
        {
            return queue_.size();
        }

        [[nodiscard]] std::size_t queued_bytes() const noexcept
        {
            return queued_bytes_;
        }

        [[nodiscard]] static constexpr std::size_t entry_size_bytes() noexcept
        {
            return sizeof(ResourceQueueEntry);
        }

    private:
        std::size_t max_records_ { };
        std::size_t max_bytes_ { };
        runtime::TransactionRecordLimits record_limits_;
        std::deque<ResourceQueueEntry> queue_;
        std::size_t queued_bytes_ { };
        bool connected_ { true };
    };

    bool bounded(const std::size_t value)
    {
        return value != 0U
            && value <= std::numeric_limits<std::uint32_t>::max();
    }

    bool valid_probe(const ScvIslandId island,
        const ScvResourceWorkload& workload, const ScvResourceLimits& limits,
        diagnostic::Engine& diagnostics)
    {
        if (!island.valid() || workload.transactions == 0U
            || (workload.producer_failure_at
                && *workload.producer_failure_at >= workload.transactions)
            || (workload.consumer_failure_at
                && *workload.consumer_failure_at >= workload.transactions)) {
            diagnostics.error("FSIM-SCV-E001",
                "SCV resource probe identity or workload is invalid");
            return false;
        }
        if (!bounded(limits.max_transactions)
            || !bounded(limits.max_attributes_per_transaction)
            || !bounded(limits.max_queue_records)
            || !bounded(limits.max_queue_bytes)
            || limits.max_queue_bytes < queue_entry_reservation_bytes
            || limits.max_solver_search_steps == 0U
            || workload.transactions > limits.max_transactions
            || workload.attributes_per_transaction
                > limits.max_attributes_per_transaction
            || workload.attributes_per_transaction
                > std::numeric_limits<std::size_t>::max()
                    - transaction_adapter_attributes) {
            diagnostics.error("FSIM-SCV-E003",
                "SCV resource probe limit is inconsistent or exhausted");
            return false;
        }
        return true;
    }

    runtime::TransactionTypedValue boolean_value(const bool value)
    {
        return { runtime::TransactionValueKind::boolean, "bool", 1U, false,
            { }, { value ? 1U : 0U }, { 0U } };
    }

    bool exercise_solver_limit(const std::uint64_t search_limit)
    {
        ScvNativeSmartPtrRegistry registry;
        diagnostic::Engine setup_diagnostics;
        const auto aggregate = registry.create(ScvNativeValueKind::aggregate,
            { 0x173U, 0xe001U }, "resource.constraints", 17318U,
            setup_diagnostics);
        if (!aggregate || setup_diagnostics.has_error())
            return false;

        const std::vector<ScvNativeExtensionStep> path {
            { ScvNativeExtensionStepKind::field, 0U }
        };
        if (!registry.assign_signed(*aggregate, path, 0, setup_diagnostics))
            return false;

        ScvConstraintRequest request;
        request.variables = {
            { "resource.constraints.value", *aggregate, path, { 1, 2, 3 } },
        };
        request.clauses = {
            { "value-is-one", { { 0U, 1 } },
                ScvConstraintRelation::equal, 1, false },
            { "value-is-three", { { 0U, 1 } },
                ScvConstraintRelation::equal, 3, false },
        };
        request.limits.max_search_steps = std::min<std::uint64_t>(
            search_limit, 1U);
        diagnostic::Engine expected_diagnostics;
        const auto result = solve_scv_constraints(
            registry, request, expected_diagnostics);
        diagnostic::Engine release_diagnostics;
        const bool released
            = registry.release(*aggregate, release_diagnostics);
        return result.status == ScvConstraintStatus::resource_exhausted
            && result.assignments.empty() && expected_diagnostics.has_error()
            && released && !release_diagnostics.has_error()
            && registry.live_handles() == 0U;
    }

    bool add_memory(std::size_t& result, const std::size_t value)
    {
        if (value > std::numeric_limits<std::size_t>::max() - result)
            return false;
        result += value;
        return true;
    }

    void hash_u64_le(support::Sha256& hash, const std::uint64_t value)
    {
        std::array<std::byte, sizeof(value)> bytes { };
        for (std::size_t index = 0U; index < bytes.size(); ++index) {
            const auto shift = static_cast<unsigned>(index * 8U);
            bytes[index] = static_cast<std::byte>(
                static_cast<unsigned char>((value >> shift) & 0xffU));
        }
        hash.update(bytes);
    }

    void hash_resource_record(support::Sha256& hash, const ScvIslandId island,
        const ScvSequenceId sequence, const std::span<const std::byte> payload)
    {
        hash_u64_le(hash, island.high);
        hash_u64_le(hash, island.low);
        hash_u64_le(hash, sequence.value);
        hash_u64_le(hash, static_cast<std::uint64_t>(payload.size()));
        hash.update(payload);
    }

} // namespace

std::optional<ScvResourceMetrics> run_scv_resource_probe(
    const ScvIslandId island, const ScvResourceWorkload& workload,
    const ScvResourceLimits& limits, diagnostic::Engine& diagnostics)
{
    const auto started = std::chrono::steady_clock::now();
    if (!valid_probe(island, workload, limits, diagnostics))
        return std::nullopt;

    const auto record_attributes
        = workload.attributes_per_transaction + transaction_adapter_attributes;
    ScvNativeRecordingLimits recording_limits;
    recording_limits.max_streams = 1U;
    recording_limits.max_generators = 1U;
    recording_limits.max_handles = 1U;
    recording_limits.max_completed_records = 1U;
    recording_limits.max_attributes_per_transaction = record_attributes;
    recording_limits.record_limits.max_attributes = record_attributes;
    recording_limits.record_limits.max_message_bytes
        = limits.max_queue_bytes - queue_entry_reservation_bytes;

    ScvNativeRecordingRegistry recording(
        island, "fsim.scv.resource.probe", recording_limits);
    const ScvStreamId stream { island.high != 0U ? island.high : 1U,
        island.low ^ 0x17318U };
    const ScvGeneratorId generator {
        island.high != 0U ? island.high : 1U, island.low ^ 0x27318U
    };
    if (!recording.valid(diagnostics)
        || !recording.create_stream(
            stream, "resource.transactions", "RESOURCE", diagnostics)
        || !recording.create_generator(generator, stream, "operation",
            "begin.value", "end.value", diagnostics)
        || !recording.set_recording(
            workload.recording_enabled, diagnostics)) {
        diagnostics.error("FSIM-SCV-E002",
            "SCV resource probe could not initialize native recording");
        return std::nullopt;
    }

    ResourceQueue queue(limits.max_queue_records, limits.max_queue_bytes,
        recording_limits.record_limits);
    support::Sha256 output_hash;
    output_hash.update(typed_record_digest_domain);
    ScvResourceMetrics metrics;
    const ScvNativeTransactionCoordinate begin {
        0U, runtime::TransactionRegion::evaluate
    };
    const ScvNativeTransactionCoordinate end {
        0U, runtime::TransactionRegion::update
    };

    for (std::size_t ordinal = 0U; ordinal < workload.transactions;
        ++ordinal) {
        ++metrics.attempted_transactions;
        if (workload.producer_failure_at == ordinal) {
            diagnostic::Engine expected_diagnostics;
            if (recording.begin_transaction({ }, generator, 0, begin,
                    expected_diagnostics)
                || !expected_diagnostics.has_error()) {
                diagnostics.error("FSIM-SCV-E002",
                    "SCV producer failure was not contained");
                return std::nullopt;
            }
            ++metrics.producer_failures;
        }

        const ScvTransactionId transaction {
            island.high != 0U ? island.high : 1U,
            static_cast<std::uint64_t>(ordinal) + 1U
        };
        if (!recording.begin_transaction(transaction, generator,
                static_cast<std::int64_t>(ordinal), begin, diagnostics)) {
            diagnostics.error("FSIM-SCV-E002",
                "SCV resource probe failed to begin a transaction");
            return std::nullopt;
        }
        for (std::size_t attribute = 0U;
            attribute < workload.attributes_per_transaction; ++attribute) {
            if (!recording.record_attribute(transaction,
                    "attribute." + std::to_string(attribute),
                    boolean_value(((ordinal + attribute) & 1U) != 0U),
                    diagnostics)) {
                diagnostics.error("FSIM-SCV-E002",
                    "SCV resource probe failed to record an attribute");
                return std::nullopt;
            }
        }
        if (!recording.end_transaction(transaction,
                static_cast<std::int64_t>(ordinal) + 1, end, diagnostics)
            || !recording.release_transaction(transaction, diagnostics)) {
            diagnostics.error("FSIM-SCV-E002",
                "SCV resource probe failed to end or release a transaction");
            return std::nullopt;
        }

        auto records = recording.take_records();
        if (!workload.recording_enabled) {
            if (!records.empty()) {
                diagnostics.error("FSIM-SCV-E002",
                    "disabled SCV recording published a transaction");
                return std::nullopt;
            }
            continue;
        }
        if (records.size() != 1U) {
            diagnostics.error("FSIM-SCV-E002",
                "enabled SCV recording did not publish one transaction");
            return std::nullopt;
        }
        ++metrics.recorded_transactions;
        const ScvSequenceId sequence {
            static_cast<std::uint64_t>(ordinal) + 1U
        };

        if (workload.consumer_failure_at == ordinal) {
            queue.set_connected(false);
            diagnostic::Engine expected_diagnostics;
            const auto failed = queue.send(
                island, sequence, records.front(), expected_diagnostics);
            queue.set_connected(true);
            if (failed.status != ResourceQueueSendStatus::disconnected
                || !expected_diagnostics.has_error()) {
                diagnostics.error("FSIM-SCV-E002",
                    "SCV consumer failure was not contained");
                return std::nullopt;
            }
            ++metrics.consumer_failures;
        }

        auto result = queue.send(island, sequence, records.front(), diagnostics);
        if (result.status == ResourceQueueSendStatus::backpressure) {
            ++metrics.backpressure_events;
            queue.drain();
            result = queue.send(island, sequence, records.front(), diagnostics);
        }
        if (result.status != ResourceQueueSendStatus::accepted) {
            diagnostics.error("FSIM-SCV-E003",
                "SCV resource probe transport budget is exhausted");
            return std::nullopt;
        }
        hash_resource_record(output_hash, island, sequence,
            std::span<const std::byte>(result.record_payload));
        ++metrics.transported_transactions;
        if (result.record_payload.size()
            > std::numeric_limits<std::uint64_t>::max()
                - metrics.record_payload_bytes) {
            diagnostics.error("FSIM-SCV-E003",
                "SCV resource record-byte metric overflowed");
            return std::nullopt;
        }
        metrics.record_payload_bytes += result.record_payload.size();
        metrics.peak_queue_records = std::max(
            metrics.peak_queue_records, queue.queued_records());
        metrics.peak_queue_bytes
            = std::max(metrics.peak_queue_bytes, queue.queued_bytes());
    }

    queue.drain();
    metrics.native_callbacks = recording.native_callback_count();
    if (recording.live_handles() != 0U || !exercise_solver_limit(limits.max_solver_search_steps)) {
        diagnostics.error("FSIM-SCV-E002",
            "SCV resource probe failed solver or ownership recovery");
        return std::nullopt;
    }
    metrics.solver_resource_exhaustions = 1U;

    metrics.approximate_peak_memory_bytes = metrics.peak_queue_bytes;
    if (metrics.peak_queue_records
            > std::numeric_limits<std::size_t>::max()
                / ResourceQueue::entry_size_bytes()
        || !add_memory(metrics.approximate_peak_memory_bytes,
            metrics.peak_queue_records * ResourceQueue::entry_size_bytes())) {
        diagnostics.error("FSIM-SCV-E003",
            "SCV resource probe memory accounting overflowed");
        return std::nullopt;
    }
    metrics.output_digest = output_hash.finish();
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - started)
                             .count();
    metrics.elapsed_nanoseconds
        = static_cast<std::uint64_t>(std::max<std::int64_t>(elapsed, 1));
    return metrics;
}

} // namespace fsim::systemc
