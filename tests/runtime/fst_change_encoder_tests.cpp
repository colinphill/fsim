// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/fst_change_encoder.hpp"

#include <cassert>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

template <typename Exception, typename Function>
void expect_exception(Function&& function)
{
    bool rejected = false;
    try {
        function();
    } catch (const Exception&) {
        rejected = true;
    }
    assert(rejected);
}

[[nodiscard]] fsim::runtime::FstEncodedValue logic(std::string_view symbols)
{
    return fsim::runtime::encode_fst_logic_value(
        fsim::runtime::PackedLogic4::from_msb_string(symbols));
}

[[nodiscard]] fsim::runtime::TraceEvent event(
    const std::uint64_t signal,
    const fsim::runtime::SimulationTick time,
    const std::uint64_t delta,
    const fsim::runtime::TraceRegion region,
    const std::uint64_t sequence)
{
    return { { signal }, time, delta, region, sequence };
}

void test_canonical_order_preserves_every_value()
{
    using namespace fsim::runtime;
    FstChangeEncoder encoder;
    encoder.append(event(2U, 11U, 0U, TraceRegion::Active, 4U), logic("0"));
    encoder.append(event(1U, 10U, 1U, TraceRegion::Active, 3U), logic("1"));
    encoder.append(event(1U, 10U, 0U, TraceRegion::Observed, 2U), logic("x"));
    encoder.append(event(2U, 10U, 0U, TraceRegion::Active, 2U), logic("z"));
    encoder.append(event(1U, 10U, 0U, TraceRegion::Active, 2U), logic("0"));
    encoder.append(event(1U, 10U, 0U, TraceRegion::Active, 1U), logic("1"));
    encoder.append(event(1U, 10U, 0U, TraceRegion::Snapshot, 0U), logic("x"));

    const auto ordered = std::move(encoder).freeze();
    const auto values = ordered.values();
    assert(values.size() == 7U);
    assert(ordered.timestamp_count() == 2U);
    assert(ordered.payload_bytes() == 7U);

    assert(values[0].event.region == TraceRegion::Snapshot);
    assert(values[1].event.sequence == 1U);
    assert(values[2].event.signal == TraceSignalId { 1U });
    assert(values[3].event.signal == TraceSignalId { 2U });
    assert(values[4].event.region == TraceRegion::Observed);
    assert(values[5].event.delta == 1U);
    assert(values[6].event.time == 11U);
    assert(values[0].value.symbols() == "x");
    assert(values[1].value.symbols() == "1");
    assert(values[2].value.symbols() == "0");
    assert(values[3].value.symbols() == "z");
}

void test_mixed_payload_accounting()
{
    using namespace fsim::runtime;
    FstChangeEncoder encoder;
    const auto text = encode_fst_string(std::string_view { "A\0B", 3U });
    const auto real = encode_fst_systemverilog_real(
        { SystemVerilogScalarKind::Real, UINT64_C(0x8000000000000000) });
    encoder.append(event(1U, 0U, 0U, TraceRegion::Snapshot, 0U), text);
    encoder.append(event(2U, 9U, 0U, TraceRegion::Postponed, 1U), real);
    const auto ordered = std::move(encoder).freeze();
    assert(ordered.timestamp_count() == 2U);
    assert(ordered.payload_bytes()
        == text.string_bytes().size() + text.canonical_type().size()
            + sizeof(std::uint64_t) + real.canonical_type().size());
    assert(ordered.values()[0].value.string_bytes()
        == std::string_view("A\0B", 3U));
    assert(ordered.values()[1].value.real_bits()
        == UINT64_C(0x8000000000000000));
}

void test_identity_and_lifecycle_rejection()
{
    using namespace fsim::runtime;
    FstChangeEncoder encoder;
    expect_exception<std::invalid_argument>([&] {
        encoder.append(event(0U, 0U, 0U, TraceRegion::Active, 0U), logic("0"));
    });
    const auto first = event(1U, 2U, 3U, TraceRegion::Reactive, 4U);
    encoder.append(first, logic("0"));
    expect_exception<std::invalid_argument>([&] {
        encoder.append(first, logic("1"));
    });
    encoder.append(event(2U, 2U, 3U, TraceRegion::Reactive, 4U), logic("1"));
    const auto ordered = std::move(encoder).freeze();
    assert(ordered.values().size() == 2U);
    expect_exception<std::logic_error>([&] {
        static_cast<void>(std::move(encoder).freeze());
    });
}

void test_limits_reject_before_accepted_state_changes()
{
    using namespace fsim::runtime;
    expect_exception<std::invalid_argument>([] {
        static_cast<void>(FstChangeEncoder(
            { .maximum_events = 0U, .maximum_timestamps = 1U, .maximum_payload_bytes = 1U }));
    });

    FstChangeEncoder event_limited(
        { .maximum_events = 1U, .maximum_timestamps = 2U, .maximum_payload_bytes = 8U });
    event_limited.append(
        event(1U, 0U, 0U, TraceRegion::Active, 0U), logic("0"));
    expect_exception<std::length_error>([&] {
        event_limited.append(
            event(2U, 1U, 0U, TraceRegion::Active, 1U), logic("1"));
    });
    assert(std::move(event_limited).freeze().values().size() == 1U);

    FstChangeEncoder timestamp_limited(
        { .maximum_events = 3U, .maximum_timestamps = 1U, .maximum_payload_bytes = 8U });
    timestamp_limited.append(
        event(1U, 5U, 0U, TraceRegion::Active, 0U), logic("0"));
    timestamp_limited.append(
        event(2U, 5U, 0U, TraceRegion::Active, 1U), logic("1"));
    expect_exception<std::length_error>([&] {
        timestamp_limited.append(
            event(3U, 6U, 0U, TraceRegion::Active, 2U), logic("x"));
    });
    const auto same_time = std::move(timestamp_limited).freeze();
    assert(same_time.values().size() == 2U);
    assert(same_time.timestamp_count() == 1U);

    FstChangeEncoder payload_limited(
        { .maximum_events = 2U, .maximum_timestamps = 2U, .maximum_payload_bytes = 1U });
    payload_limited.append(
        event(1U, 0U, 0U, TraceRegion::Active, 0U), logic("0"));
    expect_exception<std::length_error>([&] {
        payload_limited.append(
            event(1U, 1U, 0U, TraceRegion::Active, 1U), logic("1"));
    });
    const auto bounded = std::move(payload_limited).freeze();
    assert(bounded.values().size() == 1U);
    assert(bounded.payload_bytes() == 1U);
}

} // namespace

int main()
{
    test_canonical_order_preserves_every_value();
    test_mixed_payload_accounting();
    test_identity_and_lifecycle_rejection();
    test_limits_reject_before_accepted_state_changes();
}
