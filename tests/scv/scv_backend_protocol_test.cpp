// SPDX-License-Identifier: Apache-2.0

#include "fsim/systemc/scv_backend_protocol.hpp"
#include "fsim/systemc/scv_resources.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace {

using namespace fsim::systemc;

struct Identities {
    ScvIslandId island;
    ScvHierarchyId hierarchy;
    ScvObjectId object;
    ScvStreamId stream;
    ScvGeneratorId generator;
    ScvSequenceId sequence;
    ScvTransactionId transaction;
};

Identities make_identities()
{
    ScvBackendProtocolLimits limits;
    fsim::diagnostic::Engine diagnostics;
    const auto island = make_scv_island_id("island:primary", limits, diagnostics);
    const auto hierarchy = make_scv_hierarchy_id(
        *island, "work.top", limits, diagnostics);
    const auto object = make_scv_object_id(
        *hierarchy, "work.top.controller", limits, diagnostics);
    const auto stream = make_scv_stream_id(
        *object, "requests", limits, diagnostics);
    const auto generator = make_scv_generator_id(
        *stream, "request-generator", limits, diagnostics);
    const auto sequence = make_scv_sequence_id(*island, 17U, diagnostics);
    const auto transaction = make_scv_transaction_id(
        *generator, *sequence, diagnostics);
    assert(island && hierarchy && object && stream && generator && sequence
        && transaction && !diagnostics.has_error());
    return { *island, *hierarchy, *object, *stream, *generator, *sequence,
        *transaction };
}

bool has_code(
    const fsim::diagnostic::Engine& diagnostics,
    const std::string_view code)
{
    return std::ranges::any_of(
        diagnostics.diagnostics(), [&](const auto& diagnostic) {
            return diagnostic.code == code;
        });
}

} // namespace

int main()
{
    static_assert(sizeof(ScvIslandId) == 16U);
    static_assert(sizeof(ScvHierarchyId) == 16U);
    static_assert(sizeof(ScvObjectId) == 16U);
    static_assert(sizeof(ScvStreamId) == 16U);
    static_assert(sizeof(ScvGeneratorId) == 16U);
    static_assert(sizeof(ScvTransactionId) == 16U);

    const ScvResourceLimits resource_limits;
    assert(resource_limits.max_transactions == 1024U * 1024U);
    assert(resource_limits.max_queue_bytes == 64U * 1024U * 1024U);
    assert(resource_limits.max_solver_search_steps == 1024U * 1024U);

    const auto ids = make_identities();
    const auto repeated = make_identities();
    assert(ids.island == repeated.island);
    assert(ids.hierarchy == repeated.hierarchy);
    assert(ids.object == repeated.object);
    assert(ids.stream == repeated.stream);
    assert(ids.generator == repeated.generator);
    assert(ids.transaction == repeated.transaction);

    ScvBackendProtocolLimits limits;
    fsim::diagnostic::Engine alternate_diagnostics;
    const auto alternate_hierarchy = make_scv_hierarchy_id(
        ids.island, "work.other", limits, alternate_diagnostics);
    const auto alternate_stream = make_scv_stream_id(
        ids.object, "responses", limits, alternate_diagnostics);
    const auto alternate_sequence = make_scv_sequence_id(
        ids.island, 18U, alternate_diagnostics);
    const auto alternate_transaction = make_scv_transaction_id(
        ids.generator, *alternate_sequence, alternate_diagnostics);
    assert(alternate_hierarchy && alternate_stream && alternate_sequence
        && alternate_transaction && !alternate_diagnostics.has_error());
    assert(*alternate_hierarchy != ids.hierarchy);
    assert(*alternate_stream != ids.stream);
    assert(*alternate_transaction != ids.transaction);
    assert(ids.object.high != ids.stream.high || ids.object.low != ids.stream.low);

    fsim::diagnostic::Engine identity_diagnostics;
    assert(!make_scv_island_id({ }, limits, identity_diagnostics));
    assert(!make_scv_island_id(" leading", limits, identity_diagnostics));
    assert(!make_scv_hierarchy_id(
        { }, "work.top", limits, identity_diagnostics));
    assert(!make_scv_object_id(
        ids.hierarchy, "work\\top", limits, identity_diagnostics));
    assert(!make_scv_stream_id(
        ids.object, std::string(4097U, 's'), limits, identity_diagnostics));
    assert(!make_scv_generator_id(
        { }, "generator", limits, identity_diagnostics));
    assert(!make_scv_sequence_id(ids.island, 0U, identity_diagnostics));
    assert(!make_scv_transaction_id(
        ids.generator, { }, identity_diagnostics));
    assert(has_code(identity_diagnostics, "FSIM-SCV-B001"));

    ScvBackendProtocolLimits zero_limit;
    zero_limit.max_identity_bytes = 0U;
    fsim::diagnostic::Engine limits_diagnostics;
    assert(!make_scv_island_id("island:invalid-limit", zero_limit,
        limits_diagnostics));
    assert(has_code(limits_diagnostics, "FSIM-SCV-B003"));

    if constexpr (std::numeric_limits<std::size_t>::digits
        > std::numeric_limits<std::uint32_t>::digits) {
        ScvBackendProtocolLimits oversized_limit;
        oversized_limit.max_identity_bytes
            = static_cast<std::size_t>(
                  std::numeric_limits<std::uint32_t>::max())
            + 1U;
        fsim::diagnostic::Engine oversized_limit_diagnostics;
        assert(!make_scv_island_id("island:invalid-limit", oversized_limit,
            oversized_limit_diagnostics));
        assert(has_code(oversized_limit_diagnostics, "FSIM-SCV-B003"));
    }
}
