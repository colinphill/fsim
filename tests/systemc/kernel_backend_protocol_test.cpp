// SPDX-License-Identifier: Apache-2.0

#include "fsim/systemc/kernel_backend_protocol.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>
#include <utility>

namespace {

using namespace fsim;

bool has_code(const diagnostic::Engine& diagnostics,
    const std::string_view code)
{
    for (const auto& item : diagnostics.diagnostics()) {
        if (item.code == code) {
            return true;
        }
    }
    return false;
}

} // namespace

int main()
{
    using namespace fsim::systemc;
    static_assert(sizeof(SystemCIslandId) == 16U);
    static_assert(sizeof(SystemCHierarchyId) == 16U);
    static_assert(sizeof(SystemCObjectId) == 16U);
    static_assert(sizeof(SystemCEndpointId) == 16U);
    static_assert(sizeof(SystemCTransactionId) == 16U);
    static_assert(sizeof(SystemCSequenceId) == 8U);
    static_assert(!std::is_convertible_v<SystemCObjectId, SystemCEndpointId>);
    static_assert(!std::is_pointer_v<SystemCIslandId>);

    const SystemCKernelIdentityLimits limits;
    diagnostic::Engine diagnostics;
    const auto island = make_systemc_island_id(
        "design-sha256:root:work.top", limits, diagnostics);
    assert(island && island->valid());
    assert(island->high == 0xcb1bf7d185be2025ULL);
    assert(island->low == 0x90705223c4645f8aULL);

    const auto repeated_island = make_systemc_island_id(
        "design-sha256:root:work.top", limits, diagnostics);
    const auto hierarchy = make_systemc_hierarchy_id(
        *island, "work.top.u_model", limits, diagnostics);
    const auto object = make_systemc_object_id(
        *hierarchy, "work.top.u_model.signal", limits, diagnostics);
    const auto endpoint = make_systemc_endpoint_id(
        *object, "output:value", limits, diagnostics);
    const auto sequence = make_systemc_sequence_id(*island, 17U, diagnostics);
    const auto transaction = make_systemc_transaction_id(
        *endpoint, *sequence, diagnostics);
    assert(repeated_island == island);
    assert(hierarchy && object && endpoint && sequence && transaction);
    assert((std::pair { island->high, island->low }
        != std::pair { hierarchy->high, hierarchy->low }));
    assert(!diagnostics.has_error());

    const auto alternate_object = make_systemc_object_id(
        *hierarchy, "work.top.u_model.other", limits, diagnostics);
    const auto alternate_endpoint = make_systemc_endpoint_id(
        *object, "input:value", limits, diagnostics);
    const auto alternate_sequence = make_systemc_sequence_id(
        *island, 18U, diagnostics);
    const auto alternate_transaction = make_systemc_transaction_id(
        *endpoint, *alternate_sequence, diagnostics);
    assert(alternate_object && *alternate_object != *object);
    assert(alternate_endpoint && *alternate_endpoint != *endpoint);
    assert(alternate_sequence && *alternate_sequence != *sequence);
    assert(alternate_transaction && *alternate_transaction != *transaction);
    assert(!diagnostics.has_error());

    SystemCKernelIdentityLimits short_limit;
    short_limit.max_identity_bytes = 4U;
    diagnostic::Engine invalid_diagnostics;
    assert(!make_systemc_island_id(
        "too-long", short_limit, invalid_diagnostics));
    assert(has_code(invalid_diagnostics, "FSIM-SC-B001"));
    invalid_diagnostics.clear();
    assert(!make_systemc_island_id(
        std::string_view { "bad\0id", 6U }, limits, invalid_diagnostics));
    assert(has_code(invalid_diagnostics, "FSIM-SC-B001"));
    invalid_diagnostics.clear();
    assert(!make_systemc_hierarchy_id(
        { }, "top", limits, invalid_diagnostics));
    assert(has_code(invalid_diagnostics, "FSIM-SC-B001"));
    invalid_diagnostics.clear();
    assert(!make_systemc_sequence_id(*island, 0U, invalid_diagnostics));
    assert(has_code(invalid_diagnostics, "FSIM-SC-B001"));
    invalid_diagnostics.clear();
    assert(!make_systemc_transaction_id({ }, *sequence, invalid_diagnostics));
    assert(has_code(invalid_diagnostics, "FSIM-SC-B001"));
    return 0;
}
