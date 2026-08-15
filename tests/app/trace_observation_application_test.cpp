// SPDX-License-Identifier: Apache-2.0
#include "application_trace_observation.hpp"
#include "fsim/runtime/fst_value_encoder.hpp"
#include "fsim/runtime/fst_writer.hpp"
#include "fsim/runtime/vcd_writer.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using fsim::app::application_detail::TraceObservationKind;
using fsim::app::application_detail::TraceObservationLimits;
using fsim::app::application_detail::TraceObservationRecorder;
using fsim::app::application_detail::TraceObservationValue;

template <typename Exception, typename Function>
void expect_throws(Function&& function)
{
    bool rejected = false;
    try {
        std::forward<Function>(function)();
    } catch (const Exception&) {
        rejected = true;
    }
    assert(rejected);
}

[[nodiscard]] fsim::runtime::TraceSignalId add_leaf(
    fsim::runtime::TraceDeclarationBuilder& builder,
    const std::string_view path,
    const fsim::runtime::FstLeafKind kind,
    const std::size_t width)
{
    fsim::runtime::FstLeafTypeMetadata metadata;
    metadata.kind = kind;
    metadata.owner_identity = std::string { path } + "@owner";
    metadata.leaf_path = "value";
    metadata.width = width;
    metadata.four_state = true;
    if (kind == fsim::runtime::FstLeafKind::Container) {
        metadata.dimensions = { { 0, static_cast<std::int64_t>(width - 1U),
            false } };
    }
    return builder.add_typed_variable(
        path, fsim::runtime::TraceTypeKind::TypedLeaf, width,
        fsim::runtime::SystemVerilogScalarKind::None,
        fsim::runtime::canonical_fst_leaf_type_metadata(metadata),
        fsim::runtime::TraceSourceKind::Internal);
}

struct Fixture {
    fsim::runtime::TraceDeclarationModel declarations;
    std::array<fsim::runtime::TraceSignalId, 8> signals;
};

[[nodiscard]] Fixture make_fixture()
{
    using namespace fsim::runtime;
    TraceDeclarationBuilder builder;
    std::array<TraceSignalId, 8> signals;
    signals[0] = builder.add_variable(
        "top.signal", 4U, SystemVerilogScalarKind::None,
        TraceSourceKind::Callback);
    signals[1] = builder.add_variable(
        "__fsim.uvm.sequence", 64U, SystemVerilogScalarKind::None,
        TraceSourceKind::Uvm);
    signals[2] = builder.add_variable(
        "__fsim.uvm.value", 64U, SystemVerilogScalarKind::None,
        TraceSourceKind::Uvm);
    signals[3] = add_leaf(
        builder, "__fsim.class.value", FstLeafKind::DynamicClass, 17U);
    signals[4] = add_leaf(
        builder, "__fsim.container.value", FstLeafKind::Container, 9U);
    signals[5] = add_leaf(
        builder, "__fsim.coverage.value", FstLeafKind::Coverage, 11U);
    signals[6] = add_leaf(
        builder, "__fsim.assertion.value", FstLeafKind::Assertion, 3U);
    signals[7] = builder.add_variable(
        "__fsim.sdf.value", 8U, SystemVerilogScalarKind::None,
        TraceSourceKind::Callback);
    return { std::move(builder).freeze(), signals };
}

[[nodiscard]] fsim::runtime::FstEncodedValue encode(
    const fsim::runtime::TraceDeclarationModel& declarations,
    const TraceObservationValue& observed)
{
    using namespace fsim::runtime;
    const auto& variable = declarations.variable(observed.signal);
    const auto& type = declarations.type(variable.type);
    if (type.kind == TraceTypeKind::TypedLeaf) {
        return encode_fst_leaf_value(observed.value, type.canonical_metadata);
    }
    return encode_fst_logic_value(observed.value);
}

void test_atomic_correlated_fanout()
{
    using namespace fsim;
    using namespace runtime;
    const auto fixture = make_fixture();
    TraceObservationRecorder recorder(fixture.declarations);

    std::ostringstream fst_bytes(std::ios::binary);
    FstWriter fst(fst_bytes);
    fst.declare(fixture.declarations);
    fst.begin();
    static_cast<void>(recorder.add_observer([&](const auto& record) {
        for (const auto& observed : record.values) {
            fst.change({ observed.signal, record.time, record.delta,
                           record.region, record.sequence },
                encode(fixture.declarations, observed));
        }
    }));

    std::ostringstream vcd_text;
    VcdWriter vcd(vcd_text);
    const auto handles = vcd.declare_model(fixture.declarations);
    vcd.begin();
    static_cast<void>(recorder.add_observer([&](const auto& record) {
        for (const auto& observed : record.values) {
            vcd.set_event({ observed.signal, record.time, record.delta,
                record.region, record.sequence });
            vcd.change(handles.at(observed.signal.value - 1U), observed.value);
        }
    }));

    std::size_t failing_attempts = 0U;
    static_cast<void>(recorder.add_observer([&](const auto& record) {
        ++failing_attempts;
        if (record.kind == TraceObservationKind::Coverage) {
            throw std::runtime_error("contained optional callback failure");
        }
    }));
    std::vector<std::uint64_t> complete_sequences;
    static_cast<void>(recorder.add_observer([&](const auto& record) {
        complete_sequences.push_back(record.sequence);
        assert(!record.values.empty());
    }));

    const auto accept_one = [&](const TraceObservationKind kind,
                                const std::uint64_t time,
                                const runtime::TraceRegion region,
                                const std::string_view identity,
                                const std::size_t signal,
                                const std::string_view value) {
        const std::array values { TraceObservationValue {
            fixture.signals[signal], PackedLogic4::from_msb_string(value),
            std::nullopt } };
        static_cast<void>(recorder.accept(
            kind, time, time + 10U, region, std::string { identity }, values));
    };
    accept_one(TraceObservationKind::Signal, 1U, TraceRegion::Active,
        "signal:top.signal", 0U, "10XZ");
    const std::array uvm_values {
        TraceObservationValue { fixture.signals[1],
            PackedLogic4::from_aval_bval(64U, 17U, 0U), std::nullopt },
        TraceObservationValue { fixture.signals[2],
            PackedLogic4::from_aval_bval(64U, 99U, 0U), std::nullopt },
    };
    static_cast<void>(recorder.accept(TraceObservationKind::Uvm, 2U, 12U,
        TraceRegion::Callback, "uvm:phase", uvm_values));
    accept_one(TraceObservationKind::DynamicClass, 3U, TraceRegion::Reactive,
        "class:7:property", 3U, "10XZ0011010101010");
    accept_one(TraceObservationKind::Container, 4U, TraceRegion::Active,
        "container:3", 4U, "10XZ00110");
    accept_one(TraceObservationKind::Coverage, 5U, TraceRegion::Observed,
        "coverage:point", 5U, "10101010X01");
    accept_one(TraceObservationKind::Assertion, 6U, TraceRegion::Observed,
        "assertion:p", 6U, "10X");
    accept_one(TraceObservationKind::Sdf, 7U, TraceRegion::Postponed,
        "sdf:top.path", 7U, "10XZ0011");

    assert(recorder.records().size() == 7U);
    assert(recorder.records()[1].values.size() == 2U);
    assert(recorder.records()[1].time == 2U);
    assert(recorder.records()[1].delta == 12U);
    assert(recorder.records()[1].region == TraceRegion::Callback);
    assert(failing_attempts == 7U);
    assert(complete_sequences
        == std::vector<std::uint64_t>({ 1U, 2U, 3U, 4U, 5U, 6U, 7U }));
    assert(recorder.callback_failures() == 1U);
    assert(recorder.callback_failure());

    fst.close(7U);
    vcd.flush();
    assert(!fst_bytes.str().empty());
    assert(vcd_text.str().find("#7") != std::string::npos);
}

void test_transactional_negatives()
{
    using namespace fsim;
    using namespace runtime;
    const auto fixture = make_fixture();
    TraceObservationLimits limits;
    limits.maximum_records = 1U;
    limits.maximum_values_per_record = 2U;
    limits.maximum_payload_bits = 64U;
    limits.maximum_identity_bytes = 8U;
    limits.maximum_observers = 1U;
    TraceObservationRecorder recorder(fixture.declarations, limits);

    expect_throws<std::invalid_argument>([&] {
        static_cast<void>(recorder.add_observer({ }));
    });
    const auto token = recorder.add_observer([](const auto&) { });
    expect_throws<std::length_error>([&] {
        static_cast<void>(recorder.add_observer([](const auto&) { }));
    });
    recorder.remove_observer(token);

    const std::array valid { TraceObservationValue { fixture.signals[0],
        PackedLogic4::from_msb_string("1010"), std::nullopt } };
    const auto expect_unchanged = [&](auto&& function) {
        const auto before = recorder.records().size();
        std::forward<decltype(function)>(function)();
        assert(recorder.records().size() == before);
    };
    expect_unchanged([&] {
        expect_throws<std::invalid_argument>([&] {
            static_cast<void>(recorder.accept(
                static_cast<TraceObservationKind>(255U), 0U, 0U,
                TraceRegion::Active, "kind", valid));
        });
    });
    expect_unchanged([&] {
        expect_throws<std::invalid_argument>([&] {
            static_cast<void>(recorder.accept(TraceObservationKind::Signal,
                0U, 0U, static_cast<TraceRegion>(255U), "region", valid));
        });
    });
    expect_unchanged([&] {
        expect_throws<std::invalid_argument>([&] {
            static_cast<void>(recorder.accept(TraceObservationKind::Signal,
                0U, 0U, TraceRegion::Active, "", valid));
        });
    });
    expect_unchanged([&] {
        expect_throws<std::invalid_argument>([&] {
            static_cast<void>(recorder.accept(TraceObservationKind::Signal,
                0U, 0U, TraceRegion::Active,
                std::string { "nul\0id", 6U }, valid));
        });
    });
    expect_unchanged([&] {
        expect_throws<std::invalid_argument>([&] {
            static_cast<void>(recorder.accept(TraceObservationKind::Signal,
                0U, 0U, TraceRegion::Active, "too-long!", valid));
        });
    });
    expect_unchanged([&] {
        expect_throws<std::length_error>([&] {
            static_cast<void>(recorder.accept(TraceObservationKind::Signal,
                0U, 0U, TraceRegion::Active, "empty", { }));
        });
    });
    const std::array duplicate { valid[0], valid[0] };
    expect_unchanged([&] {
        expect_throws<std::invalid_argument>([&] {
            static_cast<void>(recorder.accept(TraceObservationKind::Signal,
                0U, 0U, TraceRegion::Active, "dup", duplicate));
        });
    });
    const std::array too_many { valid[0], valid[0], valid[0] };
    expect_unchanged([&] {
        expect_throws<std::length_error>([&] {
            static_cast<void>(recorder.accept(TraceObservationKind::Signal,
                0U, 0U, TraceRegion::Active, "many", too_many));
        });
    });
    const std::array unknown { TraceObservationValue { TraceSignalId { 999U },
        PackedLogic4::from_msb_string("1"), std::nullopt } };
    expect_unchanged([&] {
        expect_throws<std::invalid_argument>([&] {
            static_cast<void>(recorder.accept(TraceObservationKind::Signal,
                0U, 0U, TraceRegion::Active, "unknown", unknown));
        });
    });
    const std::array wrong_width { TraceObservationValue { fixture.signals[0],
        PackedLogic4::from_msb_string("1"), std::nullopt } };
    expect_unchanged([&] {
        expect_throws<std::invalid_argument>([&] {
            static_cast<void>(recorder.accept(TraceObservationKind::Signal,
                0U, 0U, TraceRegion::Active, "width", wrong_width));
        });
    });
    const std::array oversized { TraceObservationValue { fixture.signals[1],
                                     PackedLogic4(64U, Logic4::zero), std::nullopt },
        TraceObservationValue { fixture.signals[2],
            PackedLogic4(64U, Logic4::zero), std::nullopt } };
    expect_unchanged([&] {
        expect_throws<std::length_error>([&] {
            static_cast<void>(recorder.accept(TraceObservationKind::Uvm,
                0U, 0U, TraceRegion::Callback, "payload", oversized));
        });
    });

    static_cast<void>(recorder.accept(TraceObservationKind::Signal,
        0U, 0U, TraceRegion::Active, "valid", valid));
    expect_throws<std::length_error>([&] {
        static_cast<void>(recorder.accept(TraceObservationKind::Signal,
            1U, 0U, TraceRegion::Active, "second", valid));
    });
    assert(recorder.records().size() == 1U);
}

void test_reentrant_callback_containment()
{
    const auto fixture = make_fixture();
    TraceObservationRecorder recorder(fixture.declarations);
    std::size_t complete = 0U;
    static_cast<void>(recorder.add_observer([&](const auto& record) {
        static_cast<void>(recorder.accept(record.kind, record.time,
            record.delta, record.region, "nested", record.values));
    }));
    static_cast<void>(recorder.add_observer([&](const auto&) { ++complete; }));
    const std::array values { TraceObservationValue { fixture.signals[0],
        fsim::runtime::PackedLogic4::from_msb_string("1010"), std::nullopt } };
    static_cast<void>(recorder.accept(TraceObservationKind::Signal, 1U, 2U,
        fsim::runtime::TraceRegion::Active, "outer", values));
    assert(recorder.records().size() == 1U);
    assert(recorder.callback_failures() == 1U);
    assert(recorder.callback_failure());
    assert(complete == 1U);
}

void test_systemc_repeated_dirty_phase()
{
    using namespace fsim::runtime;
    TraceDeclarationBuilder builder;
    TraceSourceMetadata source;
    source.kind = TraceSourceKind::SystemC;
    source.language = TraceLanguage::SystemC;
    source.root_identity = "systemc_root";
    source.library = "systemc";
    source.owner_identity = "systemc:trace-island";
    const auto signal = builder.add_typed_variable("systemc_root.wide",
        TraceTypeKind::Packed, 257U, SystemVerilogScalarKind::None,
        { }, source);
    const auto alias = builder.add_alias(
        "systemc_root.wide_input", signal, source);
    const auto declarations = std::move(builder).freeze();
    assert(declarations.alias(alias).target == signal);
    TraceObservationRecorder recorder(declarations);
    std::string symbols(257U, '0');
    for (std::size_t index = 0U; index < symbols.size(); ++index) {
        constexpr std::array states { '0', '1', 'X', 'Z' };
        symbols[index] = states[index % states.size()];
    }
    const std::array values { TraceObservationValue { signal,
        PackedLogic4::from_msb_string(symbols), std::nullopt } };
    const auto first = recorder.accept(TraceObservationKind::Signal, 8U, 2U,
        TraceRegion::Postponed, "systemc:post-update-dirty", values);
    const auto second = recorder.accept(TraceObservationKind::Signal, 8U, 3U,
        TraceRegion::Postponed, "systemc:post-update-dirty", values);
    assert(first + 1U == second);
    assert(recorder.records().size() == 2U);
    assert(recorder.records()[0].values.front().signal
        == recorder.records()[1].values.front().signal);
    assert(recorder.records()[0].values.front().value
        == recorder.records()[1].values.front().value);
}

} // namespace

int main()
{
    test_atomic_correlated_fanout();
    test_transactional_negatives();
    test_reentrant_callback_containment();
    test_systemc_repeated_dirty_phase();
}
