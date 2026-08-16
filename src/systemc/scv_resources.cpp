// SPDX-License-Identifier: Apache-2.0

#include "fsim/systemc/scv_resources.hpp"

#include "fsim/support/sha256.hpp"
#include "fsim/systemc/scv_backend_transport.hpp"
#include "fsim/systemc/scv_constraints.hpp"
#include "fsim/systemc/scv_recording.hpp"

#include <algorithm>
#include <chrono>
#include <limits>
#include <span>
#include <string>

namespace fsim::systemc {
namespace {

    constexpr std::size_t transaction_adapter_attributes = 2U;

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
            || limits.max_queue_bytes < scv_transport_header_bytes
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
    recording_limits.record_limits.max_message_bytes = limits.max_queue_bytes;

    ScvBackendTransportLimits transport_limits;
    transport_limits.max_message_bytes = limits.max_queue_bytes;
    transport_limits.max_queued_bytes = limits.max_queue_bytes;
    transport_limits.max_queued_records = limits.max_queue_records;
    transport_limits.record_limits = recording_limits.record_limits;

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

    ScvBackendRecordTransport transport(
        ScvBackendTransportKind::direct, std::nullopt, transport_limits);
    support::Sha256 output_hash;
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
        ScvTransportEnvelope envelope {
            island, { static_cast<std::uint64_t>(ordinal) + 1U },
            std::move(records.front())
        };

        if (workload.consumer_failure_at == ordinal) {
            transport.set_connected(false);
            diagnostic::Engine expected_diagnostics;
            const auto failed = transport.send(envelope, expected_diagnostics);
            transport.set_connected(true);
            if (failed.status != ScvBackendTransportStatus::disconnected
                || !expected_diagnostics.has_error()) {
                diagnostics.error("FSIM-SCV-E002",
                    "SCV consumer failure was not contained");
                return std::nullopt;
            }
            ++metrics.consumer_failures;
        }

        diagnostic::Engine transport_diagnostics;
        auto receipt = transport.send(envelope, transport_diagnostics);
        if (receipt.status == ScvBackendTransportStatus::backpressure) {
            ++metrics.backpressure_events;
            static_cast<void>(transport.drain());
            receipt = transport.send(envelope, diagnostics);
        } else if (transport_diagnostics.has_error()) {
            diagnostics.error("FSIM-SCV-E002",
                "SCV resource probe transport failed unexpectedly");
            return std::nullopt;
        }
        if (receipt.status != ScvBackendTransportStatus::accepted) {
            diagnostics.error("FSIM-SCV-E003",
                "SCV resource probe transport budget is exhausted");
            return std::nullopt;
        }
        output_hash.update(std::span<const std::byte>(receipt.bytes));
        ++metrics.transported_transactions;
        metrics.serialized_bytes += receipt.bytes.size();
        metrics.peak_queue_records = std::max(
            metrics.peak_queue_records, transport.queued_records());
        metrics.peak_queue_bytes
            = std::max(metrics.peak_queue_bytes, transport.queued_bytes());
    }

    static_cast<void>(transport.drain());
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
                / sizeof(ScvTransportEnvelope)
        || !add_memory(metrics.approximate_peak_memory_bytes,
            metrics.peak_queue_records * sizeof(ScvTransportEnvelope))) {
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
