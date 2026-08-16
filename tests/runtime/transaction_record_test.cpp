// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/transaction_record.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <string_view>

namespace {

bool has_code(
    const fsim::diagnostic::Engine& diagnostics,
    const std::string_view code)
{
    for (const auto& diagnostic : diagnostics.diagnostics()) {
        if (diagnostic.code == code)
            return true;
    }
    return false;
}

fsim::runtime::TransactionRecord record()
{
    using namespace fsim::runtime;
    TransactionRecord result;
    result.stream = { 1U, 10U };
    result.generator = { 2U, 20U };
    result.transaction = { 3U, 30U };
    result.begin_time_fs = 1000U;
    result.begin_delta = 4U;
    result.begin_region = TransactionRegion::evaluate;
    result.end_time_fs = 2000U;
    result.end_delta = 7U;
    result.end_region = TransactionRegion::postponed;
    result.attributes = {
        { "count", { TransactionValueKind::unsigned_integer, "uint64", 64U, false, { }, { 42U }, { 0U } } },
        { "name", { TransactionValueKind::string, "std::string", 0U, false, "payload", { }, { } } },
        { "state", { TransactionValueKind::logic_vector, "sc_lv<65>", 65U, false, { }, { 1U, 1U }, { 2U, 0U } } },
    };
    result.relations = {
        { "child", { 5U, 50U } },
        { "parent", { 4U, 40U } },
    };
    result.correlated_objects = {
        { TransactionObjectDomain::systemc, { 7U, 70U } },
        { TransactionObjectDomain::tlm1, { 8U, 80U } },
        { TransactionObjectDomain::tlm2, { 9U, 90U } },
    };
    return result;
}

} // namespace

int main()
{
    using namespace fsim::runtime;

    TransactionRecordLimits limits;
    auto value = record();
    fsim::diagnostic::Engine diagnostics;
    assert(validate_transaction_record(value, limits, diagnostics));
    const auto bytes = serialize_transaction_record(value, limits, diagnostics);
    const auto repeated = serialize_transaction_record(value, limits, diagnostics);
    assert(bytes && repeated == bytes && !diagnostics.has_error());
    const auto decoded = deserialize_transaction_record(
        *bytes, limits, diagnostics);
    assert(decoded == value && !diagnostics.has_error());

    auto later = value;
    later.transaction.low += 1U;
    assert(transaction_record_precedes(value, later));
    later = value;
    later.begin_delta += 1U;
    assert(transaction_record_precedes(value, later));

    auto invalid = value;
    invalid.schema = 2U;
    fsim::diagnostic::Engine schema_diagnostics;
    assert(!serialize_transaction_record(invalid, limits, schema_diagnostics));
    assert(has_code(schema_diagnostics, "FSIM-SCV-T001"));
    invalid = value;
    invalid.end_time_fs = 10U;
    fsim::diagnostic::Engine time_diagnostics;
    assert(!validate_transaction_record(invalid, limits, time_diagnostics));
    assert(has_code(time_diagnostics, "FSIM-SCV-T002"));
    invalid = value;
    std::swap(invalid.attributes[0], invalid.attributes[1]);
    fsim::diagnostic::Engine order_diagnostics;
    assert(!validate_transaction_record(invalid, limits, order_diagnostics));
    assert(has_code(order_diagnostics, "FSIM-SCV-T002"));
    invalid = value;
    invalid.attributes[2].value.aval_words.back() = 2U;
    fsim::diagnostic::Engine high_diagnostics;
    assert(!validate_transaction_record(invalid, limits, high_diagnostics));
    assert(has_code(high_diagnostics, "FSIM-SCV-T002"));
    invalid = value;
    invalid.attributes[0].value.kind = TransactionValueKind::bit_vector;
    invalid.attributes[0].value.bval_words[0] = 1U;
    fsim::diagnostic::Engine plane_diagnostics;
    assert(!validate_transaction_record(invalid, limits, plane_diagnostics));
    assert(has_code(plane_diagnostics, "FSIM-SCV-T002"));

    auto small_limits = limits;
    small_limits.max_attributes = 2U;
    fsim::diagnostic::Engine resource_diagnostics;
    assert(!validate_transaction_record(value, small_limits, resource_diagnostics));
    assert(has_code(resource_diagnostics, "FSIM-SCV-T003"));

    auto corrupt = *bytes;
    corrupt.front() = std::byte { 0U };
    fsim::diagnostic::Engine magic_diagnostics;
    assert(!deserialize_transaction_record(corrupt, limits, magic_diagnostics));
    assert(has_code(magic_diagnostics, "FSIM-SCV-T001"));
    corrupt = *bytes;
    corrupt[8] = std::byte { 2U };
    fsim::diagnostic::Engine future_diagnostics;
    assert(!deserialize_transaction_record(corrupt, limits, future_diagnostics));
    assert(has_code(future_diagnostics, "FSIM-SCV-T001"));
    corrupt = *bytes;
    corrupt[12] = std::byte { 1U };
    fsim::diagnostic::Engine reserved_diagnostics;
    assert(!deserialize_transaction_record(corrupt, limits, reserved_diagnostics));
    assert(has_code(reserved_diagnostics, "FSIM-SCV-T002"));
    corrupt = *bytes;
    corrupt.pop_back();
    fsim::diagnostic::Engine truncated_diagnostics;
    assert(!deserialize_transaction_record(corrupt, limits, truncated_diagnostics));
    assert(has_code(truncated_diagnostics, "FSIM-SCV-T002"));
    corrupt = *bytes;
    corrupt.push_back(std::byte { 0U });
    fsim::diagnostic::Engine trailing_diagnostics;
    assert(!deserialize_transaction_record(corrupt, limits, trailing_diagnostics));
    assert(has_code(trailing_diagnostics, "FSIM-SCV-T002"));
}
