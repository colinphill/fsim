// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

namespace fsim::tests::elaboration {

void test_specialization_and_packages() {
    constexpr std::string_view source = R"(
module counter(input logic clk, output logic [7:0] q);
  logic [7:0] next;
  assign next = q + 1;
  always_ff @(posedge clk) q <= next;
endmodule
)";
    const auto parsed = fsim::frontend::parse_text(
        "counter.sv", source, fsim::frontend::Language::SystemVerilog2017);
    assert(parsed.ok());

    auto elaborated = fsim::elaboration::elaborate(parsed.design, "sv:work.counter");
    if (!elaborated.ok()) {
        for (const auto& diagnostic : elaborated.diagnostics) {
            std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
        }
    }
    assert(elaborated.ok());
    assert(elaborated.design->signals().size() == 3);
    assert(elaborated.design->processes().size() == 2);
    assert(elaborated.design->specializations().size() == 1);
    const auto& counter_specialization =
        elaborated.design->specializations().front();
    assert(counter_specialization.id == 0);
    assert(counter_specialization.unit == "sv:work.counter");
    assert(counter_specialization.instance == "counter");
    assert(counter_specialization.source == "counter.sv");
    assert(
        counter_specialization.language
        == fsim::frontend::Language::SystemVerilog2017);
    assert(counter_specialization.library == "work");
    assert(counter_specialization.parameter_values.empty());
    assert(!counter_specialization.is_cell);
    assert((
        counter_specialization.processes
        == std::vector<fsim::runtime::simir::ProcessId>{0, 1}));

    auto interpreter = elaborated.design->create_interpreter();
    const auto clock = elaborated.design->find_signal("clk");
    const auto q = elaborated.design->find_signal("q");
    assert(clock && q);
    interpreter->deposit_signal(*q, fsim::runtime::PackedLogic4::from_msb_string("00000000"));
    interpreter->deposit_signal(*clock, fsim::runtime::PackedLogic4::from_msb_string("0"));
    interpreter->start();
    (void)interpreter->run();

    interpreter->deposit_signal(*clock, fsim::runtime::PackedLogic4::from_msb_string("1"));
    (void)interpreter->run();
    assert(interpreter->signal_value(*q).to_msb_string() == "00000001");

    interpreter->deposit_signal(*clock, fsim::runtime::PackedLogic4::from_msb_string("0"));
    (void)interpreter->run();
    interpreter->deposit_signal(*clock, fsim::runtime::PackedLogic4::from_msb_string("1"));
    (void)interpreter->run();
    assert(interpreter->signal_value(*q).to_msb_string() == "00000010");

    const auto directive_parsed = fsim::frontend::parse_text(
        "directives.sv",
        R"(
`celldefine
module pulled_child(input logic value);
endmodule
`endcelldefine
`unconnected_drive pull1
module pulled_parent;
  pulled_child child_instance();
endmodule
`nounconnected_drive
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(directive_parsed.ok());
    const auto directive_elaborated = fsim::elaboration::elaborate(
        directive_parsed.design, "sv:work.pulled_parent");
    assert(directive_elaborated.ok());
    const auto pulled_value =
        directive_elaborated.design->find_signal(
            "pulled_parent.child_instance.value");
    assert(pulled_value);
    const auto directive_interpreter =
        directive_elaborated.design->create_interpreter();
    assert(
        directive_interpreter->signal_value(*pulled_value).to_msb_string()
        == "1");
    assert(directive_elaborated.design->specializations().size() == 2);
    const auto& directive_specializations =
        directive_elaborated.design->specializations();
    assert(!directive_specializations.front().is_cell);
    assert(directive_specializations.back().is_cell);

    const auto implicit_parsed = fsim::frontend::parse_text(
        "implicit.sv",
        R"(
`default_nettype tri0
module implicit_top;
  assign created = 1'b1;
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(implicit_parsed.ok());
    const auto implicit_elaborated = fsim::elaboration::elaborate(
        implicit_parsed.design, "sv:work.implicit_top");
    assert(implicit_elaborated.ok());
    const auto created =
        implicit_elaborated.design->find_signal("created");
    assert(created);
    auto implicit_interpreter =
        implicit_elaborated.design->create_interpreter();
    assert(
        implicit_interpreter->signal_value(*created).to_msb_string()
        == "0");
    implicit_interpreter->start();
    (void)implicit_interpreter->run();
    assert(
        implicit_interpreter->signal_value(*created).to_msb_string()
        == "1");

    const auto parameterized_parsed = fsim::frontend::parse_text(
        "parameterized.sv",
        R"(
module parameterized #(
  parameter int WIDTH = 8,
  parameter int INCREMENT = 1,
  localparam int LAST = WIDTH - 1,
  localparam int CLOG_WIDTH = $clog2(WIDTH)
) (
  input logic clk,
  output logic [WIDTH - 1:0] q
);
  logic [WIDTH - 1:0] next;
  assign next = q + INCREMENT;
  always_ff @(posedge clk) q <= next;
endmodule

module parameterized_top(
  input logic clk,
  output logic [3:0] q4,
  output logic [7:0] q8
);
  parameterized #(.WIDTH(4), .INCREMENT(2)) four(
    .clk(clk), .q(q4)
  );
  parameterized #(8, 3) eight(
    .clk(clk), .q(q8)
  );
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(parameterized_parsed.ok());
    const auto parameterized_elaborated =
        fsim::elaboration::elaborate(
            parameterized_parsed.design,
            "sv:work.parameterized_top");
    if (!parameterized_elaborated.ok()) {
        for (const auto& diagnostic :
             parameterized_elaborated.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(parameterized_elaborated.ok());
    assert(
        parameterized_elaborated.design->specializations().size() == 3);
    const auto& parameter_specializations =
        parameterized_elaborated.design->specializations();
    assert(parameter_specializations[0].parameter_values.empty());
    assert((
        parameter_specializations[1].parameter_values
        == std::vector<std::pair<std::string, std::string>>{
            {"WIDTH", "4"},
            {"INCREMENT", "2"},
            {"LAST", "3"},
            {"CLOG_WIDTH", "2"}}));
    assert((
        parameter_specializations[2].parameter_values
        == std::vector<std::pair<std::string, std::string>>{
            {"WIDTH", "8"},
            {"INCREMENT", "3"},
            {"LAST", "7"},
            {"CLOG_WIDTH", "3"}}));
    const auto parameter_clock =
        parameterized_elaborated.design->find_signal("clk");
    const auto parameter_q4 =
        parameterized_elaborated.design->find_signal("q4");
    const auto parameter_q8 =
        parameterized_elaborated.design->find_signal("q8");
    const auto parameter_next4 =
        parameterized_elaborated.design->find_signal(
            "parameterized_top.four.next");
    const auto parameter_next8 =
        parameterized_elaborated.design->find_signal(
            "parameterized_top.eight.next");
    assert(
        parameter_clock && parameter_q4 && parameter_q8
        && parameter_next4 && parameter_next8);
    assert(
        parameterized_elaborated.design->signals()
            .at(*parameter_q4).width
        == 4);
    assert(
        parameterized_elaborated.design->signals()
            .at(*parameter_q8).width
        == 8);
    assert(
        parameterized_elaborated.design->signals()
            .at(*parameter_next4).width
        == 4);
    assert(
        parameterized_elaborated.design->signals()
            .at(*parameter_next8).width
        == 8);
    auto parameter_interpreter =
        parameterized_elaborated.design->create_interpreter();
    parameter_interpreter->deposit_signal(
        *parameter_clock,
        fsim::runtime::PackedLogic4::from_msb_string("0"));
    parameter_interpreter->deposit_signal(
        *parameter_q4,
        fsim::runtime::PackedLogic4::from_msb_string("0000"));
    parameter_interpreter->deposit_signal(
        *parameter_q8,
        fsim::runtime::PackedLogic4::from_msb_string("00000000"));
    parameter_interpreter->start();
    (void)parameter_interpreter->run();
    parameter_interpreter->deposit_signal(
        *parameter_clock,
        fsim::runtime::PackedLogic4::from_msb_string("1"));
    (void)parameter_interpreter->run();
    assert(
        parameter_interpreter->signal_value(*parameter_q4).to_msb_string()
        == "0010");
    assert(
        parameter_interpreter->signal_value(*parameter_q8).to_msb_string()
        == "00000011");

    const auto clog2_values = fsim::frontend::parse_text(
        "clog2-values.sv",
        R"(
module clog2_value #(
  parameter int VALUE = 0,
  localparam int RESULT = $clog2(VALUE)
) ();
endmodule

module clog2_values_top;
  clog2_value #(.VALUE(0)) zero();
  clog2_value #(.VALUE(1)) one();
  clog2_value #(.VALUE(2)) two();
  clog2_value #(.VALUE(3)) three();
  clog2_value #(.VALUE(4)) four();
  clog2_value #(.VALUE(5)) five();
  clog2_value #(.VALUE(9)) nine();
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(clog2_values.ok());
    const auto elaborated_clog2_values =
        fsim::elaboration::elaborate(
            clog2_values.design,
            "sv:work.clog2_values_top");
    assert(elaborated_clog2_values.ok());
    const std::array<
        std::tuple<
            std::string_view,
            std::string_view,
            std::string_view>,
        7>
        expected_clog2_values{{
            {"clog2_values_top.zero", "0", "0"},
            {"clog2_values_top.one", "1", "0"},
            {"clog2_values_top.two", "2", "1"},
            {"clog2_values_top.three", "3", "2"},
            {"clog2_values_top.four", "4", "2"},
            {"clog2_values_top.five", "5", "3"},
            {"clog2_values_top.nine", "9", "4"},
        }};
    for (const auto& [instance, input, expected] :
         expected_clog2_values) {
      const auto specialization = std::ranges::find_if(
          elaborated_clog2_values.design->specializations(),
          [&](const auto& candidate) {
            return candidate.instance == instance;
          });
      assert(
          specialization
          != elaborated_clog2_values.design->specializations().end());
      assert((
          specialization->parameter_values
          == std::vector<std::pair<std::string, std::string>>{
              {"VALUE", std::string{input}},
              {"RESULT", std::string{expected}}}));
    }

    const auto verilog_clog2 = fsim::frontend::parse_text(
        "verilog-clog2.v",
        R"(
module verilog_clog2 #(
  parameter VALUE = 9
) ();
  localparam RESULT = $clog2(VALUE);
endmodule
)",
        fsim::frontend::Language::Verilog2005);
    assert(verilog_clog2.ok());
    const auto elaborated_verilog_clog2 =
        fsim::elaboration::elaborate(
            verilog_clog2.design,
            "verilog:work.verilog_clog2");
    assert(elaborated_verilog_clog2.ok());
    assert(
        elaborated_verilog_clog2.design
            ->specializations().size()
        == 1);
    assert((
        elaborated_verilog_clog2.design
            ->specializations().front().parameter_values
        == std::vector<std::pair<std::string, std::string>>{
            {"VALUE", "9"}, {"RESULT", "4"}}));

    const auto invalid_parameters = fsim::frontend::parse_text(
        "invalid-parameter-elaboration.sv",
        R"(
module invalid_parameter_target #(
  parameter int WIDTH = 4,
  localparam int LOCAL_WIDTH = WIDTH,
  parameter int BROKEN = 1 / 0,
  parameter int BROKEN_CLOG = $clog2(-1),
  parameter int BROKEN_CLOG_ARITY = $clog2()
) ();
endmodule
module invalid_parameter_top;
  invalid_parameter_target #(.MISSING(2)) unknown();
  invalid_parameter_target #(.LOCAL_WIDTH(2)) local_override();
  invalid_parameter_target #(1, 2, 3) excessive();
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(invalid_parameters.ok());
    const auto rejected_parameters = fsim::elaboration::elaborate(
        invalid_parameters.design,
        "sv:work.invalid_parameter_top");
    assert(!rejected_parameters.ok());
    assert(has_diagnostic(
        rejected_parameters, "FSIM-ELAB-PARAM-001"));
    assert(has_diagnostic(
        rejected_parameters, "FSIM-ELAB-PARAM-005"));
    assert(std::ranges::any_of(
        rejected_parameters.diagnostics,
        [](const auto& diagnostic) {
          return diagnostic.message.find(
                     "$clog2 requires a nonnegative")
                     != std::string::npos;
        }));
    assert(std::ranges::any_of(
        rejected_parameters.diagnostics,
        [](const auto& diagnostic) {
          return diagnostic.message.find(
                     "$clog2 requires exactly one argument")
                     != std::string::npos;
        }));

    const auto systemverilog_base_package =
        fsim::frontend::parse_text(
            "systemverilog_base_package.sv",
            R"(
package base_values;
  parameter int WIDTH = 4;
  localparam int BASE = 5;
  typedef logic [WIDTH-1:0] word_t;
  typedef enum logic [WIDTH-1:0] {
    IDLE,
    ACTIVE = BASE + 1,
    DONE
  } state_t;
  typedef enum logic signed [1:0] {
    LOW = -2,
    NEXT_LOW,
    ZERO,
    HIGH
  } signed_state_t;
  typedef struct packed {
    logic [WIDTH-1:0] payload;
    bit valid;
  } packet_t;
  typedef union packed {
    logic [WIDTH-1:0] payload;
    logic [WIDTH-1:0] mirror;
  } overlay_t;
endpackage : base_values
)",
            fsim::frontend::Language::SystemVerilog2017);
    const auto systemverilog_derived_package =
        fsim::frontend::parse_text(
            "systemverilog_derived_package.sv",
            R"(
import base_values::*;
package derived_values;
  localparam int NEXT = BASE + 1;
  typedef base_values::state_t result_t;
endpackage : derived_values
)",
            fsim::frontend::Language::SystemVerilog2017);
    const auto systemverilog_package_user =
        fsim::frontend::parse_text(
            "systemverilog_package_user.sv",
            R"(
import derived_values::NEXT, derived_values::result_t;
import base_values::ACTIVE, base_values::WIDTH, base_values::packet_t,
       base_values::overlay_t;
module systemverilog_package_user #(
  parameter result_t INITIAL = ACTIVE
)(
  output result_t observed
);
  typedef result_t local_result_t;
  typedef struct packed {
    logic [0:3] payload;
  } ascending_packet_t;
  packet_t packet;
  overlay_t overlay;
  ascending_packet_t ascending_packet;
  result_t struct_payload;
  result_t replicated;
  logic signed [3:0] signed_shift;
  logic [3:0] unsigned_shift;
  generate
    if (1) begin : typed
      local_result_t staged;
      assign staged = INITIAL;
    end
  endgenerate
  initial begin
    packet_t local_packet;
    local_packet.payload = INITIAL;
    local_packet.valid = 1'b1;
    local_packet.payload[0 +: 2] = 2'b10;
    packet = local_packet;
    overlay.payload = INITIAL;
    overlay.mirror[WIDTH-1 -: 2] =
      packet.payload[WIDTH-1 -: 2];
    ascending_packet.payload = 4'b1010;
    ascending_packet.payload[0 +: 2] = 2'b01;
    ascending_packet.payload[3 -: 2] =
      ascending_packet.payload[0 +: 2];
    replicated = {WIDTH/2{1'b1, 1'b0}};
    replicated = {2'd2{2'b10}};
    signed_shift = 4'b1000;
    signed_shift = signed_shift >>> 1;
    unsigned_shift = 4'b1000;
    unsigned_shift = unsigned_shift >>> 1;
    overlay.payload = overlay.payload <<< 0;
  end
  packet_passthrough u_passthrough(
    .packet(packet),
    .payload(struct_payload)
  );
  assign observed = {
    overlay.mirror[base_values::WIDTH-1:1],
    packet.payload[0]
  };
endmodule

module packet_passthrough(
  input packet_t packet,
  output result_t payload
);
  assign payload = packet.payload;
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(systemverilog_base_package.ok());
    assert(systemverilog_derived_package.ok());
    assert(systemverilog_package_user.ok());
    fsim::frontend::ParsedDesign systemverilog_package_design =
        systemverilog_base_package.design;
    systemverilog_package_design.units.insert(
        systemverilog_package_design.units.end(),
        systemverilog_derived_package.design.units.begin(),
        systemverilog_derived_package.design.units.end());
    systemverilog_package_design.units.insert(
        systemverilog_package_design.units.end(),
        systemverilog_package_user.design.units.begin(),
        systemverilog_package_user.design.units.end());
    const auto systemverilog_package_elaborated =
        fsim::elaboration::elaborate(
            systemverilog_package_design,
            "sv:work.systemverilog_package_user");
    if (!systemverilog_package_elaborated.ok()) {
        for (const auto& diagnostic :
             systemverilog_package_elaborated.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(systemverilog_package_elaborated.ok());
    const auto systemverilog_package_observed =
        systemverilog_package_elaborated.design->find_signal(
            "observed");
    assert(systemverilog_package_observed);
    assert(
        systemverilog_package_elaborated.design->signals()
            .at(*systemverilog_package_observed).width
        == 4);
    const auto systemverilog_package_staged =
        systemverilog_package_elaborated.design->find_signal(
            "systemverilog_package_user.typed.staged");
    assert(systemverilog_package_staged);
    assert(
        systemverilog_package_elaborated.design->signals()
            .at(*systemverilog_package_staged).width
        == 4);
    const auto systemverilog_package_packet =
        systemverilog_package_elaborated.design->find_signal(
            "systemverilog_package_user.packet");
    assert(systemverilog_package_packet);
    const auto& systemverilog_packet_info =
        systemverilog_package_elaborated.design->signals().at(
            *systemverilog_package_packet);
    assert(systemverilog_packet_info.width == 5);
    assert(systemverilog_packet_info.packed_members.size() == 2);
    assert(
        systemverilog_packet_info.packed_members[0].name
            == "payload"
        && systemverilog_packet_info.packed_members[0].lsb_offset
            == 1
        && systemverilog_packet_info.packed_members[1].name
            == "valid"
        && systemverilog_packet_info.packed_members[1].lsb_offset
            == 0);
    const auto systemverilog_package_overlay =
        systemverilog_package_elaborated.design->find_signal(
            "systemverilog_package_user.overlay");
    assert(systemverilog_package_overlay);
    const auto& systemverilog_overlay_info =
        systemverilog_package_elaborated.design->signals().at(
            *systemverilog_package_overlay);
    assert(systemverilog_overlay_info.width == 4);
    assert(systemverilog_overlay_info.packed_members.size() == 2);
    assert(
        systemverilog_overlay_info.packed_members[0].lsb_offset
            == 0
        && systemverilog_overlay_info.packed_members[1].lsb_offset
            == 0);
    const auto systemverilog_struct_payload =
        systemverilog_package_elaborated.design->find_signal(
            "systemverilog_package_user.struct_payload");
    assert(systemverilog_struct_payload);
    const auto systemverilog_ascending_packet =
        systemverilog_package_elaborated.design->find_signal(
            "systemverilog_package_user.ascending_packet");
    assert(systemverilog_ascending_packet);
    const auto systemverilog_replicated =
        systemverilog_package_elaborated.design->find_signal(
            "systemverilog_package_user.replicated");
    assert(systemverilog_replicated);
    const auto systemverilog_signed_shift =
        systemverilog_package_elaborated.design->find_signal(
            "systemverilog_package_user.signed_shift");
    const auto systemverilog_unsigned_shift =
        systemverilog_package_elaborated.design->find_signal(
            "systemverilog_package_user.unsigned_shift");
    assert(systemverilog_signed_shift);
    assert(systemverilog_unsigned_shift);
    const auto& systemverilog_package_dependencies =
        systemverilog_package_elaborated.design
            ->specializations()
            .front()
            .source_dependencies;
    assert(std::find(
               systemverilog_package_dependencies.begin(),
               systemverilog_package_dependencies.end(),
               "systemverilog_base_package.sv")
           != systemverilog_package_dependencies.end());
    assert(std::find(
               systemverilog_package_dependencies.begin(),
               systemverilog_package_dependencies.end(),
               "systemverilog_derived_package.sv")
           != systemverilog_package_dependencies.end());
    auto systemverilog_package_interpreter =
        systemverilog_package_elaborated.design
            ->create_interpreter();
    assert(
        systemverilog_package_interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    assert(
        systemverilog_package_interpreter
            ->signal_value(*systemverilog_package_observed)
            .to_msb_string()
        == "0110");
    assert(
        systemverilog_package_interpreter
            ->signal_value(*systemverilog_package_packet)
            .to_msb_string()
        == "01101");
    assert(
        systemverilog_package_interpreter
            ->signal_value(*systemverilog_package_overlay)
            .to_msb_string()
        == "0110");
    assert(
        systemverilog_package_interpreter
            ->signal_value(*systemverilog_struct_payload)
            .to_msb_string()
        == "0110");
    assert(
        systemverilog_package_interpreter
            ->signal_value(*systemverilog_ascending_packet)
            .to_msb_string()
        == "0101");
    assert(
        systemverilog_package_interpreter
            ->signal_value(*systemverilog_replicated)
            .to_msb_string()
        == "1010");
    assert(
        systemverilog_package_interpreter
            ->signal_value(*systemverilog_signed_shift)
            .to_msb_string()
        == "1100");
    assert(
        systemverilog_package_interpreter
            ->signal_value(*systemverilog_unsigned_shift)
            .to_msb_string()
        == "0100");

    const auto invalid_systemverilog_packages =
        fsim::frontend::parse_text(
            "invalid_systemverilog_packages.sv",
            R"(
package first_values;
  import second_values::*;
  localparam int VALUE = OTHER + 1;
endpackage
package second_values;
  import first_values::*;
  localparam int OTHER = VALUE + 1;
endpackage
package duplicate_values;
  localparam int VALUE = 2;
endpackage
package alpha_values;
  localparam int SHARED = 3;
  typedef logic shared_t;
endpackage
package beta_values;
  localparam int SHARED = 4;
  typedef bit shared_t;
endpackage
package broken_values;
  localparam int BROKEN = 1 / 0;
endpackage
package invalid_enum_values;
  typedef enum logic [1:0] {
    ZERO = 0,
    DUPLICATE = 0,
    TOO_LARGE = 4
  } invalid_t;
endpackage
package invalid_struct_layout;
  typedef struct packed {
    logic [MISSING-1:0] payload;
  } invalid_packet_t;
endpackage
package invalid_union_layout;
  typedef union packed {
    logic [3:0] wide;
    logic [2:0] narrow;
  } invalid_union_t;
endpackage
import duplicate_values::MISSING;
import missing_values::*;
import first_values::*;
import alpha_values::*, beta_values::*;
import broken_values::*;
import invalid_enum_values::*;
import invalid_struct_layout::invalid_packet_t;
import invalid_union_layout::*;
module invalid_systemverilog_package_user;
  typedef struct packed {
    logic [3:0] field;
  } valid_packet_t;
  logic value;
  logic [3:0] invalid_replication;
  missing_t missing_value;
  shared_t ambiguous_value;
  typedef cycle_b cycle_a;
  typedef cycle_a cycle_b;
  cycle_a cyclic_value;
  invalid_packet_t invalid_packet;
  valid_packet_t valid_packet;
  initial valid_packet.field[2+2] = 1'b0;
  initial valid_packet.field[0 +: 0] = 1'b0;
  initial valid_packet.field[3 +: 2] = 2'b00;
  initial valid_packet.field[0 -: 2] = 2'b00;
  initial invalid_replication = {0{1'b0}};
  initial invalid_replication = {value{1'b0}};
  initial invalid_replication = {2147483648{2'b00}};
  assign value = first_values::second_values::VALUE;
  assign value = invalid_packet.payload;
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(invalid_systemverilog_packages.ok());
    const auto invalid_systemverilog_package_result =
        fsim::elaboration::elaborate(
            invalid_systemverilog_packages.design,
            "sv:work.invalid_systemverilog_package_user");
    assert(!invalid_systemverilog_package_result.ok());
    for (const auto code : {
             "FSIM-ELAB-SVPKG-001",
             "FSIM-ELAB-SVPKG-002",
             "FSIM-ELAB-SVPKG-003",
             "FSIM-ELAB-SVPKG-004",
             "FSIM-ELAB-SVPKG-005",
             "FSIM-ELAB-SVPKG-006",
             "FSIM-ELAB-SVTYPE-001",
             "FSIM-ELAB-SVTYPE-002",
             "FSIM-ELAB-SVTYPE-003",
             "FSIM-ELAB-SVENUM-001",
             "FSIM-ELAB-SVENUM-002",
             "FSIM-ELAB-SVSTRUCT-001",
             "FSIM-ELAB-SVSTRUCT-002",
             "FSIM-ELAB-SVUNION-001",
             "FSIM-ELAB-SVREPL-001",
             "FSIM-ELAB-068"}) {
        assert(has_diagnostic(
            invalid_systemverilog_package_result, code));
    }
    assert(
        std::count_if(
            invalid_systemverilog_package_result.diagnostics.begin(),
            invalid_systemverilog_package_result.diagnostics.end(),
            [](const auto& diagnostic) {
                return diagnostic.code == "FSIM-ELAB-068";
            })
        >= 4);

    const auto generic_parsed = fsim::frontend::parse_text(
        "generic-specialization.vhd",
        R"(
entity generic_counter is
  generic (
    width : positive := 8;
    increment : natural := 1;
    enabled : boolean := true;
    last : integer := width - 1
  );
  port (
    clk : in std_logic;
    q : out unsigned(last downto 0)
  );
end entity;

architecture rtl of generic_counter is
  signal next_value : unsigned(last downto 0);
begin
  next_value <= q + increment;
  update: process(clk)
  begin
    if rising_edge(clk) then
      if enabled then
        q <= next_value;
      else
        q <= q;
      end if;
    end if;
  end process;
end architecture;

entity generic_top is
  port (
    clk : in std_logic;
    q4 : out unsigned(3 downto 0);
    q8 : out unsigned(7 downto 0)
  );
end entity;

architecture rtl of generic_top is
begin
  four: entity work.generic_counter(rtl)
    generic map (
      width => 4,
      increment => 2
    )
    port map (
      clk => clk,
      q => q4
    );
  eight: entity work.generic_counter(rtl)
    generic map (
      8,
      increment => 3,
      enabled => false
    )
    port map (
      clk => clk,
      q => q8
    );
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(generic_parsed.ok());
    const auto generic_elaborated =
        fsim::elaboration::elaborate(
            generic_parsed.design,
            "vhdl:work.generic_top(rtl)");
    if (!generic_elaborated.ok()) {
        for (const auto& diagnostic :
             generic_elaborated.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(generic_elaborated.ok());
    assert(
        generic_elaborated.design->specializations().size() == 3);
    const auto& generic_specializations =
        generic_elaborated.design->specializations();
    assert(generic_specializations[0].parameter_values.empty());
    assert((
        generic_specializations[1].parameter_values
        == std::vector<std::pair<std::string, std::string>>{
            {"width", "4"},
            {"increment", "2"},
            {"enabled", "true"},
            {"last", "3"}}));
    assert((
        generic_specializations[2].parameter_values
        == std::vector<std::pair<std::string, std::string>>{
            {"width", "8"},
            {"increment", "3"},
            {"enabled", "false"},
            {"last", "7"}}));
    const auto generic_clock =
        generic_elaborated.design->find_signal("clk");
    const auto generic_q4 =
        generic_elaborated.design->find_signal("q4");
    const auto generic_q8 =
        generic_elaborated.design->find_signal("q8");
    const auto generic_next4 =
        generic_elaborated.design->find_signal(
            "generic_top.four.next_value");
    const auto generic_next8 =
        generic_elaborated.design->find_signal(
            "generic_top.eight.next_value");
    assert(
        generic_clock && generic_q4 && generic_q8
        && generic_next4 && generic_next8);
    assert(
        generic_elaborated.design->signals()
            .at(*generic_q4).width
        == 4);
    assert(
        generic_elaborated.design->signals()
            .at(*generic_q8).width
        == 8);
    assert(
        generic_elaborated.design->signals()
            .at(*generic_next4).width
        == 4);
    assert(
        generic_elaborated.design->signals()
            .at(*generic_next8).width
        == 8);
    auto generic_interpreter =
        generic_elaborated.design->create_interpreter();
    generic_interpreter->deposit_signal(
        *generic_clock,
        fsim::runtime::PackedLogic4::from_msb_string("0"));
    generic_interpreter->deposit_signal(
        *generic_q4,
        fsim::runtime::PackedLogic4::from_msb_string("0000"));
    generic_interpreter->deposit_signal(
        *generic_q8,
        fsim::runtime::PackedLogic4::from_msb_string("00000000"));
    generic_interpreter->start();
    (void)generic_interpreter->run();
    generic_interpreter->deposit_signal(
        *generic_clock,
        fsim::runtime::PackedLogic4::from_msb_string("1"));
    (void)generic_interpreter->run();
    assert(
        generic_interpreter
            ->signal_value(*generic_q4)
            .to_msb_string()
        == "0010");
    assert(
        generic_interpreter
            ->signal_value(*generic_q8)
            .to_msb_string()
        == "00000000");

    const auto package_base_source =
        fsim::frontend::parse_text(
            "package_base_constants.vhd",
            R"(
package base_constants is
  constant base_width : natural := 4;
  constant base_value : natural := 5;
end package base_constants;
)",
            fsim::frontend::Language::Vhdl2008);
    const auto package_source = fsim::frontend::parse_text(
        "package_constants.vhd",
        R"(
use work.base_constants.all;
package constants is
  constant width : natural := base_width;
  constant next_value : natural := base_value + 1;
end package constants;
)",
        fsim::frontend::Language::Vhdl2008);
    const auto package_base_context =
        fsim::frontend::parse_text(
            "package_base_context.vhd",
            R"(
context package_base_context is
  library work;
  use work.constants.all;
end context package_base_context;
)",
            fsim::frontend::Language::Vhdl2008);
    const auto package_context = fsim::frontend::parse_text(
        "package_context.vhd",
        R"(
context package_context is
  context work.package_base_context;
end context package_context;
)",
        fsim::frontend::Language::Vhdl2008);
    const auto package_user_source =
        fsim::frontend::parse_text(
            "package_user.vhd",
            R"(
context work.package_context;
entity package_user is
  port (observed : out unsigned(width - 1 downto 0));
end entity package_user;

context work.package_context;
architecture rtl of package_user is
  signal local_value : unsigned(width - 1 downto 0);
begin
  local_value <= next_value;
  observed <= local_value + 1;
end architecture rtl;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(package_base_source.ok());
    assert(package_source.ok());
    assert(package_base_context.ok());
    assert(package_context.ok());
    assert(package_user_source.ok());
    fsim::frontend::ParsedDesign package_design =
        package_base_source.design;
    package_design.units.insert(
        package_design.units.end(),
        package_source.design.units.begin(),
        package_source.design.units.end());
    package_design.units.insert(
        package_design.units.end(),
        package_base_context.design.units.begin(),
        package_base_context.design.units.end());
    package_design.units.insert(
        package_design.units.end(),
        package_context.design.units.begin(),
        package_context.design.units.end());
    package_design.units.insert(
        package_design.units.end(),
        package_user_source.design.units.begin(),
        package_user_source.design.units.end());
    const auto package_elaborated =
        fsim::elaboration::elaborate(
            package_design,
            "vhdl:work.package_user(rtl)");
    if (!package_elaborated.ok()) {
        for (const auto& diagnostic :
             package_elaborated.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(package_elaborated.ok());
    const auto package_observed =
        package_elaborated.design->find_signal("observed");
    const auto package_local =
        package_elaborated.design->find_signal("local_value");
    assert(package_observed && package_local);
    assert(
        package_elaborated.design->signals()
            .at(*package_observed).width
        == 4);
    const auto& package_specialization =
        package_elaborated.design->specializations().front();
    assert(std::find(
               package_specialization.source_dependencies.begin(),
               package_specialization.source_dependencies.end(),
               "package_constants.vhd")
           != package_specialization.source_dependencies.end());
    assert(std::find(
               package_specialization.source_dependencies.begin(),
               package_specialization.source_dependencies.end(),
               "package_base_constants.vhd")
           != package_specialization.source_dependencies.end());
    assert(std::find(
               package_specialization.source_dependencies.begin(),
               package_specialization.source_dependencies.end(),
               "package_base_context.vhd")
           != package_specialization.source_dependencies.end());
    assert(std::find(
               package_specialization.source_dependencies.begin(),
               package_specialization.source_dependencies.end(),
               "package_context.vhd")
           != package_specialization.source_dependencies.end());
    auto package_interpreter =
        package_elaborated.design->create_interpreter();
    assert(
        package_interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    assert(
        package_interpreter
            ->signal_value(*package_local)
            .to_msb_string()
        == "0110");
    assert(
        package_interpreter
            ->signal_value(*package_observed)
            .to_msb_string()
        == "0111");

    auto cross_library_context_design = package_design;
    for (auto& unit : cross_library_context_design.units) {
        if (unit.kind
                == fsim::frontend::UnitKind::VhdlPackage
            || unit.kind
                == fsim::frontend::UnitKind::VhdlContext) {
            unit.library = "support";
            continue;
        }
        for (auto& item : unit.vhdl_context) {
            for (auto& selected_name : item.selected_names) {
                if (selected_name == "work.package_context") {
                    selected_name = "support.package_context";
                }
            }
        }
    }
    const auto cross_library_context_elaborated =
        fsim::elaboration::elaborate(
            cross_library_context_design,
            "vhdl:work.package_user(rtl)");
    assert(cross_library_context_elaborated.ok());
    const auto cross_library_context_observed =
        cross_library_context_elaborated.design->find_signal(
            "observed");
    assert(cross_library_context_observed);
    auto cross_library_context_interpreter =
        cross_library_context_elaborated.design
            ->create_interpreter();
    assert(
        cross_library_context_interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    assert(
        cross_library_context_interpreter
            ->signal_value(*cross_library_context_observed)
            .to_msb_string()
        == "0111");

    const auto selected_package_user =
        fsim::frontend::parse_text(
            "selected_package_user.vhd",
            R"(
entity selected_package_user is
  port (
    observed : out unsigned(
      work.constants.width - 1 downto 0)
  );
end entity selected_package_user;
architecture rtl of selected_package_user is
  signal local_value : unsigned(
    constants.width - 1 downto 0);
begin
  local_value <= constants.next_value;
  observed <= local_value + 1;
end architecture rtl;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(selected_package_user.ok());
    auto selected_package_design = package_design;
    selected_package_design.units.insert(
        selected_package_design.units.end(),
        selected_package_user.design.units.begin(),
        selected_package_user.design.units.end());
    const auto selected_package_elaborated =
        fsim::elaboration::elaborate(
            selected_package_design,
            "vhdl:work.selected_package_user(rtl)");
    assert(selected_package_elaborated.ok());
    const auto selected_package_observed =
        selected_package_elaborated.design->find_signal(
            "observed");
    assert(selected_package_observed);
    assert(
        selected_package_elaborated.design->signals()
            .at(*selected_package_observed).width
        == 4);
    const auto& selected_package_dependencies =
        selected_package_elaborated.design
            ->specializations()
            .front()
            .source_dependencies;
    assert(std::find(
               selected_package_dependencies.begin(),
               selected_package_dependencies.end(),
               "package_constants.vhd")
           != selected_package_dependencies.end());
    assert(std::find(
               selected_package_dependencies.begin(),
               selected_package_dependencies.end(),
               "package_base_constants.vhd")
           != selected_package_dependencies.end());
    auto selected_package_interpreter =
        selected_package_elaborated.design
            ->create_interpreter();
    assert(
        selected_package_interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    assert(
        selected_package_interpreter
            ->signal_value(*selected_package_observed)
            .to_msb_string()
        == "0111");

    const auto invalid_selected_package_user =
        fsim::frontend::parse_text(
            "invalid_selected_package_user.vhd",
            R"(
entity invalid_selected_package_user is
end entity invalid_selected_package_user;
architecture rtl of invalid_selected_package_user is
  signal first : std_logic;
  signal second : std_logic;
  signal third : std_logic;
begin
  first <= work.not_present.value;
  second <= work.constants.not_present;
  third <= too.many.selected.name;
end architecture rtl;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(invalid_selected_package_user.ok());
    auto invalid_selected_package_design = package_design;
    invalid_selected_package_design.units.insert(
        invalid_selected_package_design.units.end(),
        invalid_selected_package_user.design.units.begin(),
        invalid_selected_package_user.design.units.end());
    const auto invalid_selected_package_result =
        fsim::elaboration::elaborate(
            invalid_selected_package_design,
            "vhdl:work.invalid_selected_package_user(rtl)");
    assert(!invalid_selected_package_result.ok());
    assert(has_diagnostic(
        invalid_selected_package_result,
        "FSIM-ELAB-PKG-008"));
    assert(has_diagnostic(
        invalid_selected_package_result,
        "FSIM-ELAB-PKG-009"));
    assert(has_diagnostic(
        invalid_selected_package_result,
        "FSIM-ELAB-PKG-010"));

    const auto missing_package = fsim::frontend::parse_text(
        "missing_package.vhd",
        R"(
use work.not_present.all;
entity missing_package is
end entity missing_package;
architecture rtl of missing_package is
begin
end architecture rtl;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(missing_package.ok());
    const auto missing_package_result =
        fsim::elaboration::elaborate(
            missing_package.design,
            "vhdl:work.missing_package(rtl)");
    assert(!missing_package_result.ok());
    assert(has_diagnostic(
        missing_package_result, "FSIM-ELAB-PKG-002"));

    const auto missing_constant = fsim::frontend::parse_text(
        "missing_package_constant.vhd",
        R"(
package values is
  constant present : natural := 1;
end package values;
use work.values.absent;
entity missing_package_constant is
end entity missing_package_constant;
architecture rtl of missing_package_constant is
begin
end architecture rtl;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(missing_constant.ok());
    const auto missing_constant_result =
        fsim::elaboration::elaborate(
            missing_constant.design,
            "vhdl:work.missing_package_constant(rtl)");
    assert(!missing_constant_result.ok());
    assert(has_diagnostic(
        missing_constant_result, "FSIM-ELAB-PKG-003"));

    const auto ambiguous_constant = fsim::frontend::parse_text(
        "ambiguous_package_constant.vhd",
        R"(
package first_values is
  constant width : natural := 1;
end package first_values;
package second_values is
  constant width : natural := 2;
end package second_values;
use work.first_values.all;
use work.second_values.all;
entity ambiguous_package_constant is
end entity ambiguous_package_constant;
architecture rtl of ambiguous_package_constant is
begin
end architecture rtl;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(ambiguous_constant.ok());
    const auto ambiguous_constant_result =
        fsim::elaboration::elaborate(
            ambiguous_constant.design,
            "vhdl:work.ambiguous_package_constant(rtl)");
    assert(!ambiguous_constant_result.ok());
    assert(has_diagnostic(
        ambiguous_constant_result, "FSIM-ELAB-PKG-004"));

    const auto malformed_import = fsim::frontend::parse_text(
        "malformed_package_import.vhd",
        R"(
use work.values;
entity malformed_package_import is
end entity malformed_package_import;
architecture rtl of malformed_package_import is
begin
end architecture rtl;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(malformed_import.ok());
    const auto malformed_import_result =
        fsim::elaboration::elaborate(
            malformed_import.design,
            "vhdl:work.malformed_package_import(rtl)");
    assert(!malformed_import_result.ok());
    assert(has_diagnostic(
        malformed_import_result, "FSIM-ELAB-PKG-001"));

    const auto invalid_package_values =
        fsim::frontend::parse_text(
            "invalid_package_values.vhd",
            R"(
package invalid_values is
  constant missing_dependency : natural := absent + 1;
  constant invalid_positive : positive := 0;
end package invalid_values;
use work.invalid_values.all;
entity invalid_package_values is
end entity invalid_package_values;
architecture rtl of invalid_package_values is
begin
end architecture rtl;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(invalid_package_values.ok());
    const auto invalid_package_values_result =
        fsim::elaboration::elaborate(
            invalid_package_values.design,
            "vhdl:work.invalid_package_values(rtl)");
    assert(!invalid_package_values_result.ok());
    assert(has_diagnostic(
        invalid_package_values_result, "FSIM-ELAB-PKG-005"));
    assert(has_diagnostic(
        invalid_package_values_result, "FSIM-ELAB-PKG-006"));

    const auto cyclic_packages = fsim::frontend::parse_text(
        "cyclic_packages.vhd",
        R"(
use work.second_values.all;
package first_values is
  constant first : natural := second + 1;
end package first_values;
use work.first_values.all;
package second_values is
  constant second : natural := first + 1;
end package second_values;
use work.first_values.all;
entity cyclic_package_user is
end entity cyclic_package_user;
architecture rtl of cyclic_package_user is
begin
end architecture rtl;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(cyclic_packages.ok());
    const auto cyclic_package_result =
        fsim::elaboration::elaborate(
            cyclic_packages.design,
            "vhdl:work.cyclic_package_user(rtl)");
    assert(!cyclic_package_result.ok());
    assert(has_diagnostic(
        cyclic_package_result, "FSIM-ELAB-PKG-007"));

    const auto invalid_contexts = fsim::frontend::parse_text(
        "invalid_contexts.vhd",
        R"(
context first_context is
  context work.second_context;
end context first_context;
context second_context is
  context work.first_context;
end context second_context;
context work.first_context;
context work.too.many.parts;
context work.not_present;
entity invalid_context_user is
end entity invalid_context_user;
architecture rtl of invalid_context_user is
begin
end architecture rtl;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(invalid_contexts.ok());
    const auto invalid_context_result =
        fsim::elaboration::elaborate(
            invalid_contexts.design,
            "vhdl:work.invalid_context_user(rtl)");
    assert(!invalid_context_result.ok());
    assert(has_diagnostic(
        invalid_context_result, "FSIM-ELAB-CTX-001"));
    assert(has_diagnostic(
        invalid_context_result, "FSIM-ELAB-CTX-002"));
    assert(has_diagnostic(
        invalid_context_result, "FSIM-ELAB-CTX-003"));

    const auto invalid_generics = fsim::frontend::parse_text(
        "invalid-generic-elaboration.vhd",
        R"(
entity invalid_generic_target is
  generic (
    required_value : integer;
    natural_value : natural := -1;
    broken_value : integer := missing_value
  );
end entity;
architecture rtl of invalid_generic_target is
begin
end architecture;
entity invalid_generic_top is
end entity;
architecture rtl of invalid_generic_top is
begin
  unknown: entity work.invalid_generic_target(rtl)
    generic map (missing => 2)
    port map ();
  excessive: entity work.invalid_generic_target(rtl)
    generic map (1, 2, 3, 4)
    port map ();
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(invalid_generics.ok());
    const auto rejected_generics =
        fsim::elaboration::elaborate(
            invalid_generics.design,
            "vhdl:work.invalid_generic_top(rtl)");
    assert(!rejected_generics.ok());
    assert(has_diagnostic(
        rejected_generics, "FSIM-ELAB-GENERIC-001"));
    assert(has_diagnostic(
        rejected_generics, "FSIM-ELAB-GENERIC-005"));
    assert(has_diagnostic(
        rejected_generics, "FSIM-ELAB-GENERIC-008"));

    auto mixed_actual_sv = fsim::frontend::parse_text(
        "mixed-actuals.sv",
        R"(
module sv_generic_host(output logic [3:0] q);
  vhdl_bound #(.WIDTH(4), .VALUE(5)) child(.q(q));
endmodule

module sv_parameter_child #(
  parameter width = 1,
  parameter value = 1,
  localparam last = width - 1
) (
  output logic [last:0] q
);
  initial q = value;
endmodule

module ambiguous_parameter_child #(
  parameter WIDTH = 1,
  parameter width = 2
) (
  output logic q
);
  initial q = 1'b0;
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    auto mixed_actual_vhdl = fsim::frontend::parse_text(
        "mixed-actuals.vhd",
        R"(
entity vhdl_generic_child is
  generic (
    width : positive := 1;
    value : natural := 1;
    last : integer := width - 1
  );
  port (
    q : out unsigned(last downto 0)
  );
end entity;
architecture rtl of vhdl_generic_child is
begin
  q <= value;
end architecture;

entity vhdl_parameter_host is
  port (
    q : out unsigned(3 downto 0)
  );
end entity;
architecture rtl of vhdl_parameter_host is
begin
  child: entity work.foreign_parameter(rtl)
    generic map (
      4,
      value => 6
    )
    port map (
      q => q
    );
end architecture;

entity ambiguous_parameter_host is
  port (
    q : out std_logic
  );
end entity;
architecture rtl of ambiguous_parameter_host is
begin
  child: entity work.foreign_ambiguous(rtl)
    generic map (
      width => 1
    )
    port map (
      q => q
    );
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(mixed_actual_sv.ok());
    assert(mixed_actual_vhdl.ok());
    fsim::frontend::ParsedDesign mixed_actual_design =
        std::move(mixed_actual_sv.design);
    mixed_actual_design.units.insert(
        mixed_actual_design.units.end(),
        std::make_move_iterator(
            mixed_actual_vhdl.design.units.begin()),
        std::make_move_iterator(
            mixed_actual_vhdl.design.units.end()));

    const std::vector<fsim::elaboration::Binding>
        sv_to_vhdl_actual_binding{
            {"sv_generic_host.child",
             "vhdl:work.vhdl_generic_child(rtl)",
             std::nullopt}};
    const auto sv_to_vhdl_actual =
        fsim::elaboration::elaborate(
            mixed_actual_design,
            "sv:work.sv_generic_host",
            sv_to_vhdl_actual_binding);
    if (!sv_to_vhdl_actual.ok()) {
        for (const auto& diagnostic :
             sv_to_vhdl_actual.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(sv_to_vhdl_actual.ok());
    assert(
        sv_to_vhdl_actual.design->specializations().size() == 2);
    assert((
        sv_to_vhdl_actual.design->specializations()[1]
            .parameter_values
        == std::vector<std::pair<std::string, std::string>>{
            {"width", "4"},
            {"value", "5"},
            {"last", "3"}}));
    const auto sv_to_vhdl_q =
        sv_to_vhdl_actual.design->find_signal("q");
    assert(sv_to_vhdl_q);
    auto sv_to_vhdl_interpreter =
        sv_to_vhdl_actual.design->create_interpreter();
    const auto sv_to_vhdl_result =
        sv_to_vhdl_interpreter->run();
    assert(
        sv_to_vhdl_result.status
        == fsim::runtime::RunStatus::completed);
    assert(
        sv_to_vhdl_interpreter
            ->signal_value(*sv_to_vhdl_q)
            .to_msb_string()
        == "0101");

    const std::vector<fsim::elaboration::Binding>
        vhdl_to_sv_actual_binding{
            {"vhdl_parameter_host.child",
             "sv:work.sv_parameter_child",
             std::nullopt}};
    const auto vhdl_to_sv_actual =
        fsim::elaboration::elaborate(
            mixed_actual_design,
            "vhdl:work.vhdl_parameter_host(rtl)",
            vhdl_to_sv_actual_binding);
    if (!vhdl_to_sv_actual.ok()) {
        for (const auto& diagnostic :
             vhdl_to_sv_actual.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(vhdl_to_sv_actual.ok());
    assert(
        vhdl_to_sv_actual.design->specializations().size() == 2);
    assert((
        vhdl_to_sv_actual.design->specializations()[1]
            .parameter_values
        == std::vector<std::pair<std::string, std::string>>{
            {"width", "4"},
            {"value", "6"},
            {"last", "3"}}));
    const auto vhdl_to_sv_q =
        vhdl_to_sv_actual.design->find_signal("q");
    assert(vhdl_to_sv_q);
    auto vhdl_to_sv_interpreter =
        vhdl_to_sv_actual.design->create_interpreter();
    const auto vhdl_to_sv_result =
        vhdl_to_sv_interpreter->run();
    assert(
        vhdl_to_sv_result.status
        == fsim::runtime::RunStatus::completed);
    assert(
        vhdl_to_sv_interpreter
            ->signal_value(*vhdl_to_sv_q)
            .to_msb_string()
        == "0110");

    const std::vector<fsim::elaboration::Binding>
        ambiguous_actual_binding{
            {"ambiguous_parameter_host.child",
             "sv:work.ambiguous_parameter_child",
             std::nullopt}};
    const auto ambiguous_actual =
        fsim::elaboration::elaborate(
            mixed_actual_design,
            "vhdl:work.ambiguous_parameter_host(rtl)",
            ambiguous_actual_binding);
    assert(!ambiguous_actual.ok());
    assert(has_diagnostic(
        ambiguous_actual, "FSIM-ELAB-PARAM-009"));
}

} // namespace fsim::tests::elaboration
