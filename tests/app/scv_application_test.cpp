// SPDX-License-Identifier: Apache-2.0

#include "fsim/systemc/scv_backend_protocol.hpp"

#include <cassert>

int main()
{
    using namespace fsim::systemc;

    ScvBackendProtocolLimits limits;
    fsim::diagnostic::Engine diagnostics;
    const auto island = make_scv_island_id(
        "application:island", limits, diagnostics);
    assert(island && !diagnostics.has_error());

    const auto hierarchy = make_scv_hierarchy_id(
        *island, "application.module", limits, diagnostics);
    const auto object = make_scv_object_id(
        *hierarchy, "application.module.recording", limits, diagnostics);
    const auto stream = make_scv_stream_id(
        *object, "transactions", limits, diagnostics);
    const auto generator = make_scv_generator_id(
        *stream, "operation", limits, diagnostics);
    const auto sequence = make_scv_sequence_id(*island, 1U, diagnostics);
    const auto transaction = make_scv_transaction_id(
        *generator, *sequence, diagnostics);
    assert(hierarchy && object && stream && generator && sequence);
    assert(transaction && transaction->valid());
    assert(!diagnostics.has_error());
}
