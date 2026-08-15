// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/trace_model.hpp"
#include "fsim/runtime/vcd_writer.hpp"

#include <cassert>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace {

template <typename Callback>
void expect_invalid(Callback&& callback)
{
    bool rejected = false;
    try {
        callback();
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    assert(rejected);
}

void test_declaration_identities()
{
    using namespace fsim::runtime;
    TraceDeclarationBuilder builder;
    const auto clock = builder.add_variable("top.clock", 1);
    const auto clock_alias = builder.add_alias("mirror.clock", clock);
    const auto bus = builder.add_variable("top.core.bus", 17);
    const auto real = builder.add_variable(
        "top.measurement", 64, SystemVerilogScalarKind::Real);
    const auto text = builder.add_typed_variable(
        "top.text", TraceTypeKind::SystemVerilogString, 0,
        SystemVerilogScalarKind::None, "systemverilog.string:bytes:v1");
    const auto state = builder.add_typed_variable(
        "top.state", TraceTypeKind::Enumeration, 3,
        SystemVerilogScalarKind::None, "enum:A");
    const auto state_alias_type = builder.add_typed_variable(
        "top.next_state", TraceTypeKind::Enumeration, 3,
        SystemVerilogScalarKind::None, "enum:A");
    const auto other_state = builder.add_typed_variable(
        "top.other_state", TraceTypeKind::Enumeration, 3,
        SystemVerilogScalarKind::None, "enum:B");
    const auto class_leaf = builder.add_typed_variable(
        "dynamic.object_1.value", TraceTypeKind::TypedLeaf, 17,
        SystemVerilogScalarKind::None,
        "fsim.fst.leaf.v1;k=5;l=0;w=17;s=1;o=8:object-1;p=5:value;d=0;");
    const auto class_leaf_same_type = builder.add_typed_variable(
        "dynamic.object_1.mirror", TraceTypeKind::TypedLeaf, 17,
        SystemVerilogScalarKind::None,
        "fsim.fst.leaf.v1;k=5;l=0;w=17;s=1;o=8:object-1;p=5:value;d=0;");
    const auto other_class_leaf = builder.add_typed_variable(
        "dynamic.object_2.value", TraceTypeKind::TypedLeaf, 17,
        SystemVerilogScalarKind::None,
        "fsim.fst.leaf.v1;k=5;l=0;w=17;s=1;o=8:object-2;p=5:value;d=0;");
    const auto class_alias = builder.add_alias(
        "dynamic.alias_value", class_leaf);
    const auto model = std::move(builder).freeze();

    static_assert(std::is_same_v<
        decltype(model.variables()),
        std::span<const TraceVariableDeclaration>>);
    assert(clock.value == 1);
    assert(clock_alias.value == 2);
    assert(bus.value == 3);
    assert(real.value == 4);
    assert(model.entries().size() == 12);
    assert(model.variables().size() == 10);
    assert(model.aliases().size() == 2);
    assert(model.alias(clock_alias).target == clock);
    assert(model.variable(bus).hierarchical_name == "top.core.bus");
    assert(model.scopes().size() == 6);
    assert(model.sources().size() == 12);
    assert(model.types().size() == 8);
    assert(model.type(model.variable(real).type).kind
        == TraceTypeKind::SystemVerilogScalar);
    assert(model.type(model.variable(text).type).kind
        == TraceTypeKind::SystemVerilogString);
    assert(model.variable(state).type == model.variable(state_alias_type).type);
    assert(model.variable(state).type != model.variable(other_state).type);
    assert(model.type(model.variable(state).type).canonical_metadata
        == "enum:A");
    assert(model.variable(class_leaf).type
        == model.variable(class_leaf_same_type).type);
    assert(model.variable(class_leaf).type
        != model.variable(other_class_leaf).type);
    assert(model.alias(class_alias).target == class_leaf);
}

void test_declaration_negatives()
{
    using namespace fsim::runtime;
    expect_invalid([] {
        TraceDeclarationBuilder builder;
        static_cast<void>(builder.add_variable("", 1));
    });
    expect_invalid([] {
        TraceDeclarationBuilder builder;
        static_cast<void>(builder.add_variable("top..value", 1));
    });
    expect_invalid([] {
        TraceDeclarationBuilder builder;
        static_cast<void>(builder.add_variable("top.value", 0));
    });
    expect_invalid([] {
        TraceDeclarationBuilder builder;
        static_cast<void>(builder.add_typed_variable(
            "top.text", TraceTypeKind::SystemVerilogString, 1,
            SystemVerilogScalarKind::None, "string"));
    });
    expect_invalid([] {
        TraceDeclarationBuilder builder;
        static_cast<void>(builder.add_typed_variable(
            "top.packed", TraceTypeKind::Packed, 0,
            SystemVerilogScalarKind::None, { }));
    });
    expect_invalid([] {
        TraceDeclarationBuilder builder;
        static_cast<void>(builder.add_typed_variable(
            "top.state", TraceTypeKind::Enumeration, 3,
            SystemVerilogScalarKind::None, { }));
    });
    expect_invalid([] {
        TraceDeclarationBuilder builder;
        static_cast<void>(builder.add_typed_variable(
            "top.leaf", TraceTypeKind::TypedLeaf, 3,
            SystemVerilogScalarKind::None, { }));
    });
    expect_invalid([] {
        TraceDeclarationBuilder builder;
        static_cast<void>(builder.add_typed_variable(
            "top.packed", TraceTypeKind::Packed, 1,
            SystemVerilogScalarKind::Real, { }));
    });
    expect_invalid([] {
        TraceDeclarationBuilder builder;
        static_cast<void>(builder.add_alias("top.alias", { 99 }));
    });
    expect_invalid([] {
        TraceDeclarationBuilder builder;
        static_cast<void>(builder.add_variable("top.value", 1));
        static_cast<void>(builder.add_variable("top.value", 1));
    });
}

void test_mixed_root_source_provenance()
{
    using namespace fsim::runtime;
    TraceSourceMetadata verilog;
    verilog.language = TraceLanguage::Verilog;
    verilog.root_identity = "producer";
    verilog.library = "work";
    verilog.owner_identity = "work.producer@producer";
    TraceSourceMetadata vhdl;
    vhdl.language = TraceLanguage::Vhdl;
    vhdl.root_identity = "consumer";
    vhdl.library = "rtl";
    vhdl.owner_identity = "rtl.consumer(rtl)@consumer";
    TraceSourceMetadata systemverilog;
    systemverilog.language = TraceLanguage::SystemVerilog;
    systemverilog.root_identity = "monitor";
    systemverilog.library = "verification";
    systemverilog.owner_identity = "verification:monitor@monitor";
    TraceSourceMetadata systemc;
    systemc.kind = TraceSourceKind::SystemC;
    systemc.language = TraceLanguage::SystemC;
    systemc.root_identity = "bridge";
    systemc.library = "native";
    systemc.owner_identity = "native.bridge@bridge";

    TraceDeclarationBuilder builder;
    const auto produced = builder.add_typed_variable(
        "producer.data", TraceTypeKind::Packed, 8U,
        SystemVerilogScalarKind::None, { }, verilog);
    const auto consumed = builder.add_typed_variable(
        "consumer.data", TraceTypeKind::Packed, 8U,
        SystemVerilogScalarKind::None, { }, vhdl);
    const auto bridged = builder.add_typed_variable(
        "bridge.data", TraceTypeKind::Packed, 8U,
        SystemVerilogScalarKind::None, { }, systemc);
    const auto monitored = builder.add_typed_variable(
        "monitor.data", TraceTypeKind::Packed, 8U,
        SystemVerilogScalarKind::None, { }, systemverilog);
    const auto boundary_alias = builder.add_alias(
        "consumer.producer_data", produced, vhdl);
    const auto model = std::move(builder).freeze();

    const auto& producer_source
        = model.source(model.variable(produced).source);
    const auto& consumer_source
        = model.source(model.variable(consumed).source);
    const auto& systemc_source
        = model.source(model.variable(bridged).source);
    const auto& systemverilog_source
        = model.source(model.variable(monitored).source);
    const auto& alias_source = model.source(model.alias(boundary_alias).source);
    assert(producer_source.language == TraceLanguage::Verilog);
    assert(producer_source.root_identity == "producer");
    assert(consumer_source.language == TraceLanguage::Vhdl);
    assert(consumer_source.library == "rtl");
    assert(systemc_source.kind == TraceSourceKind::SystemC);
    assert(systemc_source.owner_identity == "native.bridge@bridge");
    assert(systemverilog_source.language == TraceLanguage::SystemVerilog);
    assert(systemverilog_source.library == "verification");
    assert(alias_source.language == TraceLanguage::Vhdl);
    assert(model.alias(boundary_alias).target == produced);
    assert(model.scopes().size() == 4U);

    expect_invalid([] {
        TraceSourceMetadata incomplete;
        incomplete.language = TraceLanguage::Vhdl;
        incomplete.root_identity = "top";
        TraceDeclarationBuilder invalid;
        static_cast<void>(invalid.add_typed_variable(
            "top.value", TraceTypeKind::Packed, 1U,
            SystemVerilogScalarKind::None, { }, incomplete));
    });
    expect_invalid([] {
        TraceSourceMetadata conflict;
        conflict.language = TraceLanguage::SystemC;
        conflict.root_identity = "top";
        conflict.library = "native";
        conflict.owner_identity = "native.top@top";
        TraceDeclarationBuilder invalid;
        static_cast<void>(invalid.add_typed_variable(
            "top.value", TraceTypeKind::Packed, 1U,
            SystemVerilogScalarKind::None, { }, conflict));
    });
    expect_invalid([] {
        TraceSourceMetadata wrong_root;
        wrong_root.language = TraceLanguage::Verilog;
        wrong_root.root_identity = "left";
        wrong_root.library = "work";
        wrong_root.owner_identity = "work:left@left";
        TraceDeclarationBuilder invalid;
        static_cast<void>(invalid.add_typed_variable(
            "right.value", TraceTypeKind::Packed, 1U,
            SystemVerilogScalarKind::None, { }, wrong_root));
    });
    expect_invalid([] {
        TraceSourceMetadata wrong_root;
        wrong_root.language = TraceLanguage::Vhdl;
        wrong_root.root_identity = "left";
        wrong_root.library = "work";
        wrong_root.owner_identity = "work:left(rtl)@left";
        TraceDeclarationBuilder invalid;
        const auto target = invalid.add_variable("target.value", 1U);
        static_cast<void>(invalid.add_alias(
            "right.value", target, wrong_root));
    });
}

void test_event_identity_and_order()
{
    using namespace fsim::runtime;
    const TraceEvent snapshot {
        { 1 }, 5, 2, TraceRegion::Snapshot, 10
    };
    const TraceEvent active {
        { 1 }, 5, 2, TraceRegion::Active, 11
    };
    const TraceEvent later {
        { 2 }, 6, 0, TraceRegion::Snapshot, 12
    };
    assert(trace_event_precedes(snapshot, active));
    assert(trace_event_precedes(active, later));
    assert(!trace_event_precedes(later, active));
}

void test_vcd_model_equivalence()
{
    using namespace fsim::runtime;
    std::ostringstream legacy_output;
    VcdWriter legacy { legacy_output, "1ns", 64 };
    const auto legacy_clock = legacy.declare_signal("top.clock", 1);
    const auto legacy_alias = legacy.declare_signal("mirror.clock", 1);
    const auto legacy_bus = legacy.declare_signal("top.core.bus", 4);
    legacy.begin(0);
    legacy.change(legacy_clock, Logic4::zero);
    legacy.change(legacy_alias, Logic4::zero);
    legacy.change(legacy_bus, PackedLogic4::from_msb_string("0011"));
    legacy.set_time(10);
    legacy.change(legacy_clock, Logic4::one);
    legacy.change(legacy_alias, Logic4::one);
    legacy.change(legacy_bus, PackedLogic4::from_msb_string("1010"));
    legacy.flush();

    TraceDeclarationBuilder builder;
    const auto clock = builder.add_variable("top.clock", 1);
    static_cast<void>(builder.add_alias("mirror.clock", clock));
    const auto bus = builder.add_variable("top.core.bus", 4);
    const auto model = std::move(builder).freeze();
    std::ostringstream neutral_output;
    VcdWriter neutral { neutral_output, "1ns", 64 };
    const auto handles = neutral.declare_model(model);
    assert(handles.size() == 3);
    neutral.begin(0);
    neutral.set_event({ clock, 0, 0, TraceRegion::Snapshot, 1 });
    neutral.change(handles[0], Logic4::zero);
    neutral.change(handles[1], Logic4::zero);
    neutral.set_event({ bus, 0, 0, TraceRegion::Snapshot, 2 });
    neutral.change(handles[2], PackedLogic4::from_msb_string("0011"));
    neutral.set_event({ clock, 10, 0, TraceRegion::Active, 3 });
    neutral.change(handles[0], Logic4::one);
    neutral.change(handles[1], Logic4::one);
    neutral.set_event({ bus, 10, 0, TraceRegion::Active, 4 });
    neutral.change(handles[2], PackedLogic4::from_msb_string("1010"));
    neutral.flush();
    assert(neutral_output.str() == legacy_output.str());

    bool unknown_rejected = false;
    try {
        neutral.set_event({ { 99 }, 10, 0, TraceRegion::Active, 5 });
    } catch (const std::invalid_argument&) {
        unknown_rejected = true;
    }
    assert(unknown_rejected);

    bool backward_rejected = false;
    try {
        neutral.set_event({ clock, 9, 0, TraceRegion::Active, 6 });
    } catch (const std::invalid_argument&) {
        backward_rejected = true;
    }
    assert(backward_rejected);
}

} // namespace

int main()
{
    test_declaration_identities();
    test_declaration_negatives();
    test_mixed_root_source_provenance();
    test_event_identity_and_order();
    test_vcd_model_equivalence();
}
