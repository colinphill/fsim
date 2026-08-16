// SPDX-License-Identifier: Apache-2.0

#include "fsim/systemc/scv_recording.hpp"

#include <cassert>
#include <cstdint>
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

fsim::runtime::TransactionTypedValue boolean(const bool value)
{
    return { fsim::runtime::TransactionValueKind::boolean, "bool", 1U, false,
        { }, { value ? 1U : 0U }, { 0U } };
}

fsim::runtime::TransactionTypedValue signed_value(const std::int64_t value)
{
    return { fsim::runtime::TransactionValueKind::signed_integer, "int64_t",
        64U, true, { }, { static_cast<std::uint64_t>(value) }, { 0U } };
}

fsim::runtime::TransactionTypedValue unsigned_value(const std::uint64_t value)
{
    return { fsim::runtime::TransactionValueKind::unsigned_integer, "uint64_t",
        64U, false, { }, { value }, { 0U } };
}

fsim::runtime::TransactionTypedValue string_value(std::string value)
{
    return { fsim::runtime::TransactionValueKind::string, "std::string", 0U,
        false, std::move(value), { }, { } };
}

} // namespace

int main()
{
    using namespace fsim::systemc;
    using fsim::runtime::TransactionRegion;

    fsim::diagnostic::Engine diagnostics;
    ScvNativeRecordingRegistry invalid({ }, "invalid");
    assert(!invalid.valid(diagnostics));
    assert(has_code(diagnostics, "FSIM-SCV-N003"));

    diagnostics.clear();
    ScvNativeRecordingRegistry registry({ 0x173U, 1U }, "native-recording");
    assert(registry.valid(diagnostics));
    const ScvStreamId stream { 0x173U, 2U };
    const ScvGeneratorId generator { 0x173U, 3U };
    assert(registry.create_stream(
        stream, "top.transactions", "TRANSACTOR", diagnostics));
    assert(registry.create_generator(generator, stream, "command",
        "begin.value", "end.value", diagnostics));
    assert(registry.live_streams() == 1U);
    assert(registry.live_generators() == 1U);

    const ScvTransactionId first { 0x173U, 10U };
    const ScvTransactionId second { 0x173U, 11U };
    const ScvNativeTransactionCoordinate begin {
        2U, TransactionRegion::evaluate
    };
    const ScvNativeTransactionCoordinate end {
        2U, TransactionRegion::update
    };
    assert(registry.begin_transaction(first, generator, 11, begin, diagnostics));
    assert(registry.record_attribute(
        first, "accepted", boolean(true), diagnostics));
    assert(registry.record_attribute(
        first, "count", unsigned_value(19U), diagnostics));
    assert(registry.record_attribute(
        first, "label", string_value("first"), diagnostics));
    assert(registry.record_attribute(
        first, "signed", signed_value(-7), diagnostics));
    assert(registry.begin_transaction(second, generator, 22, begin, diagnostics));
    assert(registry.relate_transaction(
        second, "follows", first, diagnostics));
    assert(registry.end_transaction(first, 111, end, diagnostics));
    assert(registry.end_transaction(second, 222, end, diagnostics));
    assert(!diagnostics.has_error());
    assert(registry.native_callback_count() >= 9U);
    assert(registry.records().size() == 2U);
    const auto& first_record = registry.records()[0];
    assert(first_record.stream.high == stream.high);
    assert(first_record.generator.low == generator.low);
    assert(first_record.transaction.low == first.low);
    assert(first_record.begin_delta == 2U);
    assert(first_record.begin_region == TransactionRegion::evaluate);
    assert(first_record.end_region == TransactionRegion::update);
    assert(first_record.attributes.size() == 6U);
    assert(first_record.attributes.front().name == "accepted");
    assert(first_record.attributes.back().name == "signed");
    assert(first_record.attributes[1].name == "begin.value");
    assert(first_record.attributes[2].name == "count");
    assert(first_record.attributes[3].name == "end.value");
    assert(first_record.attributes[4].value.text == "first");
    const auto& second_record = registry.records()[1];
    assert(second_record.relations.size() == 1U);
    assert(second_record.relations[0].name == "follows");
    assert(second_record.relations[0].target.low == first.low);

    const auto completed_count = registry.records().size();
    assert(registry.set_recording(false, diagnostics));
    const ScvTransactionId disabled { 0x173U, 12U };
    assert(registry.begin_transaction(
        disabled, generator, 33, begin, diagnostics));
    assert(registry.record_attribute(
        disabled, "ignored", boolean(false), diagnostics));
    assert(registry.end_transaction(disabled, 333, end, diagnostics));
    assert(registry.records().size() == completed_count);
    assert(registry.set_recording(true, diagnostics));

    diagnostics.clear();
    assert(!registry.end_transaction(first, 0, end, diagnostics));
    assert(has_code(diagnostics, "FSIM-SCV-N001"));
    diagnostics.clear();
    assert(registry.release_transaction(first, diagnostics));
    assert(registry.release_transaction(second, diagnostics));
    assert(registry.release_transaction(disabled, diagnostics));
    assert(registry.live_handles() == 0U);
    auto records = registry.take_records();
    assert(records.size() == 2U && registry.records().empty());

    auto narrow = ScvNativeRecordingLimits { };
    narrow.max_streams = 1U;
    narrow.max_completed_records = 1U;
    ScvNativeRecordingRegistry bounded(
        { 0x173U, 20U }, "bounded-recording", narrow);
    const ScvStreamId bounded_stream { 0x173U, 21U };
    const ScvGeneratorId bounded_generator { 0x173U, 22U };
    assert(bounded.create_stream(
        bounded_stream, "bounded", "TEST", diagnostics));
    diagnostics.clear();
    assert(!bounded.create_stream(
        { 0x173U, 23U }, "excess", "TEST", diagnostics));
    assert(has_code(diagnostics, "FSIM-SCV-N003"));
    diagnostics.clear();
    assert(bounded.create_generator(bounded_generator, bounded_stream,
        "bounded-generator", "begin", "end", diagnostics));
    assert(bounded.begin_transaction(
        { 0x173U, 24U }, bounded_generator, 1, begin, diagnostics));
    assert(bounded.end_transaction(
        { 0x173U, 24U }, 2, end, diagnostics));
    assert(bounded.begin_transaction(
        { 0x173U, 25U }, bounded_generator, 3, begin, diagnostics));
    diagnostics.clear();
    assert(!bounded.end_transaction(
        { 0x173U, 25U }, 4, end, diagnostics));
    assert(has_code(diagnostics, "FSIM-SCV-N003"));
}
