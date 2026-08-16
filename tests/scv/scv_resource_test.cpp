// SPDX-License-Identifier: Apache-2.0

#include "fsim/systemc/scv_resources.hpp"

#include "fsim/support/sha256.hpp"

#include <cassert>
#include <iostream>
#include <string_view>

namespace {

bool has_code(
    const fsim::diagnostic::Engine& diagnostics, const std::string_view code)
{
    for (const auto& diagnostic : diagnostics.diagnostics()) {
        if (diagnostic.code == code)
            return true;
    }
    return false;
}

} // namespace

int main()
{
    using namespace fsim::systemc;

    const ScvIslandId island { 0x173U, 18U };
    ScvResourceLimits limits;
    limits.max_transactions = 128U;
    limits.max_attributes_per_transaction = 8U;
    limits.max_queue_records = 4U;
    limits.max_queue_bytes = 1024U * 1024U;
    limits.max_solver_search_steps = 64U;

    ScvResourceWorkload enabled;
    enabled.transactions = 64U;
    enabled.attributes_per_transaction = 3U;
    enabled.producer_failure_at = 7U;
    enabled.consumer_failure_at = 11U;
    fsim::diagnostic::Engine first_diagnostics;
    const auto first
        = run_scv_resource_probe(island, enabled, limits, first_diagnostics);
    assert(first && !first_diagnostics.has_error());
    assert(first->attempted_transactions == 64U);
    assert(first->recorded_transactions == 64U);
    assert(first->transported_transactions == 64U);
    assert(first->native_callbacks == 64U * 6U);
    assert(first->serialized_bytes > 0U);
    assert(first->backpressure_events > 0U);
    assert(first->producer_failures == 1U);
    assert(first->consumer_failures == 1U);
    assert(first->solver_resource_exhaustions == 1U);
    assert(first->peak_queue_records == limits.max_queue_records);
    assert(first->peak_queue_bytes <= limits.max_queue_bytes);
    assert(first->approximate_peak_memory_bytes >= first->peak_queue_bytes);
    assert(first->elapsed_nanoseconds > 0U);

    fsim::diagnostic::Engine replay_diagnostics;
    const auto replay
        = run_scv_resource_probe(island, enabled, limits, replay_diagnostics);
    assert(replay && !replay_diagnostics.has_error());
    assert(replay->attempted_transactions == first->attempted_transactions);
    assert(replay->recorded_transactions == first->recorded_transactions);
    assert(replay->transported_transactions == first->transported_transactions);
    assert(replay->serialized_bytes == first->serialized_bytes);
    assert(replay->backpressure_events == first->backpressure_events);
    assert(replay->peak_queue_records == first->peak_queue_records);
    assert(replay->peak_queue_bytes == first->peak_queue_bytes);
    assert(replay->output_digest == first->output_digest);

    auto byte_limits = limits;
    byte_limits.max_queue_records = byte_limits.max_transactions;
    byte_limits.max_queue_bytes = 1024U;
    fsim::diagnostic::Engine byte_diagnostics;
    const auto byte_bounded = run_scv_resource_probe(
        island, enabled, byte_limits, byte_diagnostics);
    assert(byte_bounded && !byte_diagnostics.has_error());
    assert(byte_bounded->backpressure_events > 0U);
    assert(byte_bounded->peak_queue_records < byte_limits.max_queue_records);
    assert(byte_bounded->peak_queue_bytes <= byte_limits.max_queue_bytes);
    assert(byte_bounded->output_digest == first->output_digest);

    auto disabled = enabled;
    disabled.producer_failure_at.reset();
    disabled.consumer_failure_at.reset();
    disabled.recording_enabled = false;
    fsim::diagnostic::Engine disabled_diagnostics;
    const auto suppressed = run_scv_resource_probe(
        island, disabled, limits, disabled_diagnostics);
    assert(suppressed && !disabled_diagnostics.has_error());
    assert(suppressed->attempted_transactions == 64U);
    assert(suppressed->recorded_transactions == 0U);
    assert(suppressed->transported_transactions == 0U);
    assert(suppressed->native_callbacks == 64U * 6U);
    assert(suppressed->serialized_bytes == 0U);
    assert(suppressed->backpressure_events == 0U);
    assert(suppressed->peak_queue_records == 0U);
    assert(suppressed->peak_queue_bytes == 0U);
    assert(suppressed->elapsed_nanoseconds > 0U);
    std::cout << "SCV_RESOURCE_BASELINE enabled_transactions="
              << first->transported_transactions
              << " serialized_bytes=" << first->serialized_bytes
              << " peak_queue_records=" << first->peak_queue_records
              << " peak_queue_bytes=" << first->peak_queue_bytes
              << " backpressure_events=" << first->backpressure_events
              << " enabled_elapsed_ns=" << first->elapsed_nanoseconds
              << " disabled_elapsed_ns=" << suppressed->elapsed_nanoseconds
              << " digest="
              << fsim::support::Sha256::hex(first->output_digest) << '\n';

    auto excessive = enabled;
    excessive.transactions = limits.max_transactions + 1U;
    fsim::diagnostic::Engine excessive_diagnostics;
    assert(!run_scv_resource_probe(
        island, excessive, limits, excessive_diagnostics));
    assert(has_code(excessive_diagnostics, "FSIM-SCV-E003"));

    auto malformed = enabled;
    malformed.producer_failure_at = malformed.transactions;
    fsim::diagnostic::Engine malformed_diagnostics;
    assert(!run_scv_resource_probe(
        island, malformed, limits, malformed_diagnostics));
    assert(has_code(malformed_diagnostics, "FSIM-SCV-E001"));

    auto invalid_limits = limits;
    invalid_limits.max_queue_records = 0U;
    fsim::diagnostic::Engine limit_diagnostics;
    assert(!run_scv_resource_probe(
        island, enabled, invalid_limits, limit_diagnostics));
    assert(has_code(limit_diagnostics, "FSIM-SCV-E003"));
}
