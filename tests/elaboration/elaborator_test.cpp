// SPDX-License-Identifier: Apache-2.0
#include "fsim/elaboration/elaborator.hpp"
#include "fsim/frontend/frontend.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <iostream>
#include <iterator>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

bool has_diagnostic(
    const fsim::elaboration::ElaborationResult& result,
    const std::string_view code) {
    for (const auto& diagnostic : result.diagnostics) {
        if (diagnostic.code == code) {
            return true;
        }
    }
    return false;
}

class TestSystemCFactoryProvider final
    : public fsim::elaboration::SystemCFactoryProvider {
public:
    std::vector<fsim::elaboration::SystemCConstructionParameter>
        parameters;
    fsim::elaboration::SystemCInstanceDescription prototype;
    std::vector<std::pair<std::string, std::int64_t>>
        last_values;
    std::uint64_t next_handle{10'000};
    std::string schema_failure;
    std::string construction_failure;

    std::optional<std::vector<
        fsim::elaboration::SystemCConstructionParameter>>
    schema(
        std::string_view,
        std::string& error) override {
        if (!schema_failure.empty()) {
            error = schema_failure;
            return std::nullopt;
        }
        error.clear();
        return parameters;
    }

    std::optional<fsim::elaboration::SystemCInstanceDescription>
    instantiate(
        const std::string_view path,
        const std::string_view target,
        const std::span<
            const std::pair<std::string, std::int64_t>> values,
        std::string& error) override {
        if (!construction_failure.empty()) {
            error = construction_failure;
            return std::nullopt;
        }
        error.clear();
        auto result = prototype;
        result.path = path;
        result.target = target;
        result.handle = next_handle++;
        result.construction_values.assign(
            values.begin(), values.end());
        last_values = result.construction_values;
        const auto width = std::find_if(
            values.begin(),
            values.end(),
            [](const auto& value) {
                return value.first == "WIDTH";
            });
        if (width != values.end() && width->second > 0) {
            for (auto& port : result.ports) {
                if (port.name == "value" && width->second > 1) {
                    port.type.packed_range =
                        fsim::frontend::PackedRange{
                            width->second - 1, 0, true};
                }
            }
        }
        return result;
    }
};

} // namespace

int main() {
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

    auto generated_sv = fsim::frontend::parse_text(
        "generated-mixed.sv",
        R"(
module generated_sv_leaf #(
  parameter VALUE = 3
) (
  output logic [3:0] q
);
  initial q = VALUE;
endmodule

module generated_sv_internal_leaf #(
  parameter VALUE = 0
);
  logic [3:0] q;
  initial q = VALUE;
endmodule

module generated_sv_true #(
  parameter ENABLED = 1
) (
  output logic [3:0] q
);
  generate
    if (ENABLED) begin : foreign_branch
      for (genvar j = 0; j < 1; j = j + 1) begin : nested_lane
        generated_foreign #(.VALUE(j + 9)) child(.q(q));
      end
    end else begin : local_branch
      if (1) begin : nested_branch
        generated_sv_leaf #(.VALUE(3)) child(.q(q));
      end else begin : unused_nested_branch
        generated_sv_leaf #(.VALUE(4)) child(.q(q));
      end
    end
  endgenerate
endmodule

module generated_sv_false #(
  parameter ENABLED = 0
) (
  output logic [3:0] q
);
  generate
    if (ENABLED) begin : foreign_branch
      generated_foreign #(.VALUE(9)) child(.q(q));
    end else begin : local_branch
      if (1) begin : nested_branch
        generated_sv_leaf #(.VALUE(3)) child(.q(q));
      end else begin : unused_nested_branch
        generated_sv_leaf #(.VALUE(4)) child(.q(q));
      end
    end
  endgenerate
endmodule

module generated_sv_loop #(
  parameter COUNT = 3
);
  genvar i;
  generate
    for (i = 0; i < COUNT; i++) begin : lanes
      generated_vhdl_loop_bound #(.VALUE(i + 5)) child();
    end
  endgenerate
endmodule

module generated_shadow_loop;
  generate
    for (genvar i = 0; i < 1; i = i + 1) begin : outer
      for (genvar i = 0; i < 1; i = i + 1) begin : inner
        generated_sv_internal_leaf child();
      end
    end
  endgenerate
endmodule

module generated_sv_case_selected #(
  parameter MODE = 2
);
  generate
    case (MODE)
      0: begin : zero
        generated_sv_internal_leaf #(.VALUE(1)) child();
      end
      1, 2: begin : selected
        generated_case_foreign #(.VALUE(8)) child();
      end
      default: begin : fallback
        generated_sv_internal_leaf #(.VALUE(4)) child();
      end
    endcase
  endgenerate
endmodule

module generated_sv_case_default #(
  parameter MODE = 9
);
  generate
    case (MODE)
      0: begin : zero
        generated_sv_internal_leaf #(.VALUE(1)) child();
      end
      default: begin : fallback
        generated_sv_internal_leaf #(.VALUE(4)) child();
      end
    endcase
  endgenerate
endmodule

module generated_sv_behavior #(
  parameter ENABLED = 1
) (
  output logic [3:0] observed
);
  generate
    if (ENABLED) begin : selected
      logic [3:0] generated_value;
      assign generated_value = 4'd5;
      always_comb observed = generated_value + 1;
    end else begin : fallback
      assign observed = 4'd1;
    end
  endgenerate
endmodule

module generated_sv_loop_behavior #(parameter COUNT = 3);
  genvar i;
  generate
    for (i = COUNT - 1; i >= 0; i--) begin : lane
      localparam int LOCAL_VALUE = i + 1;
      logic [3:0] generated_value;
      initial generated_value = LOCAL_VALUE;
    end
  endgenerate
endmodule

module generated_sv_implicit_behavior #(
  parameter ENABLED = 1
) (
  output logic [3:0] observed
);
  if (ENABLED) begin : implicit_scope
    localparam int BASE_VALUE = 5;
    parameter int GENERATED_VALUE = BASE_VALUE + 1;
    logic [3:0] generated_value;
    assign generated_value = GENERATED_VALUE;
    always_comb observed = generated_value + 1;
  end
endmodule

module generated_sv_direct_behavior (
  output logic [3:0] observed
);
  generate
    localparam int DIRECT_BASE = 1;
    logic [3:0] direct_value;
    assign direct_value = DIRECT_BASE + 1;
    begin : named_scope
      localparam int NESTED_OFFSET = DIRECT_BASE;
      logic [3:0] nested_value;
      assign nested_value = direct_value + NESTED_OFFSET;
      always_comb observed = nested_value + 1;
    end
  endgenerate
endmodule

module generated_sv_bad_constant;
  generate
    if (1) begin : selected
      localparam int BAD_VALUE = 1 / 0;
    end
  endgenerate
endmodule

module generated_sv_wide_constant;
  generate
    if (1) begin : selected
      localparam logic [64:0] WIDE_VALUE = 0;
    end
  endgenerate
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    auto generated_vhdl = fsim::frontend::parse_text(
        "generated-mixed.vhd",
        R"(
entity generated_vhdl_leaf is
  generic (
    value : natural := 1
  );
  port (
    q : out unsigned(3 downto 0)
  );
end entity;
architecture rtl of generated_vhdl_leaf is
begin
  q <= value;
end architecture;

entity generated_vhdl_internal_leaf is
  generic (
    value : natural := 0
  );
end entity;
architecture rtl of generated_vhdl_internal_leaf is
  signal q : unsigned(3 downto 0);
begin
  q <= value;
end architecture;

entity generated_vhdl_top is
  generic (
    enabled : boolean := true
  );
  port (
    q : out unsigned(3 downto 0)
  );
end entity;
architecture rtl of generated_vhdl_top is
begin
  selection: if enabled generate
    nested: if enabled generate
      child: entity work.generated_foreign(rtl)
        generic map (
          value => 6
        )
        port map (
          q => q
        );
    else generate
      child: entity work.generated_vhdl_leaf(rtl)
        generic map (
          value => 7
        )
        port map (
          q => q
        );
    end generate nested;
  else generate
    child: entity work.generated_vhdl_leaf(rtl)
      generic map (
        value => 2
      )
      port map (
        q => q
      );
  end generate selection;
end architecture;

entity generated_vhdl_loop_top is
  generic (
    count : positive := 3
  );
end entity;
architecture rtl of generated_vhdl_loop_top is
begin
  lanes: for i in count - 1 downto 0 generate
    child: entity work.generated_sv_loop_bound(rtl)
      generic map (
        value => i + 4
      )
      port map ();
  end generate lanes;
end architecture;

entity generated_vhdl_case_top is
  generic (
    mode : integer := 6
  );
end entity;
architecture rtl of generated_vhdl_case_top is
begin
  selection: case mode generate
    zero: when 0 =>
      child: entity work.generated_vhdl_internal_leaf(rtl)
        generic map (
          value => 1
        )
        port map ();
    selected: when 1 to 2 | 7 downto 5 =>
      child: entity work.generated_case_foreign(rtl)
        generic map (
          value => 7
        )
        port map ();
    empty_choice: when 3 to 1 =>
      child: entity work.generated_vhdl_internal_leaf(rtl)
        generic map (
          value => 15
        )
        port map ();
    fallback: when others =>
      child: entity work.generated_vhdl_internal_leaf(rtl)
        generic map (
          value => 3
        )
        port map ();
  end generate selection;
end architecture;

entity generated_vhdl_overlapping_ranges is
end entity;
architecture rtl of generated_vhdl_overlapping_ranges is
begin
  selection: case 2 generate
    first_choice: when 0 to 2 =>
    second_choice: when 2 to 4 =>
  end generate selection;
end architecture;

entity generated_vhdl_bad_range is
end entity;
architecture rtl of generated_vhdl_bad_range is
begin
  selection: case 2 generate
    invalid_choice: when 0 to missing_bound =>
  end generate selection;
end architecture;

entity generated_vhdl_behavior is
  generic (
    enabled : boolean := true
  );
  port (
    observed : out unsigned(3 downto 0)
  );
end entity;
architecture rtl of generated_vhdl_behavior is
begin
  chosen: if enabled generate
    signal generated_value : unsigned(3 downto 0);
  begin
    generated_value <= 6;
    worker: process(generated_value)
    begin
      observed <= generated_value + 1;
    end process;
  else generate
    observed <= 1;
  end generate chosen;
end architecture;

entity generated_vhdl_loop_behavior is
end entity;
architecture rtl of generated_vhdl_loop_behavior is
begin
  lanes: for i in 0 to 2 generate
    constant local_value : natural := i + 4;
    signal generated_value : unsigned(3 downto 0);
  begin
    generated_value <= local_value;
  end generate lanes;
end architecture;

entity generated_vhdl_block_behavior is
  port (
    observed : out unsigned(3 downto 0)
  );
end entity;
architecture rtl of generated_vhdl_block_behavior is
begin
  static_scope: block is
    constant base_value : natural := 6;
    constant local_value : natural := base_value + 1;
    signal generated_value : unsigned(3 downto 0);
  begin
    generated_value <= local_value;
    worker: process(generated_value)
    begin
      observed <= generated_value + 1;
    end process;
  end block static_scope;
end architecture;

entity generated_vhdl_bad_constant is
end entity;
architecture rtl of generated_vhdl_bad_constant is
begin
  invalid_scope: block
    constant bad_value : positive := 0;
  begin
  end block invalid_scope;
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(generated_sv.ok());
    assert(generated_vhdl.ok());
    fsim::frontend::ParsedDesign generated_design =
        std::move(generated_sv.design);
    generated_design.units.insert(
        generated_design.units.end(),
        std::make_move_iterator(
            generated_vhdl.design.units.begin()),
        std::make_move_iterator(
            generated_vhdl.design.units.end()));

    const std::vector<fsim::elaboration::Binding>
        generated_sv_binding{
            {"generated_sv_true.foreign_branch.nested_lane[0].child",
             "vhdl:work.generated_vhdl_leaf(rtl)",
             std::nullopt},
        };
    const auto generated_sv_true =
        fsim::elaboration::elaborate(
            generated_design,
            "sv:work.generated_sv_true",
            generated_sv_binding);
    assert(generated_sv_true.ok());
    assert(
        generated_sv_true.design->specializations().size() == 2);
    assert(
        generated_sv_true.design->specializations()[1].instance
        == "generated_sv_true.foreign_branch.nested_lane[0].child");
    const auto generated_sv_true_q =
        generated_sv_true.design->find_signal("q");
    assert(generated_sv_true_q);
    auto generated_sv_true_interpreter =
        generated_sv_true.design->create_interpreter();
    assert(
        generated_sv_true_interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    assert(
        generated_sv_true_interpreter
            ->signal_value(*generated_sv_true_q)
            .to_msb_string()
        == "1001");

    const auto generated_sv_false =
        fsim::elaboration::elaborate(
            generated_design,
            "sv:work.generated_sv_false");
    assert(generated_sv_false.ok());
    assert(
        generated_sv_false.design->specializations()[1].instance
        == "generated_sv_false.local_branch.nested_branch.child");
    assert(
        !generated_sv_false.design->find_signal(
            "generated_sv_false.foreign_branch.child.q"));
    const auto generated_sv_false_q =
        generated_sv_false.design->find_signal("q");
    assert(generated_sv_false_q);
    auto generated_sv_false_interpreter =
        generated_sv_false.design->create_interpreter();
    assert(
        generated_sv_false_interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    assert(
        generated_sv_false_interpreter
            ->signal_value(*generated_sv_false_q)
            .to_msb_string()
        == "0011");

    const std::vector<fsim::elaboration::Binding>
        generated_vhdl_binding{
            {"generated_vhdl_top.selection.nested.child",
             "sv:work.generated_sv_leaf",
             std::nullopt},
        };
    const auto generated_vhdl_top =
        fsim::elaboration::elaborate(
            generated_design,
            "vhdl:work.generated_vhdl_top(rtl)",
            generated_vhdl_binding);
    assert(generated_vhdl_top.ok());
    assert(
        generated_vhdl_top.design->specializations()[1].instance
        == "generated_vhdl_top.selection.nested.child");
    assert((
        generated_vhdl_top.design->specializations()[1]
            .parameter_values
        == std::vector<std::pair<std::string, std::string>>{
            {"VALUE", "6"}}));
    const auto generated_vhdl_q =
        generated_vhdl_top.design->find_signal("q");
    assert(generated_vhdl_q);
    auto generated_vhdl_interpreter =
        generated_vhdl_top.design->create_interpreter();
    assert(
        generated_vhdl_interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    assert(
        generated_vhdl_interpreter
            ->signal_value(*generated_vhdl_q)
            .to_msb_string()
        == "0110");

    const std::vector<fsim::elaboration::Binding>
        generated_sv_loop_bindings{
            {"generated_sv_loop.lanes[0].child",
             "vhdl:work.generated_vhdl_internal_leaf(rtl)",
             std::nullopt},
            {"generated_sv_loop.lanes[1].child",
             "vhdl:work.generated_vhdl_internal_leaf(rtl)",
             std::nullopt},
            {"generated_sv_loop.lanes[2].child",
             "vhdl:work.generated_vhdl_internal_leaf(rtl)",
             std::nullopt},
        };
    const auto generated_sv_loop =
        fsim::elaboration::elaborate(
            generated_design,
            "sv:work.generated_sv_loop",
            generated_sv_loop_bindings);
    assert(generated_sv_loop.ok());
    assert(generated_sv_loop.design->specializations().size() == 4);
    for (std::size_t index = 0; index < 3; ++index) {
      const auto path =
          "generated_sv_loop.lanes[" + std::to_string(index)
          + "].child";
      const auto& specialization =
          generated_sv_loop.design->specializations()[index + 1];
      assert(specialization.instance == path);
      assert((
          specialization.parameter_values
          == std::vector<std::pair<std::string, std::string>>{
              {"value", std::to_string(index + 5)}}));
    }
    auto generated_sv_loop_interpreter =
        generated_sv_loop.design->create_interpreter();
    assert(
        generated_sv_loop_interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    const std::array<std::string_view, 3> sv_loop_values{
        "0101", "0110", "0111"};
    for (std::size_t index = 0; index < 3; ++index) {
      const auto signal = generated_sv_loop.design->find_signal(
          "generated_sv_loop.lanes[" + std::to_string(index)
          + "].child.q");
      assert(signal);
      assert(
          generated_sv_loop_interpreter
              ->signal_value(*signal)
              .to_msb_string()
          == sv_loop_values[index]);
    }
    auto empty_generated_loop_design = generated_design;
    const auto empty_loop_unit = std::find_if(
        empty_generated_loop_design.units.begin(),
        empty_generated_loop_design.units.end(),
        [](const auto& unit) {
          return unit.name == "generated_sv_loop";
        });
    assert(empty_loop_unit != empty_generated_loop_design.units.end());
    assert(!empty_loop_unit->parameters.empty());
    empty_loop_unit->parameters.front().default_value.text = "0";
    const auto empty_generated_loop =
        fsim::elaboration::elaborate(
            empty_generated_loop_design,
            "sv:work.generated_sv_loop");
    assert(empty_generated_loop.ok());
    assert(empty_generated_loop.design->specializations().size() == 1);

    const std::vector<fsim::elaboration::Binding>
        generated_vhdl_loop_bindings{
            {"generated_vhdl_loop_top.lanes[2].child",
             "sv:work.generated_sv_internal_leaf",
             std::nullopt},
            {"generated_vhdl_loop_top.lanes[1].child",
             "sv:work.generated_sv_internal_leaf",
             std::nullopt},
            {"generated_vhdl_loop_top.lanes[0].child",
             "sv:work.generated_sv_internal_leaf",
             std::nullopt},
        };
    const auto generated_vhdl_loop =
        fsim::elaboration::elaborate(
            generated_design,
            "vhdl:work.generated_vhdl_loop_top(rtl)",
            generated_vhdl_loop_bindings);
    assert(generated_vhdl_loop.ok());
    assert(generated_vhdl_loop.design->specializations().size() == 4);
    const std::array<std::size_t, 3> descending_indices{2, 1, 0};
    for (std::size_t ordinal = 0;
         ordinal < descending_indices.size();
         ++ordinal) {
      const auto index = descending_indices[ordinal];
      const auto path =
          "generated_vhdl_loop_top.lanes["
          + std::to_string(index) + "].child";
      const auto& specialization =
          generated_vhdl_loop.design->specializations()[ordinal + 1];
      assert(specialization.instance == path);
      assert((
          specialization.parameter_values
          == std::vector<std::pair<std::string, std::string>>{
              {"VALUE", std::to_string(index + 4)}}));
    }
    auto generated_vhdl_loop_interpreter =
        generated_vhdl_loop.design->create_interpreter();
    assert(
        generated_vhdl_loop_interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    const std::array<std::string_view, 3> vhdl_loop_values{
        "0100", "0101", "0110"};
    for (const auto index : descending_indices) {
      const auto signal = generated_vhdl_loop.design->find_signal(
          "generated_vhdl_loop_top.lanes["
          + std::to_string(index) + "].child.q");
      assert(signal);
      assert(
          generated_vhdl_loop_interpreter
              ->signal_value(*signal)
              .to_msb_string()
          == vhdl_loop_values[index]);
    }

    const std::vector<fsim::elaboration::Binding>
        generated_sv_case_binding{
            {"generated_sv_case_selected.selected.child",
             "vhdl:work.generated_vhdl_internal_leaf(rtl)",
             std::nullopt},
        };
    const auto generated_sv_case =
        fsim::elaboration::elaborate(
            generated_design,
            "sv:work.generated_sv_case_selected",
            generated_sv_case_binding);
    assert(generated_sv_case.ok());
    assert(generated_sv_case.design->specializations().size() == 2);
    assert(
        generated_sv_case.design->specializations()[1].instance
        == "generated_sv_case_selected.selected.child");
    assert((
        generated_sv_case.design->specializations()[1]
            .parameter_values
        == std::vector<std::pair<std::string, std::string>>{
            {"value", "8"}}));
    const auto generated_sv_case_q =
        generated_sv_case.design->find_signal(
            "generated_sv_case_selected.selected.child.q");
    assert(generated_sv_case_q);
    auto generated_sv_case_interpreter =
        generated_sv_case.design->create_interpreter();
    assert(
        generated_sv_case_interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    assert(
        generated_sv_case_interpreter
            ->signal_value(*generated_sv_case_q)
            .to_msb_string()
        == "1000");

    const auto generated_sv_case_default =
        fsim::elaboration::elaborate(
            generated_design,
            "sv:work.generated_sv_case_default");
    assert(generated_sv_case_default.ok());
    assert(
        generated_sv_case_default.design
            ->specializations()[1]
            .instance
        == "generated_sv_case_default.fallback.child");
    const auto generated_sv_case_default_q =
        generated_sv_case_default.design->find_signal(
            "generated_sv_case_default.fallback.child.q");
    assert(generated_sv_case_default_q);
    auto generated_sv_case_default_interpreter =
        generated_sv_case_default.design->create_interpreter();
    assert(
        generated_sv_case_default_interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    assert(
        generated_sv_case_default_interpreter
            ->signal_value(*generated_sv_case_default_q)
            .to_msb_string()
        == "0100");

    const std::vector<fsim::elaboration::Binding>
        generated_vhdl_case_binding{
            {"generated_vhdl_case_top.selected.child",
             "sv:work.generated_sv_internal_leaf",
             std::nullopt},
        };
    const auto generated_vhdl_case =
        fsim::elaboration::elaborate(
            generated_design,
            "vhdl:work.generated_vhdl_case_top(rtl)",
            generated_vhdl_case_binding);
    assert(generated_vhdl_case.ok());
    assert(generated_vhdl_case.design->specializations().size() == 2);
    assert(
        generated_vhdl_case.design->specializations()[1].instance
        == "generated_vhdl_case_top.selected.child");
    assert((
        generated_vhdl_case.design->specializations()[1]
            .parameter_values
        == std::vector<std::pair<std::string, std::string>>{
            {"VALUE", "7"}}));
    const auto generated_vhdl_case_q =
        generated_vhdl_case.design->find_signal(
            "generated_vhdl_case_top.selected.child.q");
    assert(generated_vhdl_case_q);
    auto generated_vhdl_case_interpreter =
        generated_vhdl_case.design->create_interpreter();
    assert(
        generated_vhdl_case_interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    assert(
        generated_vhdl_case_interpreter
            ->signal_value(*generated_vhdl_case_q)
            .to_msb_string()
        == "0111");

    const auto generated_vhdl_overlapping_ranges =
        fsim::elaboration::elaborate(
            generated_design,
            "vhdl:work.generated_vhdl_overlapping_ranges(rtl)");
    assert(!generated_vhdl_overlapping_ranges.ok());
    assert(has_diagnostic(
        generated_vhdl_overlapping_ranges,
        "FSIM-ELAB-GEN-010"));

    const auto generated_vhdl_bad_range =
        fsim::elaboration::elaborate(
            generated_design,
            "vhdl:work.generated_vhdl_bad_range(rtl)");
    assert(!generated_vhdl_bad_range.ok());
    assert(has_diagnostic(
        generated_vhdl_bad_range,
        "FSIM-ELAB-GEN-009"));

    const auto generated_sv_behavior =
        fsim::elaboration::elaborate(
            generated_design,
            "sv:work.generated_sv_behavior");
    assert(generated_sv_behavior.ok());
    const auto generated_sv_observed =
        generated_sv_behavior.design->find_signal("observed");
    const auto generated_sv_local =
        generated_sv_behavior.design->find_signal(
            "selected.generated_value");
    assert(generated_sv_observed && generated_sv_local);
    assert(generated_sv_behavior.design->processes().size() == 2);
    auto generated_sv_behavior_interpreter =
        generated_sv_behavior.design->create_interpreter();
    assert(
        generated_sv_behavior_interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    assert(
        generated_sv_behavior_interpreter
            ->signal_value(*generated_sv_local)
            .to_msb_string()
        == "0101");
    assert(
        generated_sv_behavior_interpreter
            ->signal_value(*generated_sv_observed)
            .to_msb_string()
        == "0110");

    const auto generated_vhdl_behavior =
        fsim::elaboration::elaborate(
            generated_design,
            "vhdl:work.generated_vhdl_behavior(rtl)");
    assert(generated_vhdl_behavior.ok());
    const auto generated_vhdl_observed =
        generated_vhdl_behavior.design->find_signal("observed");
    const auto generated_vhdl_local =
        generated_vhdl_behavior.design->find_signal(
            "chosen.generated_value");
    assert(generated_vhdl_observed && generated_vhdl_local);
    assert(generated_vhdl_behavior.design->processes().size() == 2);
    auto generated_vhdl_behavior_interpreter =
        generated_vhdl_behavior.design->create_interpreter();
    assert(
        generated_vhdl_behavior_interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    assert(
        generated_vhdl_behavior_interpreter
            ->signal_value(*generated_vhdl_local)
            .to_msb_string()
        == "0110");
    assert(
        generated_vhdl_behavior_interpreter
            ->signal_value(*generated_vhdl_observed)
            .to_msb_string()
        == "0111");

    const auto generated_sv_loop_behavior =
        fsim::elaboration::elaborate(
            generated_design,
            "sv:work.generated_sv_loop_behavior");
    assert(generated_sv_loop_behavior.ok());
    assert(
        generated_sv_loop_behavior.design->processes().size() == 3);
    auto generated_sv_loop_behavior_interpreter =
        generated_sv_loop_behavior.design->create_interpreter();
    assert(
        generated_sv_loop_behavior_interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    const std::array<std::string_view, 3>
        generated_sv_loop_behavior_values{
            "0001", "0010", "0011"};
    for (std::size_t index = 0; index < 3; ++index) {
      const auto signal =
          generated_sv_loop_behavior.design->find_signal(
              "lane[" + std::to_string(index)
              + "].generated_value");
      assert(signal);
      assert(
          generated_sv_loop_behavior_interpreter
              ->signal_value(*signal)
              .to_msb_string()
          == generated_sv_loop_behavior_values[index]);
    }

    const auto generated_vhdl_loop_behavior =
        fsim::elaboration::elaborate(
            generated_design,
            "vhdl:work.generated_vhdl_loop_behavior(rtl)");
    assert(generated_vhdl_loop_behavior.ok());
    assert(
        generated_vhdl_loop_behavior.design->processes().size()
        == 3);
    auto generated_vhdl_loop_behavior_interpreter =
        generated_vhdl_loop_behavior.design->create_interpreter();
    assert(
        generated_vhdl_loop_behavior_interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    const std::array<std::string_view, 3>
        generated_vhdl_loop_behavior_values{
            "0100", "0101", "0110"};
    for (std::size_t index = 0; index < 3; ++index) {
      const auto signal =
          generated_vhdl_loop_behavior.design->find_signal(
              "lanes[" + std::to_string(index)
              + "].generated_value");
      assert(signal);
      assert(
          generated_vhdl_loop_behavior_interpreter
              ->signal_value(*signal)
              .to_msb_string()
          == generated_vhdl_loop_behavior_values[index]);
    }

    const auto generated_sv_implicit_behavior =
        fsim::elaboration::elaborate(
            generated_design,
            "sv:work.generated_sv_implicit_behavior");
    assert(generated_sv_implicit_behavior.ok());
    const auto generated_sv_implicit_local =
        generated_sv_implicit_behavior.design->find_signal(
            "implicit_scope.generated_value");
    const auto generated_sv_implicit_observed =
        generated_sv_implicit_behavior.design->find_signal(
            "observed");
    assert(
        generated_sv_implicit_local
        && generated_sv_implicit_observed);
    assert(
        generated_sv_implicit_behavior.design->processes().size()
        == 2);
    auto generated_sv_implicit_interpreter =
        generated_sv_implicit_behavior.design->create_interpreter();
    assert(
        generated_sv_implicit_interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    assert(
        generated_sv_implicit_interpreter
            ->signal_value(*generated_sv_implicit_local)
            .to_msb_string()
        == "0110");
    assert(
        generated_sv_implicit_interpreter
            ->signal_value(*generated_sv_implicit_observed)
            .to_msb_string()
        == "0111");

    const auto generated_sv_direct_behavior =
        fsim::elaboration::elaborate(
            generated_design,
            "sv:work.generated_sv_direct_behavior");
    assert(generated_sv_direct_behavior.ok());
    const auto generated_sv_direct =
        generated_sv_direct_behavior.design->find_signal(
            "direct_value");
    const auto generated_sv_nested =
        generated_sv_direct_behavior.design->find_signal(
            "named_scope.nested_value");
    const auto generated_sv_direct_observed =
        generated_sv_direct_behavior.design->find_signal(
            "observed");
    assert(
        generated_sv_direct && generated_sv_nested
        && generated_sv_direct_observed);
    assert(
        generated_sv_direct_behavior.design->processes().size()
        == 3);
    auto generated_sv_direct_interpreter =
        generated_sv_direct_behavior.design->create_interpreter();
    assert(
        generated_sv_direct_interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    assert(
        generated_sv_direct_interpreter
            ->signal_value(*generated_sv_direct)
            .to_msb_string()
        == "0010");
    assert(
        generated_sv_direct_interpreter
            ->signal_value(*generated_sv_nested)
            .to_msb_string()
        == "0011");
    assert(
        generated_sv_direct_interpreter
            ->signal_value(*generated_sv_direct_observed)
            .to_msb_string()
        == "0100");

    const auto generated_vhdl_block_behavior =
        fsim::elaboration::elaborate(
            generated_design,
            "vhdl:work.generated_vhdl_block_behavior(rtl)");
    assert(generated_vhdl_block_behavior.ok());
    const auto generated_vhdl_block_local =
        generated_vhdl_block_behavior.design->find_signal(
            "static_scope.generated_value");
    const auto generated_vhdl_block_observed =
        generated_vhdl_block_behavior.design->find_signal(
            "observed");
    assert(
        generated_vhdl_block_local
        && generated_vhdl_block_observed);
    assert(
        generated_vhdl_block_behavior.design->processes().size()
        == 2);
    auto generated_vhdl_block_interpreter =
        generated_vhdl_block_behavior.design->create_interpreter();
    assert(
        generated_vhdl_block_interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    assert(
        generated_vhdl_block_interpreter
            ->signal_value(*generated_vhdl_block_local)
            .to_msb_string()
        == "0111");
    assert(
        generated_vhdl_block_interpreter
            ->signal_value(*generated_vhdl_block_observed)
            .to_msb_string()
        == "1000");

    const auto generated_sv_bad_constant =
        fsim::elaboration::elaborate(
            generated_design,
            "sv:work.generated_sv_bad_constant");
    assert(!generated_sv_bad_constant.ok());
    assert(std::any_of(
        generated_sv_bad_constant.diagnostics.begin(),
        generated_sv_bad_constant.diagnostics.end(),
        [](const auto& diagnostic) {
          return diagnostic.code == "FSIM-ELAB-GEN-011";
        }));

    const auto generated_sv_wide_constant =
        fsim::elaboration::elaborate(
            generated_design,
            "sv:work.generated_sv_wide_constant");
    assert(!generated_sv_wide_constant.ok());
    assert(std::any_of(
        generated_sv_wide_constant.diagnostics.begin(),
        generated_sv_wide_constant.diagnostics.end(),
        [](const auto& diagnostic) {
          return diagnostic.code == "FSIM-ELAB-GEN-012";
        }));

    const auto generated_vhdl_bad_constant =
        fsim::elaboration::elaborate(
            generated_design,
            "vhdl:work.generated_vhdl_bad_constant(rtl)");
    assert(!generated_vhdl_bad_constant.ok());
    assert(std::any_of(
        generated_vhdl_bad_constant.diagnostics.begin(),
        generated_vhdl_bad_constant.diagnostics.end(),
        [](const auto& diagnostic) {
          return diagnostic.code == "FSIM-ELAB-GEN-012";
        }));

    auto unevaluable_generate_design = generated_design;
    const auto unevaluable_unit = std::find_if(
        unevaluable_generate_design.units.begin(),
        unevaluable_generate_design.units.end(),
        [](const auto& unit) {
          return unit.name == "generated_sv_true";
        });
    assert(unevaluable_unit != unevaluable_generate_design.units.end());
    assert(!unevaluable_unit->generate_regions.empty());
    unevaluable_unit->generate_regions.front().condition.text =
        "MISSING_GENERATE_CONSTANT";
    const auto unevaluable_generate =
        fsim::elaboration::elaborate(
            unevaluable_generate_design,
            "sv:work.generated_sv_true");
    assert(!unevaluable_generate.ok());
    assert(has_diagnostic(
        unevaluable_generate, "FSIM-ELAB-GEN-001"));

    const auto generated_loop_unit =
        [](fsim::frontend::ParsedDesign& design)
        -> fsim::frontend::DesignUnit& {
          const auto found = std::find_if(
              design.units.begin(),
              design.units.end(),
              [](const auto& unit) {
                return unit.name == "generated_sv_loop";
              });
          assert(found != design.units.end());
          assert(!found->generate_regions.empty());
          return *found;
        };
    auto invalid_loop_initial_design = generated_design;
    auto& invalid_loop_initial =
        generated_loop_unit(invalid_loop_initial_design)
            .generate_regions.front()
            .initial;
    invalid_loop_initial.kind =
        fsim::frontend::ExpressionKind::Identifier;
    invalid_loop_initial.text = "MISSING_LOOP_INITIAL";
    invalid_loop_initial.operands.clear();
    const auto invalid_loop_initial_result =
        fsim::elaboration::elaborate(
            invalid_loop_initial_design,
            "sv:work.generated_sv_loop");
    assert(!invalid_loop_initial_result.ok());
    assert(has_diagnostic(
        invalid_loop_initial_result, "FSIM-ELAB-GEN-002"));

    auto invalid_loop_condition_design = generated_design;
    auto& invalid_loop_condition =
        generated_loop_unit(invalid_loop_condition_design)
            .generate_regions.front()
            .condition;
    invalid_loop_condition.kind =
        fsim::frontend::ExpressionKind::Identifier;
    invalid_loop_condition.text = "MISSING_LOOP_CONDITION";
    invalid_loop_condition.operands.clear();
    const auto invalid_loop_condition_result =
        fsim::elaboration::elaborate(
            invalid_loop_condition_design,
            "sv:work.generated_sv_loop");
    assert(!invalid_loop_condition_result.ok());
    assert(has_diagnostic(
        invalid_loop_condition_result, "FSIM-ELAB-GEN-003"));

    const std::vector<fsim::elaboration::Binding>
        first_generated_loop_binding{
            {"generated_sv_loop.lanes[0].child",
             "vhdl:work.generated_vhdl_internal_leaf(rtl)",
             std::nullopt},
        };
    auto stalled_loop_design = generated_design;
    auto& stalled_iteration =
        generated_loop_unit(stalled_loop_design)
            .generate_regions.front()
            .iteration;
    stalled_iteration.kind =
        fsim::frontend::ExpressionKind::Identifier;
    stalled_iteration.text = "i";
    stalled_iteration.operands.clear();
    const auto stalled_loop =
        fsim::elaboration::elaborate(
            stalled_loop_design,
            "sv:work.generated_sv_loop",
            first_generated_loop_binding);
    assert(!stalled_loop.ok());
    assert(has_diagnostic(stalled_loop, "FSIM-ELAB-GEN-006"));

    auto invalid_loop_iteration_design = generated_design;
    auto& invalid_loop_iteration =
        generated_loop_unit(invalid_loop_iteration_design)
            .generate_regions.front()
            .iteration;
    invalid_loop_iteration.kind =
        fsim::frontend::ExpressionKind::Identifier;
    invalid_loop_iteration.text = "MISSING_LOOP_ITERATION";
    invalid_loop_iteration.operands.clear();
    const auto invalid_loop_iteration_result =
        fsim::elaboration::elaborate(
            invalid_loop_iteration_design,
            "sv:work.generated_sv_loop",
            first_generated_loop_binding);
    assert(!invalid_loop_iteration_result.ok());
    assert(has_diagnostic(
        invalid_loop_iteration_result, "FSIM-ELAB-GEN-005"));

    const auto shadowed_loop =
        fsim::elaboration::elaborate(
            generated_design,
            "sv:work.generated_shadow_loop");
    assert(!shadowed_loop.ok());
    assert(has_diagnostic(
        shadowed_loop, "FSIM-ELAB-GEN-007"));

    const auto generated_case_unit =
        [](fsim::frontend::ParsedDesign& design)
        -> fsim::frontend::DesignUnit& {
          const auto found = std::find_if(
              design.units.begin(),
              design.units.end(),
              [](const auto& unit) {
                return unit.name
                    == "generated_sv_case_selected";
              });
          assert(found != design.units.end());
          assert(!found->generate_regions.empty());
          return *found;
        };
    auto invalid_case_selector_design = generated_design;
    auto& invalid_case_selector =
        generated_case_unit(invalid_case_selector_design)
            .generate_regions.front()
            .condition;
    invalid_case_selector.kind =
        fsim::frontend::ExpressionKind::Identifier;
    invalid_case_selector.text = "MISSING_CASE_SELECTOR";
    invalid_case_selector.operands.clear();
    const auto invalid_case_selector_result =
        fsim::elaboration::elaborate(
            invalid_case_selector_design,
            "sv:work.generated_sv_case_selected");
    assert(!invalid_case_selector_result.ok());
    assert(has_diagnostic(
        invalid_case_selector_result, "FSIM-ELAB-GEN-008"));

    auto invalid_case_choice_design = generated_design;
    auto& invalid_case_choice =
        generated_case_unit(invalid_case_choice_design)
            .generate_regions.front()
            .alternatives.front()
            .choices.front();
    invalid_case_choice.left.kind =
        fsim::frontend::ExpressionKind::Identifier;
    invalid_case_choice.left.text = "MISSING_CASE_CHOICE";
    invalid_case_choice.left.operands.clear();
    const auto invalid_case_choice_result =
        fsim::elaboration::elaborate(
            invalid_case_choice_design,
            "sv:work.generated_sv_case_selected");
    assert(!invalid_case_choice_result.ok());
    assert(has_diagnostic(
        invalid_case_choice_result, "FSIM-ELAB-GEN-009"));

    auto overlapping_case_design = generated_design;
    auto& overlapping_choice =
        generated_case_unit(overlapping_case_design)
            .generate_regions.front()
            .alternatives.front()
            .choices.front();
    overlapping_choice.left.text = "2";
    const auto overlapping_case =
        fsim::elaboration::elaborate(
            overlapping_case_design,
            "sv:work.generated_sv_case_selected");
    assert(!overlapping_case.ok());
    assert(has_diagnostic(
        overlapping_case, "FSIM-ELAB-GEN-010"));

    auto unmatched_case_design = generated_design;
    const auto unmatched_case_unit = std::find_if(
        unmatched_case_design.units.begin(),
        unmatched_case_design.units.end(),
        [](const auto& unit) {
          return unit.name == "generated_sv_case_default";
        });
    assert(unmatched_case_unit != unmatched_case_design.units.end());
    auto& unmatched_alternatives =
        unmatched_case_unit->generate_regions.front().alternatives;
    unmatched_alternatives.erase(
        std::remove_if(
            unmatched_alternatives.begin(),
            unmatched_alternatives.end(),
            [](const auto& alternative) {
              return alternative.is_default;
            }),
        unmatched_alternatives.end());
    const auto unmatched_case =
        fsim::elaboration::elaborate(
            unmatched_case_design,
            "sv:work.generated_sv_case_default");
    assert(unmatched_case.ok());
    assert(unmatched_case.design->specializations().size() == 1);

    constexpr std::string_view vhdl_source = R"(
entity counter_vhdl is
  port (
    clk : in std_logic;
    q : out std_logic_vector(7 downto 0)
  );
end entity counter_vhdl;

architecture rtl of counter_vhdl is
  signal count : std_logic_vector(7 downto 0);
begin
  q <= count;
  update: process(clk)
  begin
    if rising_edge(clk) then
      count <= count + 1;
    end if;
  end process update;
end architecture rtl;
)";
    const auto parsed_vhdl = fsim::frontend::parse_text(
        "counter.vhd", vhdl_source, fsim::frontend::Language::Vhdl2008);
    assert(parsed_vhdl.ok());
    auto elaborated_vhdl =
        fsim::elaboration::elaborate(parsed_vhdl.design, "counter_vhdl");
    assert(elaborated_vhdl.ok());

    auto vhdl_interpreter = elaborated_vhdl.design->create_interpreter();
    const auto vhdl_clock = elaborated_vhdl.design->find_signal("clk");
    const auto count = elaborated_vhdl.design->find_signal("count");
    const auto vhdl_q = elaborated_vhdl.design->find_signal("q");
    assert(vhdl_clock && count && vhdl_q);
    vhdl_interpreter->deposit_signal(
        *count, fsim::runtime::PackedLogic4::from_msb_string("00000000"));
    vhdl_interpreter->deposit_signal(
        *vhdl_clock, fsim::runtime::PackedLogic4::from_msb_string("0"));
    vhdl_interpreter->start();
    (void)vhdl_interpreter->run();
    assert(
        vhdl_interpreter->signal_value(*vhdl_q).to_msb_string()
        == "00000000");
    vhdl_interpreter->deposit_signal(
        *vhdl_clock, fsim::runtime::PackedLogic4::from_msb_string("1"));
    (void)vhdl_interpreter->run();
    assert(
        vhdl_interpreter->signal_value(*vhdl_q).to_msb_string()
        == "00000001");

    constexpr std::string_view mixed_sv = R"(
module tb;
  logic clk;
  logic reset;
  logic [7:0] q;
  logic [7:0] inverted;

  counter u_counter(.clk(clk), .reset(reset), .q(q));
  child u_child(.value(q), .inverted(inverted));

  initial begin
    clk = 1'b0;
    reset = 1'b1;
    #1 clk = 1'b1;
    #1 clk = 1'b0;
    #1 reset = 1'b0;
    #1 clk = 1'b1;
    #1 $finish;
  end
endmodule

module child(
  input logic [7:0] value,
  output logic [7:0] inverted
);
  assign inverted = ~value;
endmodule
)";
    constexpr std::string_view mixed_vhdl = R"(
entity counter is
  port (
    clk : in std_logic;
    reset : in std_logic;
    q : out unsigned(7 downto 0)
  );
end entity counter;

architecture rtl of counter is
begin
  update: process(clk)
  begin
    if rising_edge(clk) then
      if reset = '1' then
        q <= "00000000";
      else
        q <= q + 1;
      end if;
    end if;
  end process update;
end architecture rtl;
)";
    auto parsed_mixed_sv = fsim::frontend::parse_text(
        "tb.sv", mixed_sv, fsim::frontend::Language::SystemVerilog2017);
    auto parsed_mixed_vhdl = fsim::frontend::parse_text(
        "counter.vhd", mixed_vhdl, fsim::frontend::Language::Vhdl2008);
    assert(parsed_mixed_sv.ok() && parsed_mixed_vhdl.ok());
    for (auto& unit : parsed_mixed_vhdl.design.units) {
        parsed_mixed_sv.design.units.push_back(std::move(unit));
    }
    const std::vector<fsim::elaboration::Binding> mixed_bindings{
        {"tb.u_counter", "vhdl:work.counter(rtl)", std::nullopt},
        {"tb.u_child", "sv:work.child", std::nullopt},
    };
    auto elaborated_mixed = fsim::elaboration::elaborate(
        parsed_mixed_sv.design, "sv:work.tb", mixed_bindings);
    if (!elaborated_mixed.ok()) {
        for (const auto& diagnostic : elaborated_mixed.diagnostics) {
            std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
        }
    }
    assert(elaborated_mixed.ok());
    assert(elaborated_mixed.design->signals().size() == 4);
    assert(elaborated_mixed.design->specializations().size() == 3);
    const auto& mixed_specializations =
        elaborated_mixed.design->specializations();
    assert(mixed_specializations[0].id == 0);
    assert(mixed_specializations[0].unit == "sv:work.tb");
    assert(mixed_specializations[0].instance == "tb");
    assert(mixed_specializations[0].source == "tb.sv");
    assert((
        mixed_specializations[0].processes
        == std::vector<fsim::runtime::simir::ProcessId>{0}));
    assert(mixed_specializations[1].id == 1);
    assert(
        mixed_specializations[1].unit
        == "vhdl:work.counter(rtl)");
    assert(mixed_specializations[1].instance == "tb.u_counter");
    assert(mixed_specializations[1].source == "counter.vhd");
    assert((
        mixed_specializations[1].processes
        == std::vector<fsim::runtime::simir::ProcessId>{1}));
    assert(mixed_specializations[2].id == 2);
    assert(mixed_specializations[2].unit == "sv:work.child");
    assert(mixed_specializations[2].instance == "tb.u_child");
    assert(mixed_specializations[2].source == "tb.sv");
    assert((
        mixed_specializations[2].processes
        == std::vector<fsim::runtime::simir::ProcessId>{2}));
    const auto mixed_q = elaborated_mixed.design->find_signal("q");
    const auto mixed_child_q =
        elaborated_mixed.design->find_signal("tb.u_counter.q");
    const auto mixed_inverted =
        elaborated_mixed.design->find_signal("inverted");
    assert(mixed_q && mixed_child_q && mixed_inverted);
    assert(*mixed_q == *mixed_child_q);
    auto mixed_interpreter =
        elaborated_mixed.design->create_interpreter();
    const auto mixed_result = mixed_interpreter->run();
    assert(mixed_result.status == fsim::runtime::RunStatus::stopped);
    assert(mixed_result.time == 5);
    assert(
        mixed_interpreter->signal_value(*mixed_q).to_msb_string()
        == "00000001");
    assert(
        mixed_interpreter->signal_value(*mixed_inverted).to_msb_string()
        == "11111110");

    const auto integer_boundary_vhdl =
        fsim::frontend::parse_text(
            "integer_boundary.vhd",
            R"(
entity integer_boundary is
end entity;
architecture rtl of integer_boundary is
  signal source : integer;
  signal result : integer;
begin
  source <= -2;
  child: sv_integer_child
    port map (value => source, result => result);
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    const auto integer_boundary_sv =
        fsim::frontend::parse_text(
            "integer_child.sv",
            R"(
module sv_integer_child(
  input bit signed [31:0] value,
  output bit signed [31:0] result
);
  assign result = value + 1;
endmodule

module sv_logic_integer_child(
  input bit signed [31:0] value,
  output logic signed [31:0] result
);
  assign result = value + 1;
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(integer_boundary_vhdl.ok());
    assert(integer_boundary_sv.ok());
    auto integer_boundary_design =
        integer_boundary_vhdl.design;
    integer_boundary_design.units.insert(
        integer_boundary_design.units.end(),
        integer_boundary_sv.design.units.begin(),
        integer_boundary_sv.design.units.end());
    const std::vector<fsim::elaboration::Binding>
        integer_boundary_binding{
            {
                "integer_boundary.child",
                "sv:work.sv_integer_child",
                std::nullopt},
        };
    const auto elaborated_integer_boundary =
        fsim::elaboration::elaborate(
            integer_boundary_design,
            "vhdl:work.integer_boundary(rtl)",
            integer_boundary_binding);
    if (!elaborated_integer_boundary.ok()) {
      for (const auto& diagnostic :
           elaborated_integer_boundary.diagnostics) {
        std::cerr << diagnostic.code << ": "
                  << diagnostic.message << '\n';
      }
    }
    assert(elaborated_integer_boundary.ok());
    const auto integer_source =
        elaborated_integer_boundary.design->find_signal(
            "source");
    const auto integer_result =
        elaborated_integer_boundary.design->find_signal(
            "result");
    const auto integer_child_source =
        elaborated_integer_boundary.design->find_signal(
            "integer_boundary.child.value");
    assert(
        integer_source && integer_result
        && integer_child_source);
    assert(*integer_source == *integer_child_source);
    auto integer_boundary_interpreter =
        elaborated_integer_boundary.design
            ->create_interpreter();
    const auto integer_boundary_result =
        integer_boundary_interpreter->run();
    assert(
        integer_boundary_result.status
        == fsim::runtime::RunStatus::completed);
    assert(
        integer_boundary_interpreter
            ->signal_value(*integer_result)
            .to_msb_string()
        == "11111111111111111111111111111111");
    const std::vector<fsim::elaboration::Binding>
        lossy_integer_boundary_binding{
            {
                "integer_boundary.child",
                "sv:work.sv_logic_integer_child",
                std::nullopt},
        };
    const auto rejected_lossy_integer_boundary =
        fsim::elaboration::elaborate(
            integer_boundary_design,
            "vhdl:work.integer_boundary(rtl)",
            lossy_integer_boundary_binding);
    assert(!rejected_lossy_integer_boundary.ok());
    assert(has_diagnostic(
        rejected_lossy_integer_boundary,
        "FSIM-ELAB-BIND-022"));

    const auto vhdl_integer_output =
        fsim::frontend::parse_text(
            "vhdl_integer_output.vhd",
            R"(
entity vhdl_integer_output is
  port (result : out integer range 1 to 4);
end entity;
architecture rtl of vhdl_integer_output is
begin
  result <= 2;
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    const auto sv_integer_parent =
        fsim::frontend::parse_text(
            "sv_integer_parent.sv",
            R"(
module sv_integer_parent;
  bit signed [31:0] result;
  vhdl_integer_output child(.result(result));
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(vhdl_integer_output.ok());
    assert(sv_integer_parent.ok());
    auto reverse_integer_design = sv_integer_parent.design;
    reverse_integer_design.units.insert(
        reverse_integer_design.units.end(),
        vhdl_integer_output.design.units.begin(),
        vhdl_integer_output.design.units.end());
    const std::vector<fsim::elaboration::Binding>
        reverse_integer_binding{
            {
                "sv_integer_parent.child",
                "vhdl:work.vhdl_integer_output(rtl)",
                std::nullopt},
        };
    const auto elaborated_reverse_integer =
        fsim::elaboration::elaborate(
            reverse_integer_design,
            "sv:work.sv_integer_parent",
            reverse_integer_binding);
    assert(elaborated_reverse_integer.ok());
    assert(
        std::ranges::any_of(
            elaborated_reverse_integer.design
                ->processes().front().operations,
            [](const auto& operation) {
              const auto* check =
                  std::get_if<
                      fsim::runtime::simir::IntegerCheck>(
                      &operation);
              return check != nullptr
                  && check->lower == 1
                  && check->upper == 4;
            }));
    const auto reverse_integer_result_signal =
        elaborated_reverse_integer.design->find_signal("result");
    assert(reverse_integer_result_signal);
    auto reverse_integer_interpreter =
        elaborated_reverse_integer.design->create_interpreter();
    assert(
        reverse_integer_interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    assert(
        reverse_integer_interpreter
            ->signal_value(*reverse_integer_result_signal)
            .to_msb_string()
        == "00000000000000000000000000000010");

    const auto unsafe_integer_alias =
        fsim::frontend::parse_text(
            "unsafe_integer_alias.vhd",
            R"(
entity positive_child is
  port (value : in positive);
end entity;
architecture rtl of positive_child is
begin
end architecture;

entity unsafe_integer_alias is
end entity;
architecture rtl of unsafe_integer_alias is
  signal source : integer;
begin
  child: positive_child port map (value => source);
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(unsafe_integer_alias.ok());
    const auto rejected_integer_alias =
        fsim::elaboration::elaborate(
            unsafe_integer_alias.design,
            "vhdl:work.unsafe_integer_alias(rtl)");
    assert(!rejected_integer_alias.ok());
    assert(has_diagnostic(
        rejected_integer_alias,
        "FSIM-ELAB-BIND-051"));

    const auto struct_boundary_sv =
        fsim::frontend::parse_text(
            "struct_boundary.sv",
            R"(
module struct_boundary;
  typedef struct packed {
    logic [3:0] payload;
    bit valid;
  } packet_t;
  packet_t packet;
  struct_sink u_sink(.packet(packet));
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    const auto struct_boundary_vhdl =
        fsim::frontend::parse_text(
            "struct_sink.vhd",
            R"(
entity struct_sink is
  port (packet : in std_logic_vector(4 downto 0));
end entity;
architecture rtl of struct_sink is
begin
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(struct_boundary_sv.ok());
    assert(struct_boundary_vhdl.ok());
    auto struct_boundary_design = struct_boundary_sv.design;
    struct_boundary_design.units.insert(
        struct_boundary_design.units.end(),
        struct_boundary_vhdl.design.units.begin(),
        struct_boundary_vhdl.design.units.end());
    const std::vector<fsim::elaboration::Binding>
        struct_boundary_binding{
            {"struct_boundary.u_sink",
             "vhdl:work.struct_sink(rtl)",
             std::nullopt},
        };
    const auto rejected_struct_boundary =
        fsim::elaboration::elaborate(
            struct_boundary_design,
            "sv:work.struct_boundary",
            struct_boundary_binding);
    assert(!rejected_struct_boundary.ok());
    assert(has_diagnostic(
        rejected_struct_boundary, "FSIM-ELAB-BIND-049"));

    constexpr std::string_view vhdl_parent = R"(
entity vhdl_top is
end entity vhdl_top;

architecture rtl of vhdl_top is
  signal source : std_logic_vector(3 downto 0);
  signal inverted : std_logic_vector(3 downto 0);
begin
  source <= "1010";
  u_child: sv_child
    port map (value => source, inverted => inverted);
end architecture rtl;
)";
    constexpr std::string_view sv_bound_child = R"(
module sv_child(
  input logic [3:0] value,
  output logic [3:0] inverted
);
  assign inverted = ~value;
endmodule
)";
    auto parsed_vhdl_parent = fsim::frontend::parse_text(
        "vhdl_top.vhd",
        vhdl_parent,
        fsim::frontend::Language::Vhdl2008);
    auto parsed_sv_child = fsim::frontend::parse_text(
        "sv_child.sv",
        sv_bound_child,
        fsim::frontend::Language::SystemVerilog2017);
    assert(parsed_vhdl_parent.ok() && parsed_sv_child.ok());
    for (auto& unit : parsed_sv_child.design.units) {
        parsed_vhdl_parent.design.units.push_back(std::move(unit));
    }
    const std::vector<fsim::elaboration::Binding> reverse_binding{
        {"vhdl_top.u_child", "sv:work.sv_child", std::nullopt},
    };
    auto reverse_mixed = fsim::elaboration::elaborate(
        parsed_vhdl_parent.design,
        "vhdl:work.vhdl_top(rtl)",
        reverse_binding);
    if (!reverse_mixed.ok()) {
        for (const auto& diagnostic : reverse_mixed.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(reverse_mixed.ok());
    const auto reverse_source =
        reverse_mixed.design->find_signal("source");
    const auto reverse_output =
        reverse_mixed.design->find_signal("inverted");
    const auto child_source =
        reverse_mixed.design->find_signal("vhdl_top.u_child.value");
    assert(reverse_source && reverse_output && child_source);
    assert(*reverse_source == *child_source);
    auto reverse_interpreter =
        reverse_mixed.design->create_interpreter();
    const auto reverse_result = reverse_interpreter->run();
    assert(reverse_result.status == fsim::runtime::RunStatus::completed);
    assert(
        reverse_interpreter->signal_value(*reverse_source).to_msb_string()
        == "1010");
    assert(
        reverse_interpreter->signal_value(*reverse_output).to_msb_string()
        == "0101");

    constexpr std::string_view systemc_boundary_sv = R"(
module systemc_parent;
  bit clock;
  logic [7:0] value;
  bridge_placeholder u_bridge(.clock(clock), .value(value));
  initial begin
    clock = 1'b1;
    #1 $finish;
  end
endmodule

module systemc_hdl_child #(
  parameter VALUE = 8'hA5
) (
  input bit clock,
  output bit [7:0] value
);
  assign value = VALUE;
endmodule
)";
    const auto parsed_systemc_boundary = fsim::frontend::parse_text(
        "systemc_boundary.sv",
        systemc_boundary_sv,
        fsim::frontend::Language::SystemVerilog2017);
    assert(parsed_systemc_boundary.ok());
    fsim::frontend::Type systemc_bit{
        fsim::frontend::ValueDomain::Bit2,
        "systemc.bit",
        std::nullopt,
        false};
    fsim::frontend::Type systemc_unsigned{
        fsim::frontend::ValueDomain::Bit2,
        "systemc.unsigned",
        fsim::frontend::PackedRange{7, 0, true},
        false};
    const auto parsed_systemc_parameters =
        fsim::frontend::parse_text(
            "systemc_parameters.sv",
            R"(
module systemc_parameter_host;
  bit [3:0] value;
  bridge_placeholder #(.WIDTH(4)) u_bridge(.value(value));
endmodule

module systemc_invalid_parameter_host;
  bit value;
  bridge_placeholder #(.WIDTH(0)) u_bridge(.value(value));
endmodule

module systemc_unknown_parameter_host;
  bit value;
  bridge_placeholder #(.MISSING(1)) u_bridge(.value(value));
endmodule

module systemc_missing_parameter_host;
  bit value;
  bridge_placeholder u_bridge(.value(value));
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(parsed_systemc_parameters.ok());
    TestSystemCFactoryProvider parameter_provider;
    parameter_provider.parameters = {
        {"WIDTH", FSIM_SC_CONSTRUCTION_POSITIVE, std::nullopt},
    };
    parameter_provider.prototype.target =
        "systemc:models.parameter_bridge";
    parameter_provider.prototype.ports = {
        {10'001,
         "value",
         systemc_unsigned,
         fsim::frontend::PortDirection::Output,
         0},
    };
    const std::vector<fsim::elaboration::Binding>
        systemc_parameter_binding{
            {"systemc_parameter_host.u_bridge",
             "systemc:models.parameter_bridge",
             std::nullopt},
        };
    const auto systemc_parameterized =
        fsim::elaboration::elaborate(
            parsed_systemc_parameters.design,
            "sv:work.systemc_parameter_host",
            systemc_parameter_binding,
            std::span<const
                fsim::elaboration::SystemCInstanceDescription>{},
            &parameter_provider);
    assert(systemc_parameterized.ok());
    assert((
        parameter_provider.last_values
        == std::vector<std::pair<std::string, std::int64_t>>{
            {"WIDTH", 4}}));
    assert((
        systemc_parameterized.design->systemc_instances().front()
            .construction_values
        == parameter_provider.last_values));
    const auto parameterized_value =
        systemc_parameterized.design->find_signal("value");
    assert(parameterized_value);
    assert(
        systemc_parameterized.design->signals()
            .at(*parameterized_value)
            .width
        == 4);

    const auto reject_systemc_parameter =
        [&](const std::string_view top,
            const std::string_view instance,
            const std::string_view diagnostic) {
          TestSystemCFactoryProvider provider = parameter_provider;
          const std::vector<fsim::elaboration::Binding> binding{
              {std::string{instance},
               "systemc:models.parameter_bridge",
               std::nullopt},
          };
          const auto result = fsim::elaboration::elaborate(
              parsed_systemc_parameters.design,
              top,
              binding,
              std::span<const
                  fsim::elaboration::SystemCInstanceDescription>{},
              &provider);
          assert(!result.ok());
          assert(has_diagnostic(result, diagnostic));
        };
    reject_systemc_parameter(
        "sv:work.systemc_invalid_parameter_host",
        "systemc_invalid_parameter_host.u_bridge",
        "FSIM-ELAB-SC-PARAM-005");
    reject_systemc_parameter(
        "sv:work.systemc_unknown_parameter_host",
        "systemc_unknown_parameter_host.u_bridge",
        "FSIM-ELAB-SC-PARAM-001");
    reject_systemc_parameter(
        "sv:work.systemc_missing_parameter_host",
        "systemc_missing_parameter_host.u_bridge",
        "FSIM-ELAB-SC-PARAM-001");
    auto unavailable_schema_provider = parameter_provider;
    unavailable_schema_provider.schema_failure =
        "intentional schema failure";
    const auto unavailable_schema =
        fsim::elaboration::elaborate(
            parsed_systemc_parameters.design,
            "sv:work.systemc_parameter_host",
            systemc_parameter_binding,
            std::span<const
                fsim::elaboration::SystemCInstanceDescription>{},
            &unavailable_schema_provider);
    assert(!unavailable_schema.ok());
    assert(has_diagnostic(
        unavailable_schema, "FSIM-ELAB-SC-PARAM-007"));
    auto failed_construction_provider = parameter_provider;
    failed_construction_provider.construction_failure =
        "intentional construction failure";
    const auto failed_construction =
        fsim::elaboration::elaborate(
            parsed_systemc_parameters.design,
            "sv:work.systemc_parameter_host",
            systemc_parameter_binding,
            std::span<const
                fsim::elaboration::SystemCInstanceDescription>{},
            &failed_construction_provider);
    assert(!failed_construction.ok());
    assert(has_diagnostic(
        failed_construction, "FSIM-ELAB-SC-PARAM-008"));

    const fsim::elaboration::SystemCInstanceDescription
        nested_systemc{
            "systemc_parent.u_bridge",
            "systemc:models.bridge",
            100,
            0,
            {},
            {
                {101, "clock", systemc_bit,
                 fsim::frontend::PortDirection::Input, 0},
                {102, "value", systemc_unsigned,
                 fsim::frontend::PortDirection::Output, 0},
            },
            {
                {"u_hdl",
                 {{"VALUE", 0x3c}},
                 {
                     {"clock", systemc_bit,
                      fsim::frontend::PortDirection::Input, 101},
                     {"value", systemc_unsigned,
                      fsim::frontend::PortDirection::Output, 102},
                 }},
            },
            {},
            {},
            {},
            {},
            {},
            {}};
    const std::vector<fsim::elaboration::Binding>
        hdl_to_systemc_bindings{
            {"systemc_parent.u_bridge",
             "systemc:models.bridge",
             std::nullopt},
            {"systemc_parent.u_bridge.u_hdl",
             "sv:work.systemc_hdl_child",
             std::nullopt},
        };
    const std::array hdl_to_systemc_instances{nested_systemc};
    auto hdl_to_systemc = fsim::elaboration::elaborate(
        parsed_systemc_boundary.design,
        "sv:work.systemc_parent",
        hdl_to_systemc_bindings,
        hdl_to_systemc_instances);
    if (!hdl_to_systemc.ok()) {
        for (const auto& diagnostic : hdl_to_systemc.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(hdl_to_systemc.ok());
    assert(hdl_to_systemc.design->systemc_instances().size() == 1);
    assert(
        hdl_to_systemc.design->systemc_instances().front().instance
        == "systemc_parent.u_bridge");
    const auto systemc_parent_value =
        hdl_to_systemc.design->find_signal("value");
    const auto systemc_port_value =
        hdl_to_systemc.design->find_signal(
            "systemc_parent.u_bridge.value");
    const auto systemc_child_value =
        hdl_to_systemc.design->find_signal(
            "systemc_parent.u_bridge.u_hdl.value");
    assert(
        systemc_parent_value && systemc_port_value
        && systemc_child_value);
    assert(*systemc_parent_value == *systemc_port_value);
    assert(*systemc_parent_value == *systemc_child_value);
    auto hdl_to_systemc_interpreter =
        hdl_to_systemc.design->create_interpreter();
    const auto hdl_to_systemc_result =
        hdl_to_systemc_interpreter->run();
    assert(
        hdl_to_systemc_result.status
        == fsim::runtime::RunStatus::stopped);
    assert(
        hdl_to_systemc_interpreter
            ->signal_value(*systemc_parent_value)
            .to_msb_string()
        == "00111100");

    auto systemc_root = nested_systemc;
    systemc_root.path = "bridge";
    const std::vector<fsim::elaboration::Binding>
        systemc_to_hdl_bindings{
            {"bridge.u_hdl",
             "sv:work.systemc_hdl_child",
             std::nullopt},
        };
    const std::array systemc_root_instances{systemc_root};
    auto systemc_to_hdl = fsim::elaboration::elaborate(
        parsed_systemc_boundary.design,
        "systemc:models.bridge",
        systemc_to_hdl_bindings,
        systemc_root_instances);
    if (!systemc_to_hdl.ok()) {
        for (const auto& diagnostic : systemc_to_hdl.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(systemc_to_hdl.ok());
    assert(systemc_to_hdl.design->systemc_instances().size() == 1);
    assert(systemc_to_hdl.design->specializations().size() == 1);
    assert((
        systemc_to_hdl.design->specializations().front()
            .parameter_values
        == std::vector<std::pair<std::string, std::string>>{
            {"VALUE", "60"}}));
    const auto systemc_root_value =
        systemc_to_hdl.design->find_signal("bridge.value");
    const auto systemc_root_child_value =
        systemc_to_hdl.design->find_signal(
            "bridge.u_hdl.value");
    assert(systemc_root_value && systemc_root_child_value);
    assert(*systemc_root_value == *systemc_root_child_value);
    auto systemc_to_hdl_interpreter =
        systemc_to_hdl.design->create_interpreter();
    const auto systemc_to_hdl_result =
        systemc_to_hdl_interpreter->run();
    assert(
        systemc_to_hdl_result.status
        == fsim::runtime::RunStatus::completed);
    assert(
        systemc_to_hdl_interpreter
            ->signal_value(*systemc_root_value)
            .to_msb_string()
        == "00111100");

    auto invalid_systemc_actual = systemc_root;
    invalid_systemc_actual.foreign_children.front()
        .construction_actuals = {{"MISSING", 1}};
    const std::array invalid_systemc_actual_instances{
        invalid_systemc_actual};
    const auto rejected_systemc_actual =
        fsim::elaboration::elaborate(
            parsed_systemc_boundary.design,
            "systemc:models.bridge",
            systemc_to_hdl_bindings,
            invalid_systemc_actual_instances);
    assert(!rejected_systemc_actual.ok());
    assert(has_diagnostic(
        rejected_systemc_actual, "FSIM-ELAB-PARAM-001"));

    auto duplicate_systemc_actual = systemc_root;
    duplicate_systemc_actual.foreign_children.front()
        .construction_actuals = {
            {"VALUE", 1},
            {"VALUE", 2},
        };
    const std::array duplicate_systemc_actual_instances{
        duplicate_systemc_actual};
    const auto rejected_duplicate_systemc_actual =
        fsim::elaboration::elaborate(
            parsed_systemc_boundary.design,
            "systemc:models.bridge",
            systemc_to_hdl_bindings,
            duplicate_systemc_actual_instances);
    assert(!rejected_duplicate_systemc_actual.ok());
    assert(has_diagnostic(
        rejected_duplicate_systemc_actual,
        "FSIM-ELAB-PARAM-002"));

    const std::vector<fsim::elaboration::Binding>
        missing_systemc_child_binding;
    const auto missing_systemc_child =
        fsim::elaboration::elaborate(
            parsed_systemc_boundary.design,
            "systemc:models.bridge",
            missing_systemc_child_binding,
            systemc_root_instances);
    assert(!missing_systemc_child.ok());
    assert(has_diagnostic(
        missing_systemc_child, "FSIM-ELAB-BIND-040"));

    auto thread_systemc = systemc_root;
    thread_systemc.foreign_children.clear();
    thread_systemc.processes.push_back({
        150,
        "thread",
        FSIM_SC_THREAD,
        nullptr,
        nullptr,
        {},
        true});
    const std::array thread_systemc_instances{thread_systemc};
    const auto systemc_thread =
        fsim::elaboration::elaborate(
            parsed_systemc_boundary.design,
            "systemc:models.bridge",
            std::span<const fsim::elaboration::Binding>{},
            thread_systemc_instances);
#if defined(FSIM_HAS_BOOST_CONTEXT)
    assert(systemc_thread.ok());
    assert(systemc_thread.design->systemc_processes().size() == 1);
#else
    assert(!systemc_thread.ok());
    assert(has_diagnostic(
        systemc_thread, "FSIM-ELAB-BIND-042"));
#endif

    constexpr std::string_view systemc_boundary_vhdl = R"(
entity systemc_vhdl_parent is
end entity systemc_vhdl_parent;

architecture rtl of systemc_vhdl_parent is
  signal value : std_logic;
  signal inverted : std_logic;
begin
  value <= '1';
  u_bridge: bridge_placeholder
    port map (value => value, inverted => inverted);
end architecture rtl;

entity systemc_vhdl_child is
  generic (
    choose : integer := 1
  );
  port (
    value : in std_logic;
    inverted : out std_logic
  );
end entity systemc_vhdl_child;

architecture rtl of systemc_vhdl_child is
begin
  inverted <= not value;
end architecture rtl;
)";
    const auto parsed_systemc_vhdl = fsim::frontend::parse_text(
        "systemc_boundary.vhd",
        systemc_boundary_vhdl,
        fsim::frontend::Language::Vhdl2008);
    assert(parsed_systemc_vhdl.ok());
    fsim::frontend::Type systemc_logic{
        fsim::frontend::ValueDomain::Logic4,
        "systemc.logic",
        std::nullopt,
        false};
    const fsim::elaboration::SystemCInstanceDescription
        vhdl_nested_systemc{
            "systemc_vhdl_parent.u_bridge",
            "systemc:models.bridge",
            200,
            0,
            {},
            {
                {201, "value", systemc_logic,
                 fsim::frontend::PortDirection::Input, 0},
                {202, "inverted", systemc_logic,
                 fsim::frontend::PortDirection::Output, 0},
            },
            {
                {"u_hdl",
                 {{"CHOOSE", 0}},
                 {
                     {"value", systemc_logic,
                      fsim::frontend::PortDirection::Input, 201},
                     {"inverted", systemc_logic,
                      fsim::frontend::PortDirection::Output, 202},
                 }},
            },
            {},
            {},
            {},
            {},
            {},
            {}};
    const std::vector<fsim::elaboration::Binding>
        vhdl_systemc_bindings{
            {"systemc_vhdl_parent.u_bridge",
             "systemc:models.bridge",
             std::nullopt},
            {"systemc_vhdl_parent.u_bridge.u_hdl",
             "vhdl:work.systemc_vhdl_child(rtl)",
             std::nullopt},
        };
    const std::array vhdl_systemc_instances{
        vhdl_nested_systemc};
    auto vhdl_systemc = fsim::elaboration::elaborate(
        parsed_systemc_vhdl.design,
        "vhdl:work.systemc_vhdl_parent(rtl)",
        vhdl_systemc_bindings,
        vhdl_systemc_instances);
    if (!vhdl_systemc.ok()) {
        for (const auto& diagnostic : vhdl_systemc.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(vhdl_systemc.ok());
    assert(vhdl_systemc.design->systemc_instances().size() == 1);
    const auto vhdl_systemc_child_specialization =
        std::find_if(
            vhdl_systemc.design->specializations().begin(),
            vhdl_systemc.design->specializations().end(),
            [](const auto& specialization) {
                return specialization.instance
                    == "systemc_vhdl_parent.u_bridge.u_hdl";
            });
    assert(
        vhdl_systemc_child_specialization
        != vhdl_systemc.design->specializations().end());
    assert((
        vhdl_systemc_child_specialization->parameter_values
        == std::vector<std::pair<std::string, std::string>>{
            {"choose", "0"}}));
    const auto vhdl_systemc_value =
        vhdl_systemc.design->find_signal("value");
    const auto vhdl_systemc_child_value =
        vhdl_systemc.design->find_signal(
            "systemc_vhdl_parent.u_bridge.u_hdl.value");
    const auto vhdl_systemc_inverted =
        vhdl_systemc.design->find_signal("inverted");
    assert(
        vhdl_systemc_value && vhdl_systemc_child_value
        && vhdl_systemc_inverted);
    assert(*vhdl_systemc_value == *vhdl_systemc_child_value);
    auto vhdl_systemc_interpreter =
        vhdl_systemc.design->create_interpreter();
    const auto vhdl_systemc_result =
        vhdl_systemc_interpreter->run();
    assert(
        vhdl_systemc_result.status
        == fsim::runtime::RunStatus::completed);
    assert(
        vhdl_systemc_interpreter
            ->signal_value(*vhdl_systemc_inverted)
            .to_msb_string()
        == "0");

    auto colliding_sv = fsim::frontend::parse_text(
        "duplicate.sv",
        "module duplicate; logic sv_only; endmodule",
        fsim::frontend::Language::SystemVerilog2017);
    auto colliding_vhdl = fsim::frontend::parse_text(
        "duplicate.vhd",
        R"(
entity duplicate is
  port (vhdl_only : in std_logic);
end entity duplicate;
architecture rtl of duplicate is
begin
end architecture rtl;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(colliding_sv.ok() && colliding_vhdl.ok());
    colliding_sv.design.units.front().library = "other";
    for (auto& unit : colliding_vhdl.design.units) {
        unit.library = "work";
        colliding_sv.design.units.push_back(std::move(unit));
    }
    auto selected_vhdl = fsim::elaboration::elaborate(
        colliding_sv.design, "vhdl:work.duplicate(rtl)");
    assert(selected_vhdl.ok());
    assert(selected_vhdl.design->find_signal("vhdl_only"));
    assert(!selected_vhdl.design->find_signal("sv_only"));
    const auto ambiguous_vhdl_top = fsim::elaboration::elaborate(
        colliding_sv.design, "vhdl:work.duplicate");
    assert(!ambiguous_vhdl_top.ok());
    assert(has_diagnostic(ambiguous_vhdl_top, "FSIM-ELAB-004"));

    auto cross_library = fsim::frontend::parse_text(
        "cross_library.vhd",
        R"(
entity child is
  port (value : in std_logic);
end entity;
architecture rtl of child is
begin
end architecture;

entity parent is
  port (value : in std_logic);
end entity;
architecture rtl of parent is
begin
  selected: entity other.child(rtl)
    port map (value);
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(cross_library.ok());
    for (auto& unit : cross_library.design.units) {
        unit.library =
            unit.name == "child"
                || (unit.kind
                        == fsim::frontend::UnitKind::VhdlArchitecture
                    && unit.primary_name == "child")
            ? "other"
            : "work";
    }
    const auto selected_library = fsim::elaboration::elaborate(
        cross_library.design, "vhdl:work.parent(rtl)");
    assert(selected_library.ok());
    assert(selected_library.design->find_signal(
        "parent.selected.value"));

    const auto unsafe_edge = fsim::frontend::parse_text(
        "unsafe_edge.vhd",
        R"(
entity unsafe_edge is
  port (clk : in std_logic; q : out std_logic);
end entity;
architecture rtl of unsafe_edge is
begin
  p: process(clk)
  begin
    if rising_edge(clk) then
      q <= '1';
    else
      q <= '0';
    end if;
  end process;
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(unsafe_edge.ok());
    const auto rejected_edge =
        fsim::elaboration::elaborate(unsafe_edge.design, "unsafe_edge");
    assert(!rejected_edge.ok());
    assert(has_diagnostic(rejected_edge, "FSIM-ELAB-045"));

    const auto logical_not = fsim::frontend::parse_text(
        "logical_not.sv",
        R"(
module logical_not(input logic [3:0] value, output logic result);
  assign result = !value;
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(logical_not.ok());
    const auto elaborated_not =
        fsim::elaboration::elaborate(logical_not.design, "logical_not");
    assert(elaborated_not.ok());
    const auto not_value =
        elaborated_not.design->find_signal("value");
    const auto not_result =
        elaborated_not.design->find_signal("result");
    assert(not_value && not_result);
    auto not_interpreter =
        elaborated_not.design->create_interpreter();
    for (const auto& [value, expected] :
         std::array{
             std::pair{
                 std::string_view{"0000"},
                 std::string_view{"1"}},
             std::pair{
                 std::string_view{"00X0"},
                 std::string_view{"X"}},
             std::pair{
                 std::string_view{"01X0"},
                 std::string_view{"0"}}}) {
        not_interpreter->deposit_signal(
            *not_value,
            fsim::runtime::PackedLogic4::from_msb_string(value));
        (void)not_interpreter->run();
        assert(
            not_interpreter
                ->signal_value(*not_result)
                .to_msb_string()
            == expected);
    }

    const auto assignment_timing = fsim::frontend::parse_text(
        "assignment_timing.sv",
        R"(
module assignment_timing;
  logic clock;
  logic source;
  logic delayed_blocking;
  logic [3:0] delayed_slice;
  logic event_blocking;
  logic event_nba;
  logic wildcard_nba;
  logic local_result;

  initial delayed_blocking = #5 source;
  initial delayed_slice[2:1] <= #2 2'b10;
  initial event_blocking = @(posedge clock) source;
  initial event_nba <= @(negedge clock or source) source;
  initial wildcard_nba <= @* source;
  initial begin
    logic local_value;
    local_value = #4 source;
    local_result = local_value;
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(assignment_timing.ok());
    const auto elaborated_assignment_timing =
        fsim::elaboration::elaborate(
            assignment_timing.design, "assignment_timing");
    if (!elaborated_assignment_timing.ok()) {
        for (const auto& diagnostic :
             elaborated_assignment_timing.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(elaborated_assignment_timing.ok());
    assert(
        elaborated_assignment_timing.design->processes().size()
        == 6);
    const auto operation_index =
        []<typename OperationType>(const auto& operations) {
            const auto found = std::find_if(
                operations.begin(),
                operations.end(),
                [](const auto& operation) {
                    return std::holds_alternative<OperationType>(
                        operation);
                });
            assert(found != operations.end());
            return static_cast<std::size_t>(
                std::distance(operations.begin(), found));
        };
    const auto wait_debug_index =
        [](const auto& operations) {
            const auto found = std::find_if(
                operations.begin(),
                operations.end(),
                [](const auto& operation) {
                    const auto* point = std::get_if<
                        fsim::runtime::simir::DebugPoint>(
                        &operation);
                    return point != nullptr
                        && point->kind
                            == fsim::runtime::simir::
                                DebugPointKind::wait;
                });
            assert(found != operations.end());
            return static_cast<std::size_t>(
                std::distance(operations.begin(), found));
        };
    const auto& delayed_blocking_operations =
        elaborated_assignment_timing.design
            ->processes()[0]
            .operations;
    assert(
        operation_index
            .operator()<fsim::runtime::simir::ReadSignal>(
                delayed_blocking_operations)
        < wait_debug_index(delayed_blocking_operations));
    assert(
        wait_debug_index(delayed_blocking_operations)
        < operation_index
              .operator()<fsim::runtime::simir::WaitFor>(
                  delayed_blocking_operations));
    assert(
        operation_index
            .operator()<fsim::runtime::simir::WaitFor>(
                delayed_blocking_operations)
        < operation_index
              .operator()<fsim::runtime::simir::WriteBlocking>(
                  delayed_blocking_operations));
    const auto* blocking_wait =
        std::get_if<fsim::runtime::simir::WaitFor>(
            &delayed_blocking_operations[
                operation_index
                    .operator()<fsim::runtime::simir::WaitFor>(
                        delayed_blocking_operations)]);
    assert(blocking_wait && blocking_wait->delay == 5);

    const auto& delayed_slice_operations =
        elaborated_assignment_timing.design
            ->processes()[1]
            .operations;
    const auto delayed_slice = std::find_if(
        delayed_slice_operations.begin(),
        delayed_slice_operations.end(),
        [](const auto& operation) {
            return std::holds_alternative<
                fsim::runtime::simir::WriteAfterSlice>(
                    operation);
        });
    assert(delayed_slice != delayed_slice_operations.end());
    const auto& delayed_slice_write =
        std::get<fsim::runtime::simir::WriteAfterSlice>(
            *delayed_slice);
    assert(
        delayed_slice_write.delay == 2
        && delayed_slice_write.offset == 1);
    assert(
        std::ranges::none_of(
            delayed_slice_operations,
            [](const auto& operation) {
                return std::holds_alternative<
                    fsim::runtime::simir::WaitFor>(operation);
            }));

    const auto verify_event_assignment =
        [&](const std::size_t process_index,
            const bool nonblocking,
            const std::size_t sensitivity_count) {
            const auto& operations =
                elaborated_assignment_timing.design
                    ->processes()[process_index]
                    .operations;
            const auto wait_index =
                operation_index
                    .operator()<fsim::runtime::simir::WaitOn>(
                        operations);
            assert(wait_debug_index(operations) < wait_index);
            assert(
                wait_index
                < operation_index
                      .operator()<fsim::runtime::simir::ReadSignal>(
                          operations));
            if (nonblocking) {
                assert(
                    wait_index
                    < operation_index
                          .operator()<
                              fsim::runtime::simir::WriteUpdate>(
                              operations));
            } else {
                assert(
                    wait_index
                    < operation_index
                          .operator()<
                              fsim::runtime::simir::WriteBlocking>(
                              operations));
            }
            const auto& wait =
                std::get<fsim::runtime::simir::WaitOn>(
                    operations[wait_index]);
            assert(wait.signals.size() == sensitivity_count);
        };
    verify_event_assignment(2, false, 1);
    verify_event_assignment(3, true, 2);
    verify_event_assignment(4, true, 1);
    const auto& wildcard_wait =
        std::get<fsim::runtime::simir::WaitOn>(
            elaborated_assignment_timing.design
                ->processes()[4]
                .operations[
                    operation_index
                        .operator()<fsim::runtime::simir::WaitOn>(
                            elaborated_assignment_timing.design
                                ->processes()[4]
                                .operations)]);
    const auto source_signal =
        elaborated_assignment_timing.design->find_signal("source");
    assert(
        source_signal
        && wildcard_wait.signals
               == std::vector<fsim::runtime::simir::SignalId>{
                   *source_signal});

    const auto& local_operations =
        elaborated_assignment_timing.design
            ->processes()[5]
            .operations;
    assert(
        operation_index
            .operator()<fsim::runtime::simir::ReadSignal>(
                local_operations)
        < operation_index
              .operator()<fsim::runtime::simir::WaitFor>(
                  local_operations));
    assert(
        operation_index
            .operator()<fsim::runtime::simir::WaitFor>(
                local_operations)
        < operation_index
              .operator()<fsim::runtime::simir::CopyRegister>(
                  local_operations));

    auto inconsistent_assignment_control =
        fsim::frontend::parse_text(
            "inconsistent_assignment_control.sv",
            R"(
module inconsistent_assignment_control;
  logic source;
  logic result;
  initial result = source;
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(inconsistent_assignment_control.ok());
    inconsistent_assignment_control.design.units.front()
        .processes.front()
        .statements.front()
        .procedural_assignment_control =
        fsim::frontend::ProceduralAssignmentControl::Event;
    const auto rejected_assignment_control =
        fsim::elaboration::elaborate(
            inconsistent_assignment_control.design,
            "inconsistent_assignment_control");
    assert(!rejected_assignment_control.ok());
    assert(has_diagnostic(
        rejected_assignment_control, "FSIM-ELAB-105"));

    const auto width_mismatch = fsim::frontend::parse_text(
        "width_mismatch.sv",
        R"(
module width_mismatch;
  logic [7:0] q;
  initial q = 4'b1010;
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(width_mismatch.ok());
    const auto rejected_width_mismatch =
        fsim::elaboration::elaborate(
            width_mismatch.design, "width_mismatch");
    assert(!rejected_width_mismatch.ok());
    assert(has_diagnostic(
        rejected_width_mismatch, "FSIM-ELAB-047"));

    auto unsupported_domain = fsim::frontend::parse_text(
        "unsupported_domain.sv",
        "module unsupported_domain(input logic value); endmodule",
        fsim::frontend::Language::SystemVerilog2017);
    assert(unsupported_domain.ok());
    unsupported_domain.design.units.front().ports.front().type.domain =
        fsim::frontend::ValueDomain::Unknown;
    const auto rejected_domain = fsim::elaboration::elaborate(
        unsupported_domain.design, "unsupported_domain");
    assert(!rejected_domain.ok());
    assert(has_diagnostic(
        rejected_domain, "FSIM-ELAB-TYPE-001"));

    const auto default_values = fsim::frontend::parse_text(
        "default_values.vhd",
        R"(
entity default_values is
  port (
    bit_value : out bit;
    boolean_value : out boolean;
    logic_value : out std_logic
  );
end entity;
architecture rtl of default_values is
begin
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(default_values.ok());
    const auto elaborated_defaults = fsim::elaboration::elaborate(
        default_values.design, "default_values");
    assert(elaborated_defaults.ok());
    auto default_interpreter =
        elaborated_defaults.design->create_interpreter();
    const auto bit_value =
        elaborated_defaults.design->find_signal("bit_value");
    const auto boolean_value =
        elaborated_defaults.design->find_signal("boolean_value");
    const auto logic_value =
        elaborated_defaults.design->find_signal("logic_value");
    assert(bit_value && boolean_value && logic_value);
    assert(
        default_interpreter->signal_value(*bit_value).to_msb_string()
        == "0");
    assert(
        default_interpreter->signal_value(*boolean_value).to_msb_string()
        == "0");
    assert(
        default_interpreter->signal_value(*logic_value).to_msb_string()
        == "U");

    const auto nine_state_literals = fsim::frontend::parse_text(
        "nine_state_literals.vhd",
        R"(
entity nine_state_literals is
  port (value : out std_logic_vector(7 downto 0));
end entity;
architecture rtl of nine_state_literals is
begin
  value <= "ULH-WZ01";
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(nine_state_literals.ok());
    const auto elaborated_nine_state_literals =
        fsim::elaboration::elaborate(
            nine_state_literals.design, "nine_state_literals");
    assert(elaborated_nine_state_literals.ok());
    auto nine_state_interpreter =
        elaborated_nine_state_literals.design->create_interpreter();
    const auto nine_state_value =
        elaborated_nine_state_literals.design->find_signal("value");
    assert(nine_state_value);
    const auto nine_state_run = nine_state_interpreter->run();
    assert(
        nine_state_run.status
        == fsim::runtime::RunStatus::completed);
    assert(
        nine_state_interpreter
            ->signal_value(*nine_state_value)
            .to_msb_string()
        == "ULH-WZ01");

    const auto lossy_assignment = fsim::frontend::parse_text(
        "lossy_assignment.sv",
        R"(
module lossy_assignment(input logic source, output bit target);
  assign target = source;
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(lossy_assignment.ok());
    const auto rejected_lossy_assignment =
        fsim::elaboration::elaborate(
            lossy_assignment.design, "lossy_assignment");
    assert(!rejected_lossy_assignment.ok());
    assert(has_diagnostic(
        rejected_lossy_assignment, "FSIM-ELAB-050"));

    const auto two_state_assignment = fsim::frontend::parse_text(
        "two_state_assignment.sv",
        R"(
module two_state_assignment(output bit target);
  initial target = 1'b1;
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(two_state_assignment.ok());
    const auto accepted_two_state_assignment =
        fsim::elaboration::elaborate(
            two_state_assignment.design, "two_state_assignment");
    assert(accepted_two_state_assignment.ok());

    const auto unknown_condition = fsim::frontend::parse_text(
        "unknown_condition.sv",
        R"(
module unknown_condition;
  logic condition;
  logic result;
  initial begin
    if (condition)
      result = 1'b1;
    else
      result = 1'b0;
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(unknown_condition.ok());
    const auto elaborated_unknown_condition =
        fsim::elaboration::elaborate(
            unknown_condition.design, "unknown_condition");
    assert(elaborated_unknown_condition.ok());
    auto unknown_condition_interpreter =
        elaborated_unknown_condition.design->create_interpreter();
    const auto condition_result =
        elaborated_unknown_condition.design->find_signal("result");
    assert(condition_result);
    const auto unknown_condition_run =
        unknown_condition_interpreter->run();
    assert(
        unknown_condition_run.status
        == fsim::runtime::RunStatus::completed);
    assert(
        unknown_condition_interpreter
            ->signal_value(*condition_result)
            .to_msb_string()
        == "0");

    const std::vector<fsim::elaboration::Binding>
        architectureless_binding{
            {"tb.u_counter", "vhdl:work.counter", std::nullopt},
        };
    const auto rejected_architectureless =
        fsim::elaboration::elaborate(
            parsed_mixed_sv.design,
            "sv:work.tb",
            architectureless_binding);
    assert(!rejected_architectureless.ok());
    assert(has_diagnostic(
        rejected_architectureless, "FSIM-ELAB-BIND-016"));

    const auto multiple_drivers = fsim::frontend::parse_text(
        "multiple_drivers.sv",
        R"(
module driver(output logic value);
  assign value = 1'b0;
endmodule
module other_driver(output logic value);
  assign value = 1'b1;
endmodule
module driver_top;
  logic shared;
  driver first(.value(shared));
  other_driver second(.value(shared));
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(multiple_drivers.ok());
    const auto missing_resolver = fsim::elaboration::elaborate(
        multiple_drivers.design, "driver_top");
    assert(!missing_resolver.ok());
    assert(has_diagnostic(missing_resolver, "FSIM-ELAB-BIND-024"));
    const std::vector<fsim::elaboration::Binding> resolver_bindings{
        {"driver_top.first", "sv:work.driver", std::string{"sv_wire"}},
        {"driver_top.second", "sv:work.other_driver", std::string{"sv_wire"}},
    };
    const auto resolved_boundary = fsim::elaboration::elaborate(
        multiple_drivers.design, "driver_top", resolver_bindings);
    assert(resolved_boundary.ok());
    const auto shared =
        resolved_boundary.design->find_signal("shared");
    assert(shared);
    assert(
        resolved_boundary.design->signals().at(*shared).resolution
        == fsim::runtime::simir::ResolutionKind::sv_wire);
    auto resolved_boundary_interpreter =
        resolved_boundary.design->create_interpreter();
    const auto resolved_boundary_result =
        resolved_boundary_interpreter->run();
    assert(
        resolved_boundary_result.status
        == fsim::runtime::RunStatus::completed);
    assert(
        resolved_boundary_interpreter
            ->signal_value(*shared)
            .to_msb_string()
        == "X");

    const std::vector<fsim::elaboration::Binding> invalid_resolver_bindings{
        {"driver_top.first", "sv:work.driver", std::string{"wired"}},
        {"driver_top.second", "sv:work.other_driver", std::string{"wired"}},
    };
    const auto invalid_resolver = fsim::elaboration::elaborate(
        multiple_drivers.design,
        "driver_top",
        invalid_resolver_bindings);
    assert(!invalid_resolver.ok());
    assert(has_diagnostic(
        invalid_resolver, "FSIM-ELAB-BIND-050"));

    const auto process_drivers = fsim::frontend::parse_text(
        "process_drivers.sv",
        R"(
module process_drivers;
  logic q;
  initial q = 1'b0;
  initial q = 1'b1;
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(process_drivers.ok());
    const auto rejected_process_drivers = fsim::elaboration::elaborate(
        process_drivers.design, "process_drivers");
    assert(!rejected_process_drivers.ok());
    assert(has_diagnostic(
        rejected_process_drivers, "FSIM-ELAB-DRV-001"));

    const auto selected_process_drivers =
        fsim::frontend::parse_text(
            "selected_process_drivers.sv",
            R"(
module selected_process_drivers;
  logic [3:0] q;
  initial q[0] = 1'b0;
  initial q[3:2] = 2'b11;
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(selected_process_drivers.ok());
    const auto rejected_selected_process_drivers =
        fsim::elaboration::elaborate(
            selected_process_drivers.design,
            "selected_process_drivers");
    assert(!rejected_selected_process_drivers.ok());
    assert(has_diagnostic(
        rejected_selected_process_drivers,
        "FSIM-ELAB-DRV-001"));

    const auto native_wire_drivers = fsim::frontend::parse_text(
        "native_wire_drivers.sv",
        R"(
module native_wire_drivers;
  wire q;
  native_wire_zero zero(.value(q));
  native_wire_one one(.value(q));
endmodule
module native_wire_zero(output logic value);
  assign value = 1'b0;
endmodule
module native_wire_one(output logic value);
  assign value = 1'b1;
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(native_wire_drivers.ok());
    const auto elaborated_native_wire =
        fsim::elaboration::elaborate(
            native_wire_drivers.design, "native_wire_drivers");
    assert(elaborated_native_wire.ok());
    const auto native_wire_q =
        elaborated_native_wire.design->find_signal("q");
    assert(native_wire_q);
    assert(
        elaborated_native_wire.design->signals()
            .at(*native_wire_q)
            .resolution
        == fsim::runtime::simir::ResolutionKind::sv_wire);
    auto native_wire_interpreter =
        elaborated_native_wire.design->create_interpreter();
    const auto native_wire_result =
        native_wire_interpreter->run();
    assert(
        native_wire_result.status
        == fsim::runtime::RunStatus::completed);
    assert(
        native_wire_interpreter
            ->signal_value(*native_wire_q)
            .to_msb_string()
        == "X");

    const auto native_std_logic_drivers =
        fsim::frontend::parse_text(
            "native_std_logic_drivers.vhd",
            R"(
entity native_std_logic_drivers is
end entity;
architecture rtl of native_std_logic_drivers is
  signal q : std_logic;
begin
  q <= '0';
  q <= '1';
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(native_std_logic_drivers.ok());
    const auto elaborated_native_std_logic =
        fsim::elaboration::elaborate(
            native_std_logic_drivers.design,
            "native_std_logic_drivers");
    assert(elaborated_native_std_logic.ok());
    const auto native_std_logic_q =
        elaborated_native_std_logic.design->find_signal("q");
    assert(native_std_logic_q);
    assert(
        elaborated_native_std_logic.design->signals()
            .at(*native_std_logic_q)
            .resolution
        == fsim::runtime::simir::ResolutionKind::std_logic);
    auto native_std_logic_interpreter =
        elaborated_native_std_logic.design->create_interpreter();
    const auto native_std_logic_result =
        native_std_logic_interpreter->run();
    assert(
        native_std_logic_result.status
        == fsim::runtime::RunStatus::completed);
    assert(
        native_std_logic_interpreter
            ->signal_value(*native_std_logic_q)
            .to_msb_string()
        == "X");

    const auto local_variables = fsim::frontend::parse_text(
        "local_variables.sv",
        R"(
module local_variables;
  logic q;
  initial begin
    logic state = 1'b0;
    state = 1'b1;
    q = state;
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(local_variables.ok());
    const auto elaborated_locals = fsim::elaboration::elaborate(
        local_variables.design, "local_variables");
    assert(elaborated_locals.ok());
    const auto& local_process =
        elaborated_locals.design->processes().front();
    assert(
        local_process.debug_locals.size() == 1
        && local_process.debug_locals.front().name == "state"
        && local_process.debug_locals.front().type_name == "logic"
        && local_process.debug_locals.front().width == 1);
    assert(std::any_of(
        local_process.operations.begin(),
        local_process.operations.end(),
        [](const fsim::runtime::simir::Operation& operation) {
          return std::holds_alternative<
              fsim::runtime::simir::CopyRegister>(operation);
        }));
    auto local_interpreter =
        elaborated_locals.design->create_interpreter();
    const auto local_run = local_interpreter->run();
    assert(local_run.status == fsim::runtime::RunStatus::completed);
    assert(
        local_interpreter->read_debug_local(0, 0).to_msb_string()
        == "1");
    const auto local_q =
        elaborated_locals.design->find_signal("q");
    assert(local_q);
    assert(
        local_interpreter->signal_value(*local_q).to_msb_string()
        == "1");

    const auto scoped_variables = fsim::frontend::parse_text(
        "scoped_variables.sv",
        R"(
module scoped_variables;
  logic [7:0] result;
  logic [1:0] count;
  logic [7:0] value;
  initial begin : root_scope
    logic [7:0] value = 8'd1;
    result = 8'd0;
    count = 2'd0;
    begin : inner_scope
      logic [7:0] value = 8'd4;
      result = result + value;
    end : inner_scope
    result = result + value;
    begin
      logic [7:0] anonymous = 8'd1;
      result = result + anonymous;
    end
    for (int lane = 0; lane < 2; lane++) begin : each
      logic [7:0] scratch = 8'd2;
      result = result + scratch;
      scratch = 8'd9;
    end : each
    while (count < 2) begin : dynamic
      logic [7:0] scratch = 8'd3;
      result = result + scratch;
      scratch = 8'd7;
      count++;
    end : dynamic
    if (count == 2) begin : selected
      logic [7:0] branch = 8'd5;
      result = result + branch;
    end : selected
    else begin : alternate
      logic [7:0] branch = 8'd8;
      result = result + branch;
    end : alternate
  end : root_scope
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(scoped_variables.ok());
    const auto elaborated_scoped =
        fsim::elaboration::elaborate(
            scoped_variables.design, "scoped_variables");
    if (!elaborated_scoped.ok()) {
        for (const auto& diagnostic :
             elaborated_scoped.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(elaborated_scoped.ok());
    const auto& scoped_process =
        elaborated_scoped.design->processes().front();
    assert(scoped_process.debug_locals.size() == 7);
    assert(
        scoped_process.debug_locals[0].name
        == "root_scope.value");
    assert(
        scoped_process.debug_locals[1].name
        == "root_scope.inner_scope.value");
    assert(
        scoped_process.debug_locals[2].name.starts_with(
            "root_scope.$block_")
        && scoped_process.debug_locals[2].name.ends_with(
            ".anonymous"));
    assert(
        scoped_process.debug_locals[3].name
        == "root_scope.each.scratch");
    assert(
        scoped_process.debug_locals[4].name
        == "root_scope.dynamic.scratch");
    assert(
        scoped_process.debug_locals[5].name
        == "root_scope.selected.branch");
    assert(
        scoped_process.debug_locals[6].name
        == "root_scope.alternate.branch");
    // The statically expanded two-iteration loop reuses one frame register
    // for its lexical declaration instead of duplicating debugger objects.
    assert(
        std::count_if(
            scoped_process.debug_locals.begin(),
            scoped_process.debug_locals.end(),
            [](const auto& local) {
                return local.name
                    == "root_scope.each.scratch";
            })
        == 1);
    auto scoped_interpreter =
        elaborated_scoped.design->create_interpreter();
    const auto scoped_run = scoped_interpreter->run();
    assert(scoped_run.status == fsim::runtime::RunStatus::completed);
    const auto scoped_result =
        elaborated_scoped.design->find_signal("result");
    const auto scoped_count =
        elaborated_scoped.design->find_signal("count");
    const auto shadowed_signal =
        elaborated_scoped.design->find_signal("value");
    assert(scoped_result && scoped_count && shadowed_signal);
    assert(
        scoped_interpreter
            ->signal_value(*scoped_result)
            .to_msb_string()
        == "00010101");
    assert(
        scoped_interpreter
            ->signal_value(*scoped_count)
            .to_msb_string()
        == "10");
    assert(
        scoped_interpreter
            ->signal_value(*shadowed_signal)
            .to_msb_string()
        == "XXXXXXXX");
    assert(
        scoped_interpreter->read_debug_local(0, 0).to_msb_string()
        == "00000001");
    assert(
        scoped_interpreter->read_debug_local(0, 1).to_msb_string()
        == "00000100");
    assert(
        scoped_interpreter->read_debug_local(0, 2).to_msb_string()
        == "00000001");
    assert(
        scoped_interpreter->read_debug_local(0, 3).to_msb_string()
        == "00001001");
    assert(
        scoped_interpreter->read_debug_local(0, 4).to_msb_string()
        == "00000111");
    assert(
        scoped_interpreter->read_debug_local(0, 5).to_msb_string()
        == "00000101");

    const auto duplicate_scoped_variables =
        fsim::frontend::parse_text(
            "duplicate_scoped_variables.sv",
            R"(
module duplicate_scoped_variables;
  initial begin
    logic duplicate;
    logic duplicate;
  end
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(duplicate_scoped_variables.ok());
    const auto rejected_duplicate_scoped =
        fsim::elaboration::elaborate(
            duplicate_scoped_variables.design,
            "duplicate_scoped_variables");
    assert(!rejected_duplicate_scoped.ok());
    assert(has_diagnostic(
        rejected_duplicate_scoped, "FSIM-ELAB-053"));

    const auto vhdl_call_point = fsim::frontend::parse_text(
        "vhdl_call_point.vhd",
        R"(
entity vhdl_call_point is end entity;
architecture rtl of vhdl_call_point is
  signal input_value : std_logic;
  signal result : boolean;
begin
  observe: process
  begin
    result <= input_value'stable;
    wait;
  end process;
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(vhdl_call_point.ok());
    const auto elaborated_vhdl_call_point =
        fsim::elaboration::elaborate(
            vhdl_call_point.design,
            "vhdl:work.vhdl_call_point(rtl)");
    assert(elaborated_vhdl_call_point.ok());
    assert(std::any_of(
        elaborated_vhdl_call_point.design->processes().front()
            .operations.begin(),
        elaborated_vhdl_call_point.design->processes().front()
            .operations.end(),
        [](const fsim::runtime::simir::Operation& operation) {
          const auto* point =
              std::get_if<fsim::runtime::simir::DebugPoint>(
                  &operation);
          return point != nullptr
              && point->kind
                  == fsim::runtime::simir::DebugPointKind::call
              && point->source.path == "vhdl_call_point.vhd"
              && point->source.line == 9;
        }));

    const auto vhdl_local_variables = fsim::frontend::parse_text(
        "local_variables.vhd",
        R"(
entity local_variables is
  port (clk : in std_logic; q : out std_logic);
end entity;
architecture rtl of local_variables is begin
  worker: process(clk)
    variable state : std_logic := '0';
  begin
    state := not state;
    q <= state;
  end process;
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(vhdl_local_variables.ok());
    const auto elaborated_vhdl_locals =
        fsim::elaboration::elaborate(
            vhdl_local_variables.design,
            "vhdl:work.local_variables(rtl)");
    assert(elaborated_vhdl_locals.ok());
    const auto& vhdl_local_process =
        elaborated_vhdl_locals.design->processes().front();
    assert(
        vhdl_local_process.debug_locals.size() == 1
        && vhdl_local_process.debug_locals.front().name == "state"
        && vhdl_local_process.debug_locals.front().type_name
            == "std_logic");
    assert(std::count_if(
               vhdl_local_process.operations.begin(),
               vhdl_local_process.operations.end(),
               [](const fsim::runtime::simir::Operation& operation) {
                 return std::holds_alternative<
                     fsim::runtime::simir::CopyRegister>(operation);
               })
           >= 2);
    auto vhdl_local_interpreter =
        elaborated_vhdl_locals.design->create_interpreter();
    const auto vhdl_local_clk =
        elaborated_vhdl_locals.design->find_signal("clk");
    const auto vhdl_local_q =
        elaborated_vhdl_locals.design->find_signal("q");
    assert(vhdl_local_clk && vhdl_local_q);
    vhdl_local_interpreter->schedule_signal_at(
        *vhdl_local_clk,
        fsim::runtime::PackedLogic4::from_msb_string("1"),
        1);
    const auto vhdl_local_run = vhdl_local_interpreter->run(2);
    assert(
        vhdl_local_run.status
        == fsim::runtime::RunStatus::time_limit);
    assert(
        vhdl_local_interpreter
            ->signal_value(*vhdl_local_q)
            .to_msb_string()
        == "0");
    assert(
        vhdl_local_interpreter
            ->read_debug_local(0, 0)
            .to_msb_string()
        == "0");

    const auto vhdl_waits = fsim::frontend::parse_text(
        "waits.vhd",
        R"(
entity waits is end entity;
architecture rtl of waits is
  signal trigger : std_logic;
  signal q : std_logic;
begin
  worker: process
  begin
    q <= '0';
    wait for 2 ns;
    q <= '1';
    wait on trigger;
    q <= '0';
    wait on trigger;
  end process;
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(vhdl_waits.ok());
    const auto elaborated_vhdl_waits =
        fsim::elaboration::elaborate(
            vhdl_waits.design, "vhdl:work.waits(rtl)");
    assert(elaborated_vhdl_waits.ok());
    const auto& vhdl_wait_process =
        elaborated_vhdl_waits.design->processes().front();
    assert(std::count_if(
               vhdl_wait_process.operations.begin(),
               vhdl_wait_process.operations.end(),
               [](const fsim::runtime::simir::Operation& operation) {
                 return std::holds_alternative<
                            fsim::runtime::simir::WaitFor>(operation)
                     || std::holds_alternative<
                            fsim::runtime::simir::WaitOn>(operation);
               })
           == 3);
    assert(std::holds_alternative<fsim::runtime::simir::Jump>(
        vhdl_wait_process.operations.back()));
    auto vhdl_wait_interpreter =
        elaborated_vhdl_waits.design->create_interpreter();
    const auto vhdl_wait_trigger =
        elaborated_vhdl_waits.design->find_signal("trigger");
    const auto vhdl_wait_q =
        elaborated_vhdl_waits.design->find_signal("q");
    assert(vhdl_wait_trigger && vhdl_wait_q);
    vhdl_wait_interpreter->schedule_signal_at(
        *vhdl_wait_trigger,
        fsim::runtime::PackedLogic4::from_msb_string("1"),
        3);
    const auto vhdl_wait_mid = vhdl_wait_interpreter->run(2);
    assert(
        vhdl_wait_mid.status
        == fsim::runtime::RunStatus::time_limit);
    assert(
        vhdl_wait_interpreter
            ->signal_value(*vhdl_wait_q)
            .to_msb_string()
        == "1");
    const auto vhdl_wait_end = vhdl_wait_interpreter->run(4);
    assert(
        vhdl_wait_end.status
        == fsim::runtime::RunStatus::time_limit);
    assert(
        vhdl_wait_interpreter
            ->signal_value(*vhdl_wait_q)
            .to_msb_string()
        == "0");

    const auto sv_events = fsim::frontend::parse_text(
        "events.sv",
        R"(
module events;
  logic trigger;
  logic observed;
  initial begin
    trigger = 1'b0;
    #1 trigger = 1'b1;
    #1 trigger = 1'b0;
    #1 $finish;
  end
  initial begin
    @(posedge trigger);
    observed = trigger;
    @(negedge trigger) observed = trigger;
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(sv_events.ok());
    const auto elaborated_sv_events =
        fsim::elaboration::elaborate(
            sv_events.design, "sv:work.events");
    assert(elaborated_sv_events.ok());
    assert(elaborated_sv_events.design->processes().size() == 2);
    const auto& observer_process =
        elaborated_sv_events.design->processes().back();
    assert(std::count_if(
               observer_process.operations.begin(),
               observer_process.operations.end(),
               [](const fsim::runtime::simir::Operation& operation) {
                 return std::holds_alternative<
                     fsim::runtime::simir::WaitOn>(operation);
               })
           == 2);
    const auto first_dynamic_wait_operation = std::find_if(
        observer_process.operations.begin(),
        observer_process.operations.end(),
        [](const fsim::runtime::simir::Operation& operation) {
          return std::holds_alternative<
              fsim::runtime::simir::WaitOn>(operation);
        });
    const auto second_dynamic_wait_operation = std::find_if(
        std::next(first_dynamic_wait_operation),
        observer_process.operations.end(),
        [](const fsim::runtime::simir::Operation& operation) {
          return std::holds_alternative<
              fsim::runtime::simir::WaitOn>(operation);
        });
    const auto& first_dynamic_wait =
        std::get<fsim::runtime::simir::WaitOn>(
            *first_dynamic_wait_operation);
    const auto& second_dynamic_wait =
        std::get<fsim::runtime::simir::WaitOn>(
            *second_dynamic_wait_operation);
    assert(
        first_dynamic_wait.edges.size() == 1
        && first_dynamic_wait.edges.front()
            == fsim::runtime::simir::EdgeKind::posedge);
    assert(
        second_dynamic_wait.edges.size() == 1
        && second_dynamic_wait.edges.front()
            == fsim::runtime::simir::EdgeKind::negedge);
    auto sv_event_interpreter =
        elaborated_sv_events.design->create_interpreter();
    const auto sv_observed =
        elaborated_sv_events.design->find_signal("observed");
    assert(sv_observed);
    const auto sv_event_mid = sv_event_interpreter->run(1);
    assert(
        sv_event_mid.status
        == fsim::runtime::RunStatus::time_limit);
    assert(
        sv_event_interpreter
            ->signal_value(*sv_observed)
            .to_msb_string()
        == "1");
    const auto sv_event_end = sv_event_interpreter->run();
    assert(
        sv_event_end.status
        == fsim::runtime::RunStatus::stopped);
    assert(
        sv_event_interpreter
            ->signal_value(*sv_observed)
            .to_msb_string()
        == "0");

    const auto vhdl_condition_wait =
        fsim::frontend::parse_text(
            "condition_wait.vhd",
            R"(
entity condition_wait is end entity;
architecture rtl of condition_wait is
  signal trigger : boolean;
  signal observed : boolean;
begin
  observer: process
  begin
    wait until trigger;
    observed <= true;
    wait on trigger;
  end process;
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(vhdl_condition_wait.ok());
    const auto elaborated_vhdl_condition_wait =
        fsim::elaboration::elaborate(
            vhdl_condition_wait.design,
            "vhdl:work.condition_wait(rtl)");
    assert(elaborated_vhdl_condition_wait.ok());
    const auto& vhdl_condition_wait_process =
        elaborated_vhdl_condition_wait.design
            ->processes().front();
    assert(
        std::count_if(
            vhdl_condition_wait_process.operations.begin(),
            vhdl_condition_wait_process.operations.end(),
            [](const fsim::runtime::simir::Operation& operation) {
              return std::holds_alternative<
                  fsim::runtime::simir::WaitOn>(operation);
            })
        == 3);
    auto vhdl_condition_wait_interpreter =
        elaborated_vhdl_condition_wait.design
            ->create_interpreter();
    const auto vhdl_condition_trigger =
        elaborated_vhdl_condition_wait.design
            ->find_signal("trigger");
    const auto vhdl_condition_observed =
        elaborated_vhdl_condition_wait.design
            ->find_signal("observed");
    assert(vhdl_condition_trigger && vhdl_condition_observed);
    vhdl_condition_wait_interpreter->schedule_signal_at(
        *vhdl_condition_trigger,
        fsim::runtime::PackedLogic4::from_msb_string("0"),
        1);
    vhdl_condition_wait_interpreter->schedule_signal_at(
        *vhdl_condition_trigger,
        fsim::runtime::PackedLogic4::from_msb_string("1"),
        2);
    const auto vhdl_condition_wait_before =
        vhdl_condition_wait_interpreter->run(1);
    assert(
        vhdl_condition_wait_before.status
        == fsim::runtime::RunStatus::time_limit);
    assert(
        vhdl_condition_wait_interpreter
            ->signal_value(*vhdl_condition_observed)
            .to_msb_string()
        == "0");
    const auto vhdl_condition_wait_after =
        vhdl_condition_wait_interpreter->run(3);
    assert(
        vhdl_condition_wait_after.status
        == fsim::runtime::RunStatus::time_limit);
    assert(
        vhdl_condition_wait_interpreter
            ->signal_value(*vhdl_condition_observed)
            .to_msb_string()
        == "1");

    const auto vhdl_combined_wait =
        fsim::frontend::parse_text(
            "combined_wait.vhd",
            R"(
entity combined_wait is end entity;
architecture rtl of combined_wait is
  signal trigger : boolean;
  signal timed_result : boolean;
  signal event_result : boolean;
  signal constant_timeout_result : boolean;
  signal permanent_result : boolean;
begin
  driver: process
  begin
    wait for 1 ns;
    trigger <= true;
    wait for 2 ns;
    trigger <= false;
    wait;
  end process;
  observer: process
  begin
    wait on trigger until false for 2 ns;
    timed_result <= true;
    wait on trigger until not trigger for 2 ns;
    event_result <= true;
    wait until true for 1 ns;
    constant_timeout_result <= true;
    wait until true;
    permanent_result <= true;
  end process;
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(vhdl_combined_wait.ok());
    const auto elaborated_vhdl_combined_wait =
        fsim::elaboration::elaborate(
            vhdl_combined_wait.design,
            "vhdl:work.combined_wait(rtl)");
    if (!elaborated_vhdl_combined_wait.ok()) {
        for (const auto& diagnostic :
             elaborated_vhdl_combined_wait.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(elaborated_vhdl_combined_wait.ok());
    const auto& combined_wait_observer =
        elaborated_vhdl_combined_wait.design
            ->processes()
            .back();
    assert(std::ranges::any_of(
        combined_wait_observer.operations,
        [](const fsim::runtime::simir::Operation& operation) {
            const auto* wait =
                std::get_if<fsim::runtime::simir::WaitOn>(
                    &operation);
            return wait != nullptr
                && wait->timeout
                && wait->timeout_result
                && wait->timeout_origin;
        }));
    assert(std::ranges::any_of(
        combined_wait_observer.operations,
        [](const fsim::runtime::simir::Operation& operation) {
            return std::holds_alternative<
                fsim::runtime::simir::WaitForever>(
                    operation);
        }));

    auto combined_wait_interpreter =
        elaborated_vhdl_combined_wait.design
            ->create_interpreter();
    const auto combined_timed =
        elaborated_vhdl_combined_wait.design
            ->find_signal("timed_result");
    const auto combined_event =
        elaborated_vhdl_combined_wait.design
            ->find_signal("event_result");
    const auto combined_constant_timeout =
        elaborated_vhdl_combined_wait.design
            ->find_signal("constant_timeout_result");
    const auto combined_permanent =
        elaborated_vhdl_combined_wait.design
            ->find_signal("permanent_result");
    assert(
        combined_timed
        && combined_event
        && combined_constant_timeout
        && combined_permanent);
    const auto combined_before_timeout =
        combined_wait_interpreter->run(1);
    assert(
        combined_before_timeout.status
        == fsim::runtime::RunStatus::time_limit);
    assert(
        combined_wait_interpreter
            ->signal_value(*combined_timed)
            .to_msb_string()
        == "0");
    const auto combined_after_timeout =
        combined_wait_interpreter->run(2);
    assert(
        combined_after_timeout.status
        == fsim::runtime::RunStatus::time_limit);
    assert(
        combined_wait_interpreter
            ->signal_value(*combined_timed)
            .to_msb_string()
        == "1");
    const auto combined_after_event =
        combined_wait_interpreter->run(3);
    assert(
        combined_after_event.status
        == fsim::runtime::RunStatus::time_limit);
    assert(
        combined_wait_interpreter
            ->signal_value(*combined_event)
            .to_msb_string()
        == "1");
    const auto combined_after_constant_timeout =
        combined_wait_interpreter->run(4);
    assert(
        combined_after_constant_timeout.status
        == fsim::runtime::RunStatus::completed);
    assert(
        combined_wait_interpreter
            ->signal_value(*combined_constant_timeout)
            .to_msb_string()
        == "1");
    assert(
        combined_wait_interpreter
            ->signal_value(*combined_permanent)
            .to_msb_string()
        == "0");

    const auto sv_condition_wait =
        fsim::frontend::parse_text(
            "condition_wait.sv",
            R"(
module condition_wait;
  logic gate;
  logic enable;
  logic observed;
  logic constant_wait_result;
  initial begin
    gate = 1'b0;
    enable = 1'b0;
    #1 gate = 1'bx;
    #1 enable = 1'b1;
    #1 $finish;
  end
  initial begin
    observed = 1'b0;
    wait (gate || enable) observed = 1'b1;
  end
  initial begin
    constant_wait_result = 1'b0;
    wait (1'b0);
    constant_wait_result = 1'b1;
  end
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(sv_condition_wait.ok());
    const auto elaborated_sv_condition_wait =
        fsim::elaboration::elaborate(
            sv_condition_wait.design,
            "sv:work.condition_wait");
    if (!elaborated_sv_condition_wait.ok()) {
        for (const auto& diagnostic :
             elaborated_sv_condition_wait.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(elaborated_sv_condition_wait.ok());
    assert(std::ranges::any_of(
        elaborated_sv_condition_wait.design->processes(),
        [](const fsim::runtime::simir::Process& process) {
          return std::ranges::any_of(
              process.operations,
              [](const fsim::runtime::simir::Operation& operation) {
                const auto* wait = std::get_if<
                    fsim::runtime::simir::WaitOn>(
                    &operation);
                return wait != nullptr
                    && wait->signals.size() == 2;
              });
        }));
    auto sv_condition_wait_interpreter =
        elaborated_sv_condition_wait.design
            ->create_interpreter();
    const auto sv_condition_observed =
        elaborated_sv_condition_wait.design
            ->find_signal("observed");
    const auto sv_constant_wait_result =
        elaborated_sv_condition_wait.design
            ->find_signal("constant_wait_result");
    assert(sv_condition_observed && sv_constant_wait_result);
    const auto sv_condition_wait_before =
        sv_condition_wait_interpreter->run(1);
    assert(
        sv_condition_wait_before.status
        == fsim::runtime::RunStatus::time_limit);
    assert(
        sv_condition_wait_interpreter
            ->signal_value(*sv_condition_observed)
            .to_msb_string()
        == "0");
    const auto sv_condition_wait_after =
        sv_condition_wait_interpreter->run();
    assert(
        sv_condition_wait_after.status
        == fsim::runtime::RunStatus::stopped);
    assert(
        sv_condition_wait_interpreter
            ->signal_value(*sv_condition_observed)
            .to_msb_string()
        == "1");
    assert(
        sv_condition_wait_interpreter
            ->signal_value(*sv_constant_wait_result)
            .to_msb_string()
        == "0");

    const auto invalid_vhdl_condition_wait =
        fsim::frontend::parse_text(
            "invalid_condition_wait.vhd",
            R"(
entity invalid_condition_wait is end entity;
architecture rtl of invalid_condition_wait is
  signal trigger : std_logic;
begin
  observer: process
  begin
    wait until trigger;
  end process;
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(invalid_vhdl_condition_wait.ok());
    const auto rejected_vhdl_condition_wait =
        fsim::elaboration::elaborate(
            invalid_vhdl_condition_wait.design,
            "vhdl:work.invalid_condition_wait(rtl)");
    assert(!rejected_vhdl_condition_wait.ok());
    assert(has_diagnostic(
        rejected_vhdl_condition_wait, "FSIM-ELAB-079"));

    const auto unknown_wait = fsim::frontend::parse_text(
        "unknown_wait.vhd",
        R"(
entity unknown_wait is end entity;
architecture rtl of unknown_wait is begin
  worker: process begin
    wait on missing;
  end process;
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(unknown_wait.ok());
    const auto rejected_unknown_wait =
        fsim::elaboration::elaborate(
            unknown_wait.design,
            "vhdl:work.unknown_wait(rtl)");
    assert(!rejected_unknown_wait.ok());
    assert(has_diagnostic(rejected_unknown_wait, "FSIM-ELAB-059"));

    const auto vector_edge_wait = fsim::frontend::parse_text(
        "vector_edge_wait.sv",
        R"(
module vector_edge_wait;
  logic [1:0] trigger;
  initial @(posedge trigger);
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(vector_edge_wait.ok());
    const auto rejected_vector_edge_wait =
        fsim::elaboration::elaborate(
            vector_edge_wait.design, "sv:work.vector_edge_wait");
    assert(!rejected_vector_edge_wait.ok());
    assert(has_diagnostic(
        rejected_vector_edge_wait, "FSIM-ELAB-060"));

    const auto wildcard_processes = fsim::frontend::parse_text(
        "wildcard.sv",
        R"(
module wildcard_processes;
  logic a;
  logic q;
  logic y;
  logic latched;
  always @* q = a;
  always_comb y = ~q;
  always_latch if (a) latched = q;
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(wildcard_processes.ok());
    const auto elaborated_wildcard =
        fsim::elaboration::elaborate(
            wildcard_processes.design, "sv:work.wildcard_processes");
    assert(elaborated_wildcard.ok());
    assert(elaborated_wildcard.design->processes().size() == 3);
    const auto wildcard_a =
        elaborated_wildcard.design->find_signal("a");
    const auto wildcard_q =
        elaborated_wildcard.design->find_signal("q");
    const auto wildcard_y =
        elaborated_wildcard.design->find_signal("y");
    const auto wildcard_latched =
        elaborated_wildcard.design->find_signal("latched");
    assert(
        wildcard_a && wildcard_q && wildcard_y
        && wildcard_latched);
    assert((
        elaborated_wildcard.design->processes()[0]
            .static_sensitivity
        == std::vector<fsim::runtime::simir::Sensitivity>{
            {*wildcard_a,
             fsim::runtime::simir::EdgeKind::any}}));
    assert((
        elaborated_wildcard.design->processes()[1]
            .static_sensitivity
        == std::vector<fsim::runtime::simir::Sensitivity>{
            {*wildcard_q,
             fsim::runtime::simir::EdgeKind::any}}));
    assert((
        elaborated_wildcard.design->processes()[2]
            .static_sensitivity
        == std::vector<fsim::runtime::simir::Sensitivity>{
            {*wildcard_a,
             fsim::runtime::simir::EdgeKind::any},
            {*wildcard_q,
             fsim::runtime::simir::EdgeKind::any}}));
    auto wildcard_interpreter =
        elaborated_wildcard.design->create_interpreter();
    (void)wildcard_interpreter->run();
    wildcard_interpreter->deposit_signal(
        *wildcard_a,
        fsim::runtime::PackedLogic4::from_msb_string("0"));
    (void)wildcard_interpreter->run();
    assert(
        wildcard_interpreter
            ->signal_value(*wildcard_q)
            .to_msb_string()
        == "0");
    assert(
        wildcard_interpreter
            ->signal_value(*wildcard_y)
            .to_msb_string()
        == "1");
    wildcard_interpreter->deposit_signal(
        *wildcard_a,
        fsim::runtime::PackedLogic4::from_msb_string("1"));
    (void)wildcard_interpreter->run();
    assert(
        wildcard_interpreter
            ->signal_value(*wildcard_q)
            .to_msb_string()
        == "1");
    assert(
        wildcard_interpreter
            ->signal_value(*wildcard_y)
            .to_msb_string()
        == "0");
    assert(
        wildcard_interpreter
            ->signal_value(*wildcard_latched)
            .to_msb_string()
        == "1");

    const auto case_process = fsim::frontend::parse_text(
        "case_process.sv",
        R"(
module case_process;
  logic [1:0] selector;
  logic [1:0] result;
  always_comb case (selector)
    2'b00: result = 2'b00;
    2'b01, 2'b10: result = 2'b01;
    2'bx0: result = 2'b10;
    2'bz1: result = 2'b11;
    default: result = 2'b00;
  endcase
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(case_process.ok());
    const auto elaborated_case =
        fsim::elaboration::elaborate(
            case_process.design, "sv:work.case_process");
    assert(elaborated_case.ok());
    const auto case_selector =
        elaborated_case.design->find_signal("selector");
    const auto case_result =
        elaborated_case.design->find_signal("result");
    assert(case_selector && case_result);
    assert((
        elaborated_case.design->processes().front()
            .static_sensitivity
        == std::vector<fsim::runtime::simir::Sensitivity>{
            {*case_selector,
             fsim::runtime::simir::EdgeKind::any}}));
    const auto has_case_equality = std::any_of(
        elaborated_case.design->processes().front()
            .operations.begin(),
        elaborated_case.design->processes().front()
            .operations.end(),
        [](const fsim::runtime::simir::Operation& operation) {
            const auto* binary =
                std::get_if<fsim::runtime::simir::Binary>(
                    &operation);
            return binary != nullptr
                && binary->operation
                    == fsim::runtime::simir::BinaryOperator::
                        case_equal;
        });
    assert(has_case_equality);

    for (const auto& [keyword, expected_operator] :
         std::array{
             std::pair{
                 std::string_view{"casez"},
                 fsim::runtime::simir::BinaryOperator::casez_equal},
             std::pair{
                 std::string_view{"casex"},
                 fsim::runtime::simir::BinaryOperator::casex_equal}}) {
        const auto wildcard_case =
            fsim::frontend::parse_text(
                "wildcard_case.sv",
                "module wildcard_case;\n"
                "  logic [1:0] selector;\n"
                "  logic result;\n"
                "  always_comb "
                    + std::string{keyword}
                    + " (selector)\n"
                      "    2'b0z: result = 1'b1;\n"
                      "    default: result = 1'b0;\n"
                      "  endcase\n"
                      "endmodule\n",
                fsim::frontend::Language::SystemVerilog2017);
        assert(wildcard_case.ok());
        const auto elaborated_wildcard_case =
            fsim::elaboration::elaborate(
                wildcard_case.design, "sv:work.wildcard_case");
        assert(elaborated_wildcard_case.ok());
        assert(std::any_of(
            elaborated_wildcard_case.design->processes().front()
                .operations.begin(),
            elaborated_wildcard_case.design->processes().front()
                .operations.end(),
            [&](const fsim::runtime::simir::Operation& operation) {
                const auto* binary =
                    std::get_if<fsim::runtime::simir::Binary>(
                        &operation);
                return binary != nullptr
                    && binary->operation == expected_operator;
            }));
    }

    auto malformed_case_design = case_process.design;
    malformed_case_design.units.front().processes.front()
        .statements.front().case_match_kind =
        static_cast<fsim::frontend::CaseMatchKind>(255);
    const auto rejected_matching_mode =
        fsim::elaboration::elaborate(
            malformed_case_design, "sv:work.case_process");
    assert(!rejected_matching_mode.ok());
    assert(has_diagnostic(
        rejected_matching_mode, "FSIM-ELAB-081"));

    auto case_interpreter =
        elaborated_case.design->create_interpreter();
    (void)case_interpreter->run();
    for (const auto& [selector_value, expected] :
         std::vector<std::pair<std::string, std::string>>{
             {"00", "00"},
             {"01", "01"},
             {"10", "01"},
             {"x0", "10"},
             {"z1", "11"},
             {"11", "00"}}) {
        case_interpreter->deposit_signal(
            *case_selector,
            fsim::runtime::PackedLogic4::from_msb_string(
                selector_value));
        (void)case_interpreter->run();
        assert(
            case_interpreter
                ->signal_value(*case_result)
                .to_msb_string()
            == expected);
    }

    const auto mismatched_case = fsim::frontend::parse_text(
        "mismatched_case.sv",
        R"(
module mismatched_case;
  logic [1:0] selector;
  logic result;
  always_comb case (selector)
    1'b0: result = 1'b0;
    default: result = 1'b1;
  endcase
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(mismatched_case.ok());
    const auto rejected_case =
        fsim::elaboration::elaborate(
            mismatched_case.design, "sv:work.mismatched_case");
    assert(!rejected_case.ok());
    assert(has_diagnostic(rejected_case, "FSIM-ELAB-063"));

    const auto conditional_process = fsim::frontend::parse_text(
        "conditional_process.sv",
        R"(
module conditional_process;
  logic select;
  logic [3:0] lhs;
  logic [3:0] rhs;
  logic [3:0] result;
  always_comb result = select ? lhs : rhs;
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(conditional_process.ok());
    const auto elaborated_conditional =
        fsim::elaboration::elaborate(
            conditional_process.design,
            "sv:work.conditional_process");
    assert(elaborated_conditional.ok());
    const auto conditional_select =
        elaborated_conditional.design->find_signal("select");
    const auto conditional_lhs =
        elaborated_conditional.design->find_signal("lhs");
    const auto conditional_rhs =
        elaborated_conditional.design->find_signal("rhs");
    const auto conditional_result =
        elaborated_conditional.design->find_signal("result");
    assert(
        conditional_select && conditional_lhs
        && conditional_rhs && conditional_result);
    assert((
        elaborated_conditional.design->processes().front()
            .static_sensitivity
        == std::vector<fsim::runtime::simir::Sensitivity>{
            {*conditional_lhs,
             fsim::runtime::simir::EdgeKind::any},
            {*conditional_rhs,
             fsim::runtime::simir::EdgeKind::any},
            {*conditional_select,
             fsim::runtime::simir::EdgeKind::any}}));
    auto conditional_interpreter =
        elaborated_conditional.design->create_interpreter();
    conditional_interpreter->deposit_signal(
        *conditional_lhs,
        fsim::runtime::PackedLogic4::from_msb_string("101z"));
    conditional_interpreter->deposit_signal(
        *conditional_rhs,
        fsim::runtime::PackedLogic4::from_msb_string("100z"));
    conditional_interpreter->deposit_signal(
        *conditional_select,
        fsim::runtime::PackedLogic4::from_msb_string("0"));
    (void)conditional_interpreter->run();
    assert(
        conditional_interpreter
            ->signal_value(*conditional_result)
            .to_msb_string()
        == "100Z");
    conditional_interpreter->deposit_signal(
        *conditional_select,
        fsim::runtime::PackedLogic4::from_msb_string("1"));
    (void)conditional_interpreter->run();
    assert(
        conditional_interpreter
            ->signal_value(*conditional_result)
            .to_msb_string()
        == "101Z");
    for (const auto unknown : {"x", "z"}) {
        conditional_interpreter->deposit_signal(
            *conditional_select,
            fsim::runtime::PackedLogic4::from_msb_string(unknown));
        (void)conditional_interpreter->run();
        assert(
            conditional_interpreter
                ->signal_value(*conditional_result)
                .to_msb_string()
            == "10XZ");
    }

    const auto invalid_conditional = fsim::frontend::parse_text(
        "invalid_conditional.sv",
        R"(
module vector_condition;
  logic [1:0] select;
  logic result;
  always_comb result = select ? 1'b0 : 1'b1;
endmodule
module mismatched_alternatives;
  logic select;
  logic [1:0] result;
  always_comb result = select ? 2'b00 : 1'b1;
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(invalid_conditional.ok());
    const auto rejected_vector_condition =
        fsim::elaboration::elaborate(
            invalid_conditional.design,
            "sv:work.vector_condition");
    assert(!rejected_vector_condition.ok());
    assert(has_diagnostic(
        rejected_vector_condition, "FSIM-ELAB-064"));
    const auto rejected_alternatives =
        fsim::elaboration::elaborate(
            invalid_conditional.design,
            "sv:work.mismatched_alternatives");
    assert(!rejected_alternatives.ok());
    assert(has_diagnostic(
        rejected_alternatives, "FSIM-ELAB-065"));

    const auto comparison_process = fsim::frontend::parse_text(
        "comparison_process.sv",
        R"(
module comparison_process;
  logic [3:0] lhs;
  logic [3:0] rhs;
  logic neq;
  logic lt;
  logic le;
  logic gt;
  logic ge;
  logic logical_not;
  logic case_eq;
  logic case_neq;
  logic wildcard_eq;
  logic wildcard_neq;
  always_comb begin
    neq = lhs != rhs;
    lt = lhs < rhs;
    le = lhs <= rhs;
    gt = lhs > rhs;
    ge = lhs >= rhs;
    logical_not = !lhs;
    case_eq = lhs === rhs;
    case_neq = lhs !== rhs;
    wildcard_eq = lhs ==? rhs;
    wildcard_neq = lhs !=? rhs;
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(comparison_process.ok());
    const auto elaborated_comparisons =
        fsim::elaboration::elaborate(
            comparison_process.design,
            "sv:work.comparison_process");
    assert(elaborated_comparisons.ok());
    const auto comparison_lhs =
        elaborated_comparisons.design->find_signal("lhs");
    const auto comparison_rhs =
        elaborated_comparisons.design->find_signal("rhs");
    const auto comparison_neq =
        elaborated_comparisons.design->find_signal("neq");
    const auto comparison_lt =
        elaborated_comparisons.design->find_signal("lt");
    const auto comparison_le =
        elaborated_comparisons.design->find_signal("le");
    const auto comparison_gt =
        elaborated_comparisons.design->find_signal("gt");
    const auto comparison_ge =
        elaborated_comparisons.design->find_signal("ge");
    const auto comparison_not =
        elaborated_comparisons.design->find_signal("logical_not");
    const auto comparison_case_eq =
        elaborated_comparisons.design->find_signal("case_eq");
    const auto comparison_case_neq =
        elaborated_comparisons.design->find_signal("case_neq");
    const auto comparison_wildcard_eq =
        elaborated_comparisons.design->find_signal("wildcard_eq");
    const auto comparison_wildcard_neq =
        elaborated_comparisons.design->find_signal("wildcard_neq");
    assert(
        comparison_lhs && comparison_rhs && comparison_neq
        && comparison_lt && comparison_le && comparison_gt
        && comparison_ge && comparison_not
        && comparison_case_eq && comparison_case_neq
        && comparison_wildcard_eq && comparison_wildcard_neq);
    assert(std::any_of(
        elaborated_comparisons.design->processes().front()
            .operations.begin(),
        elaborated_comparisons.design->processes().front()
            .operations.end(),
        [](const fsim::runtime::simir::Operation& operation) {
          const auto* binary =
              std::get_if<fsim::runtime::simir::Binary>(&operation);
          return binary != nullptr
              && binary->operation
                  == fsim::runtime::simir::BinaryOperator::
                      wildcard_equal;
        }));
    auto comparison_interpreter =
        elaborated_comparisons.design->create_interpreter();
    const auto run_comparison =
        [&](const std::string_view lhs,
            const std::string_view rhs,
            const std::array<std::string_view, 10>& expected) {
          comparison_interpreter->deposit_signal(
              *comparison_lhs,
              fsim::runtime::PackedLogic4::from_msb_string(lhs));
          comparison_interpreter->deposit_signal(
              *comparison_rhs,
              fsim::runtime::PackedLogic4::from_msb_string(rhs));
          (void)comparison_interpreter->run();
          const std::array signals{
              *comparison_neq,
              *comparison_lt,
              *comparison_le,
              *comparison_gt,
              *comparison_ge,
              *comparison_not,
              *comparison_case_eq,
              *comparison_case_neq,
              *comparison_wildcard_eq,
              *comparison_wildcard_neq};
          for (std::size_t index = 0; index < signals.size();
               ++index) {
            assert(
                comparison_interpreter
                    ->signal_value(signals[index])
                    .to_msb_string()
                == expected[index]);
          }
        };
    run_comparison(
        "0010", "0011",
        {"1", "1", "1", "0", "0", "0", "0", "1", "0", "1"});
    run_comparison(
        "0000", "0000",
        {"0", "0", "1", "0", "1", "1", "1", "0", "1", "0"});
    run_comparison(
        "00X0", "0011",
        {"X", "X", "X", "X", "X", "X", "0", "1", "X", "X"});
    run_comparison(
        "01X0", "0011",
        {"X", "X", "X", "X", "X", "0", "0", "1", "X", "X"});
    run_comparison(
        "00X0", "00X0",
        {"X", "X", "X", "X", "X", "X", "1", "0", "1", "0"});
    run_comparison(
        "01Z0", "01Z0",
        {"X", "X", "X", "X", "X", "0", "1", "0", "1", "0"});
    run_comparison(
        "00X0", "00Z0",
        {"X", "X", "X", "X", "X", "X", "0", "1", "1", "0"});
    run_comparison(
        "X101", "0001",
        {"X", "X", "X", "X", "X", "0", "0", "1", "X", "X"});

    const auto signed_comparison = fsim::frontend::parse_text(
        "signed_comparison.sv",
        R"(
module signed_comparison;
  logic signed [3:0] lhs;
  logic signed [3:0] rhs;
  logic result;
  always_comb result = lhs < rhs;
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(signed_comparison.ok());
    const auto elaborated_signed_comparison =
        fsim::elaboration::elaborate(
            signed_comparison.design,
            "sv:work.signed_comparison");
    assert(elaborated_signed_comparison.ok());
    const auto signed_comparison_lhs =
        elaborated_signed_comparison.design->find_signal("lhs");
    const auto signed_comparison_rhs =
        elaborated_signed_comparison.design->find_signal("rhs");
    const auto signed_comparison_result =
        elaborated_signed_comparison.design->find_signal("result");
    assert(
        signed_comparison_lhs && signed_comparison_rhs
        && signed_comparison_result);
    auto signed_comparison_interpreter =
        elaborated_signed_comparison.design->create_interpreter();
    signed_comparison_interpreter->deposit_signal(
        *signed_comparison_lhs,
        fsim::runtime::PackedLogic4::from_msb_string("1111"));
    signed_comparison_interpreter->deposit_signal(
        *signed_comparison_rhs,
        fsim::runtime::PackedLogic4::from_msb_string("0001"));
    (void)signed_comparison_interpreter->run();
    assert(
        signed_comparison_interpreter
            ->signal_value(*signed_comparison_result)
            .to_msb_string()
        == "1");

    const auto systemverilog_power =
        fsim::frontend::parse_text(
            "systemverilog_power.sv",
            R"(
module systemverilog_power #(
  parameter int PARAMETER_POWER = 3 ** 4
);
  logic [7:0] positive;
  logic [7:0] zero_exponent;
  logic [15:0] left_associative;
  logic signed [7:0] negative_exponent;
  logic signed [7:0] minus_one_negative;
  logic signed [7:0] zero_negative;
  logic [7:0] unknown_operand;
  logic [7:0] parameter_power;
  initial begin
    positive = 8'd3 ** 8'd4;
    zero_exponent = 8'd7 ** 8'd0;
    left_associative = 16'd2 ** 16'd3 ** 16'd2;
    negative_exponent =
        $signed(8'hfe) ** $signed(8'hfd);
    minus_one_negative =
        $signed(8'hff) ** $signed(8'hfd);
    zero_negative =
        $signed(8'h00) ** $signed(8'hff);
    unknown_operand = 8'b000000x1 ** 8'd2;
    parameter_power = PARAMETER_POWER;
  end
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(systemverilog_power.ok());
    const auto elaborated_systemverilog_power =
        fsim::elaboration::elaborate(
            systemverilog_power.design,
            "sv:work.systemverilog_power");
    if (!elaborated_systemverilog_power.ok()) {
        for (const auto& diagnostic :
             elaborated_systemverilog_power.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(elaborated_systemverilog_power.ok());
    auto systemverilog_power_interpreter =
        elaborated_systemverilog_power.design
            ->create_interpreter();
    const auto systemverilog_power_result =
        systemverilog_power_interpreter->run();
    assert(
        systemverilog_power_result.status
        == fsim::runtime::RunStatus::completed);
    for (const auto& [name, expected] :
         std::initializer_list<
             std::pair<std::string_view, std::string_view>>{
             {"positive", "01010001"},
             {"zero_exponent", "00000001"},
             {"left_associative", "0000000001000000"},
             {"negative_exponent", "00000000"},
             {"minus_one_negative", "11111111"},
             {"zero_negative", "XXXXXXXX"},
             {"unknown_operand", "XXXXXXXX"},
             {"parameter_power", "01010001"}}) {
        const auto signal =
            elaborated_systemverilog_power.design
                ->find_signal(name);
        assert(signal);
        assert(
            systemverilog_power_interpreter
                ->signal_value(*signal)
                .to_msb_string()
            == expected);
    }

    const auto vhdl_power = fsim::frontend::parse_text(
        "vhdl_power.vhd",
        R"(
entity vhdl_power is
end entity;

architecture rtl of vhdl_power is
  signal positive : unsigned(7 downto 0);
  signal negative_base : signed(7 downto 0);
  signal zero_exponent : unsigned(7 downto 0);
begin
  positive <= "00000011" ** 4;
  negative_base <= "11111110" ** 3;
  zero_exponent <= "00000111" ** 0;
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(vhdl_power.ok());
    const auto elaborated_vhdl_power =
        fsim::elaboration::elaborate(
            vhdl_power.design,
            "vhdl:work.vhdl_power(rtl)");
    if (!elaborated_vhdl_power.ok()) {
        for (const auto& diagnostic :
             elaborated_vhdl_power.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(elaborated_vhdl_power.ok());
    auto vhdl_power_interpreter =
        elaborated_vhdl_power.design->create_interpreter();
    const auto vhdl_power_result =
        vhdl_power_interpreter->run();
    assert(
        vhdl_power_result.status
        == fsim::runtime::RunStatus::completed);
    for (const auto& [name, expected] :
         std::initializer_list<
             std::pair<std::string_view, std::string_view>>{
             {"positive", "01010001"},
             {"negative_base", "11111000"},
             {"zero_exponent", "00000001"}}) {
        const auto signal =
            elaborated_vhdl_power.design->find_signal(name);
        assert(signal);
        assert(
            vhdl_power_interpreter
                ->signal_value(*signal)
                .to_msb_string()
            == expected);
    }

    const auto invalid_vhdl_power =
        fsim::frontend::parse_text(
            "invalid_vhdl_power.vhd",
            R"(
entity invalid_vhdl_power is
end entity;

architecture rtl of invalid_vhdl_power is
  signal result : unsigned(7 downto 0);
begin
  result <= "00000010" ** (-1);
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(invalid_vhdl_power.ok());
    const auto rejected_vhdl_power =
        fsim::elaboration::elaborate(
            invalid_vhdl_power.design,
            "vhdl:work.invalid_vhdl_power(rtl)");
    assert(!rejected_vhdl_power.ok());
    assert(has_diagnostic(
        rejected_vhdl_power, "FSIM-ELAB-091"));

    const auto invalid_vhdl_conditional =
        fsim::frontend::parse_text(
            "invalid_vhdl_conditional.vhd",
            R"(
entity invalid_vhdl_conditional is
end entity;

architecture rtl of invalid_vhdl_conditional is
  signal choose : std_logic;
  signal result : std_logic;
begin
  result <= '1' when choose else '0';
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(invalid_vhdl_conditional.ok());
    const auto rejected_vhdl_conditional =
        fsim::elaboration::elaborate(
            invalid_vhdl_conditional.design,
            "vhdl:work.invalid_vhdl_conditional(rtl)");
    assert(!rejected_vhdl_conditional.ok());
    assert(has_diagnostic(
        rejected_vhdl_conditional, "FSIM-ELAB-092"));

    const auto signedness_casts = fsim::frontend::parse_text(
        "signedness_casts.sv",
        R"(
module signedness_casts;
  logic [3:0] unsigned_value;
  logic signed [3:0] signed_value;
  logic signed_less;
  logic unsigned_less;
  logic [3:0] signed_shift;
  logic [3:0] unsigned_shift;
  logic known_is_unknown;
  logic xz_is_unknown;
  logic [31:0] object_bits;
  logic [31:0] concatenation_bits;
  logic onehot_zero;
  logic onehot_single;
  logic onehot_multiple;
  logic onehot_unknown;
  logic onehot0_zero;
  logic onehot0_single;
  logic onehot0_multiple;
  logic onehot0_unknown;
  logic signed [31:0] countones_zero;
  logic signed [31:0] countones_single;
  logic signed [31:0] countones_multiple;
  logic signed [31:0] countones_unknown;
  logic signed [31:0] countbits_known;
  logic signed [31:0] countbits_unknown;
  logic signed [31:0] countbits_zero_x;
  always_comb begin
    signed_less = $signed(unsigned_value) < signed_value;
    unsigned_less = $unsigned(signed_value) < unsigned_value;
    signed_shift = $signed(unsigned_value) >>> 1;
    unsigned_shift = $unsigned(signed_value) >>> 1;
    known_is_unknown = $isunknown(unsigned_value);
    xz_is_unknown = $isunknown(4'b10xz);
    object_bits = $bits(unsigned_value);
    concatenation_bits = $bits({unsigned_value, signed_value});
    onehot_zero = $onehot(4'b0000);
    onehot_single = $onehot(4'b0010);
    onehot_multiple = $onehot(4'b1010);
    onehot_unknown = $onehot(4'bx001);
    onehot0_zero = $onehot0(4'b0000);
    onehot0_single = $onehot0(4'b0010);
    onehot0_multiple = $onehot0(4'b1010);
    onehot0_unknown = $onehot0(4'bz001);
    countones_zero = $countones(4'b0000);
    countones_single = $countones(4'b0010);
    countones_multiple = $countones(4'b1011);
    countones_unknown = $countones(4'bxz01);
    countbits_known = $countbits(4'b10xz, 1'b0, 1'b1);
    countbits_unknown = $countbits(4'b10xz, 1'bx, 1'bz);
    countbits_zero_x = $countbits(4'b10xz, 1'b0, 1'bx);
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(signedness_casts.ok());
    const auto elaborated_signedness_casts =
        fsim::elaboration::elaborate(
            signedness_casts.design,
            "sv:work.signedness_casts");
    assert(elaborated_signedness_casts.ok());
    const auto unsigned_cast_input =
        elaborated_signedness_casts.design->find_signal(
            "unsigned_value");
    const auto signed_cast_input =
        elaborated_signedness_casts.design->find_signal(
            "signed_value");
    const std::array signedness_cast_outputs{
        elaborated_signedness_casts.design->find_signal(
            "signed_less"),
        elaborated_signedness_casts.design->find_signal(
            "unsigned_less"),
        elaborated_signedness_casts.design->find_signal(
            "signed_shift"),
        elaborated_signedness_casts.design->find_signal(
            "unsigned_shift"),
        elaborated_signedness_casts.design->find_signal(
            "known_is_unknown"),
        elaborated_signedness_casts.design->find_signal(
            "xz_is_unknown"),
        elaborated_signedness_casts.design->find_signal(
            "object_bits"),
        elaborated_signedness_casts.design->find_signal(
            "concatenation_bits"),
        elaborated_signedness_casts.design->find_signal(
            "onehot_zero"),
        elaborated_signedness_casts.design->find_signal(
            "onehot_single"),
        elaborated_signedness_casts.design->find_signal(
            "onehot_multiple"),
        elaborated_signedness_casts.design->find_signal(
            "onehot_unknown"),
        elaborated_signedness_casts.design->find_signal(
            "onehot0_zero"),
        elaborated_signedness_casts.design->find_signal(
            "onehot0_single"),
        elaborated_signedness_casts.design->find_signal(
            "onehot0_multiple"),
        elaborated_signedness_casts.design->find_signal(
            "onehot0_unknown"),
        elaborated_signedness_casts.design->find_signal(
            "countones_zero"),
        elaborated_signedness_casts.design->find_signal(
            "countones_single"),
        elaborated_signedness_casts.design->find_signal(
            "countones_multiple"),
        elaborated_signedness_casts.design->find_signal(
            "countones_unknown"),
        elaborated_signedness_casts.design->find_signal(
            "countbits_known"),
        elaborated_signedness_casts.design->find_signal(
            "countbits_unknown"),
        elaborated_signedness_casts.design->find_signal(
            "countbits_zero_x")};
    assert(unsigned_cast_input && signed_cast_input);
    assert(std::ranges::all_of(
        signedness_cast_outputs,
        [](const auto signal) { return signal.has_value(); }));
    const auto& signedness_operations =
        elaborated_signedness_casts.design->processes().front()
            .operations;
    assert(
        std::count_if(
            signedness_operations.begin(),
            signedness_operations.end(),
            [](const fsim::runtime::simir::Operation& operation) {
              const auto* point =
                  std::get_if<
                      fsim::runtime::simir::DebugPoint>(
                      &operation);
              return point != nullptr
                  && point->kind
                      == fsim::runtime::simir::DebugPointKind::call
                  && point->source.path == "signedness_casts.sv"
                  && point->source.line != 0
                  && point->source.column != 0;
            })
        == 23);
    auto signedness_cast_interpreter =
        elaborated_signedness_casts.design
            ->create_interpreter();
    signedness_cast_interpreter->deposit_signal(
        *unsigned_cast_input,
        fsim::runtime::PackedLogic4::from_msb_string("1111"));
    signedness_cast_interpreter->deposit_signal(
        *signed_cast_input,
        fsim::runtime::PackedLogic4::from_msb_string("0001"));
    (void)signedness_cast_interpreter->run();
    const std::array<std::string_view, 23> expected_signedness_casts{
        "1",
        "1",
        "1111",
        "0000",
        "0",
        "1",
        "00000000000000000000000000000100",
        "00000000000000000000000000001000",
        "0",
        "1",
        "0",
        "1",
        "1",
        "1",
        "0",
        "1",
        "00000000000000000000000000000000",
        "00000000000000000000000000000001",
        "00000000000000000000000000000011",
        "00000000000000000000000000000001",
        "00000000000000000000000000000010",
        "00000000000000000000000000000010",
        "00000000000000000000000000000010"};
    for (std::size_t index = 0;
         index < signedness_cast_outputs.size();
         ++index) {
        assert(
            signedness_cast_interpreter
                ->signal_value(*signedness_cast_outputs[index])
                .to_msb_string()
            == expected_signedness_casts[index]);
    }

    const auto invalid_signedness_cast =
        fsim::frontend::parse_text(
            "invalid_signedness_cast.sv",
            R"(
module invalid_signedness_cast;
  logic result;
  always_comb result = $signed();
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(invalid_signedness_cast.ok());
    const auto rejected_signedness_cast =
        fsim::elaboration::elaborate(
            invalid_signedness_cast.design,
            "sv:work.invalid_signedness_cast");
    assert(!rejected_signedness_cast.ok());
    assert(has_diagnostic(
        rejected_signedness_cast, "FSIM-ELAB-083"));

    const auto invalid_isunknown = fsim::frontend::parse_text(
        "invalid_isunknown.sv",
        R"(
module invalid_isunknown;
  logic result;
  always_comb result = $isunknown();
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(invalid_isunknown.ok());
    const auto rejected_isunknown =
        fsim::elaboration::elaborate(
            invalid_isunknown.design,
            "sv:work.invalid_isunknown");
    assert(!rejected_isunknown.ok());
    assert(has_diagnostic(
        rejected_isunknown, "FSIM-ELAB-084"));

    const auto verilog_isunknown = fsim::frontend::parse_text(
        "verilog_isunknown.v",
        R"(
module verilog_isunknown;
  reg result;
  always @* result = $isunknown(1'bx);
endmodule
)",
        fsim::frontend::Language::Verilog2005);
    assert(verilog_isunknown.ok());
    const auto rejected_verilog_isunknown =
        fsim::elaboration::elaborate(
            verilog_isunknown.design,
            "sv:work.verilog_isunknown");
    assert(!rejected_verilog_isunknown.ok());
    assert(has_diagnostic(
        rejected_verilog_isunknown, "FSIM-ELAB-084"));

    const auto invalid_bits = fsim::frontend::parse_text(
        "invalid_bits.sv",
        R"(
module invalid_bits;
  logic [31:0] result;
  always_comb result = $bits();
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(invalid_bits.ok());
    const auto rejected_bits =
        fsim::elaboration::elaborate(
            invalid_bits.design, "sv:work.invalid_bits");
    assert(!rejected_bits.ok());
    assert(has_diagnostic(
        rejected_bits, "FSIM-ELAB-085"));

    const auto verilog_bits = fsim::frontend::parse_text(
        "verilog_bits.v",
        R"(
module verilog_bits;
  reg [31:0] result;
  always @* result = $bits(8'b0);
endmodule
)",
        fsim::frontend::Language::Verilog2005);
    assert(verilog_bits.ok());
    const auto rejected_verilog_bits =
        fsim::elaboration::elaborate(
            verilog_bits.design, "sv:work.verilog_bits");
    assert(!rejected_verilog_bits.ok());
    assert(has_diagnostic(
        rejected_verilog_bits, "FSIM-ELAB-085"));

    const auto packed_array_queries =
        fsim::frontend::parse_text(
            "packed_array_queries.sv",
            R"(
module packed_array_queries;
  logic [7:4] descending;
  logic [2:5] ascending;
  logic signed [31:0] descending_left;
  logic signed [31:0] descending_right;
  logic signed [31:0] descending_low;
  logic signed [31:0] descending_high;
  logic signed [31:0] descending_size;
  logic signed [31:0] descending_increment;
  logic signed [31:0] ascending_left;
  logic signed [31:0] ascending_right;
  logic signed [31:0] ascending_low;
  logic signed [31:0] ascending_high;
  logic signed [31:0] ascending_size;
  logic signed [31:0] ascending_increment;
  logic signed [31:0] dimensions;
  logic signed [31:0] unpacked_dimensions;
  always_comb begin
    descending_left = $left(descending, 1);
    descending_right = $right(descending);
    descending_low = $low(descending);
    descending_high = $high(descending);
    descending_size = $size(descending);
    descending_increment = $increment(descending);
    ascending_left = $left(ascending);
    ascending_right = $right(ascending);
    ascending_low = $low(ascending);
    ascending_high = $high(ascending);
    ascending_size = $size(ascending, 1);
    ascending_increment = $increment(ascending);
    dimensions = $dimensions(descending);
    unpacked_dimensions = $unpacked_dimensions(ascending);
  end
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(packed_array_queries.ok());
    const auto elaborated_packed_array_queries =
        fsim::elaboration::elaborate(
            packed_array_queries.design,
            "sv:work.packed_array_queries");
    assert(elaborated_packed_array_queries.ok());
    const std::array packed_query_names{
        "descending_left",
        "descending_right",
        "descending_low",
        "descending_high",
        "descending_size",
        "descending_increment",
        "ascending_left",
        "ascending_right",
        "ascending_low",
        "ascending_high",
        "ascending_size",
        "ascending_increment",
        "dimensions",
        "unpacked_dimensions"};
    std::array<
        std::optional<fsim::runtime::simir::SignalId>,
        14>
        packed_query_outputs;
    for (std::size_t index = 0;
         index < packed_query_names.size();
         ++index) {
        packed_query_outputs[index] =
            elaborated_packed_array_queries.design
                ->find_signal(packed_query_names[index]);
        assert(packed_query_outputs[index]);
    }
    auto packed_query_interpreter =
        elaborated_packed_array_queries.design
            ->create_interpreter();
    (void)packed_query_interpreter->run();
    const std::array<std::uint32_t, 14> expected_packed_queries{
        7,
        4,
        4,
        7,
        4,
        1,
        2,
        5,
        2,
        5,
        4,
        std::numeric_limits<std::uint32_t>::max(),
        1,
        0};
    for (std::size_t index = 0;
         index < packed_query_outputs.size();
         ++index) {
        assert(
            packed_query_interpreter
                ->signal_value(*packed_query_outputs[index])
                .low_word()
                .aval
            == expected_packed_queries[index]);
    }

    const auto invalid_packed_query =
        fsim::frontend::parse_text(
            "invalid_packed_query.sv",
            R"(
module invalid_packed_query;
  logic [3:0] value;
  logic signed [31:0] result;
  always_comb result = $left(value, 2);
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(invalid_packed_query.ok());
    const auto rejected_packed_query =
        fsim::elaboration::elaborate(
            invalid_packed_query.design,
            "sv:work.invalid_packed_query");
    assert(!rejected_packed_query.ok());
    assert(has_diagnostic(
        rejected_packed_query, "FSIM-ELAB-086"));

    const auto invalid_dimensions =
        fsim::frontend::parse_text(
            "invalid_dimensions.sv",
            R"(
module invalid_dimensions;
  logic signed [31:0] result;
  always_comb result = $dimensions();
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(invalid_dimensions.ok());
    const auto rejected_dimensions =
        fsim::elaboration::elaborate(
            invalid_dimensions.design,
            "sv:work.invalid_dimensions");
    assert(!rejected_dimensions.ok());
    assert(has_diagnostic(
        rejected_dimensions, "FSIM-ELAB-090"));

    const auto invalid_onehot = fsim::frontend::parse_text(
        "invalid_onehot.sv",
        R"(
module invalid_onehot;
  logic result;
  always_comb result = $onehot();
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(invalid_onehot.ok());
    const auto rejected_onehot =
        fsim::elaboration::elaborate(
            invalid_onehot.design, "sv:work.invalid_onehot");
    assert(!rejected_onehot.ok());
    assert(has_diagnostic(
        rejected_onehot, "FSIM-ELAB-087"));

    const auto invalid_countones = fsim::frontend::parse_text(
        "invalid_countones.sv",
        R"(
module invalid_countones;
  logic signed [31:0] result;
  always_comb result = $countones();
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(invalid_countones.ok());
    const auto rejected_countones =
        fsim::elaboration::elaborate(
            invalid_countones.design,
            "sv:work.invalid_countones");
    assert(!rejected_countones.ok());
    assert(has_diagnostic(
        rejected_countones, "FSIM-ELAB-088"));

    const auto invalid_countbits = fsim::frontend::parse_text(
        "invalid_countbits.sv",
        R"(
module invalid_countbits;
  logic [3:0] value;
  logic control;
  logic signed [31:0] result;
  always_comb result = $countbits(value, control);
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(invalid_countbits.ok());
    const auto rejected_countbits =
        fsim::elaboration::elaborate(
            invalid_countbits.design,
            "sv:work.invalid_countbits");
    assert(!rejected_countbits.ok());
    assert(has_diagnostic(
        rejected_countbits, "FSIM-ELAB-089"));

    const auto logical_process = fsim::frontend::parse_text(
        "logical_process.sv",
        R"(
module logical_process;
  logic [3:0] lhs;
  logic [1:0] rhs;
  logic conjunction;
  logic disjunction;
  always_comb begin
    conjunction = lhs && rhs;
    disjunction = lhs || rhs;
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(logical_process.ok());
    const auto elaborated_logical =
        fsim::elaboration::elaborate(
            logical_process.design, "sv:work.logical_process");
    assert(elaborated_logical.ok());
    const auto logical_lhs =
        elaborated_logical.design->find_signal("lhs");
    const auto logical_rhs =
        elaborated_logical.design->find_signal("rhs");
    const auto logical_and =
        elaborated_logical.design->find_signal("conjunction");
    const auto logical_or =
        elaborated_logical.design->find_signal("disjunction");
    assert(logical_lhs && logical_rhs && logical_and && logical_or);
    auto logical_interpreter =
        elaborated_logical.design->create_interpreter();
    const auto run_logical =
        [&](const std::string_view lhs,
            const std::string_view rhs,
            const std::string_view expected_and,
            const std::string_view expected_or) {
          logical_interpreter->deposit_signal(
              *logical_lhs,
              fsim::runtime::PackedLogic4::from_msb_string(lhs));
          logical_interpreter->deposit_signal(
              *logical_rhs,
              fsim::runtime::PackedLogic4::from_msb_string(rhs));
          (void)logical_interpreter->run();
          assert(
              logical_interpreter
                  ->signal_value(*logical_and)
                  .to_msb_string()
              == expected_and);
          assert(
              logical_interpreter
                  ->signal_value(*logical_or)
                  .to_msb_string()
              == expected_or);
        };
    run_logical("0000", "X1", "0", "1");
    run_logical("00X0", "00", "0", "X");
    run_logical("00X0", "01", "X", "1");
    run_logical("0010", "ZZ", "X", "1");
    run_logical("0010", "01", "1", "1");

    const auto reduction_shift_process =
        fsim::frontend::parse_text(
            "reduction_shift_process.sv",
            R"(
module reduction_shift_process;
  logic [3:0] value;
  logic [2:0] amount;
  logic reduced_and;
  logic reduced_or;
  logic reduced_xor;
  logic reduced_nand;
  logic reduced_nor;
  logic reduced_xnor;
  logic reduced_xnor_alias;
  logic [3:0] xnor_value;
  logic [3:0] xnor_value_alias;
  logic [3:0] shifted_left;
  logic [3:0] shifted_right;
  always_comb begin
    reduced_and = &value;
    reduced_or = |value;
    reduced_xor = ^value;
    reduced_nand = ~&value;
    reduced_nor = ~|value;
    reduced_xnor = ~^value;
    reduced_xnor_alias = ^~value;
    xnor_value = value ~^ 4'b1010;
    xnor_value_alias = value ^~ 4'b1010;
    shifted_left = value << amount;
    shifted_right = value >> amount;
  end
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(reduction_shift_process.ok());
    const auto elaborated_reduction_shift =
        fsim::elaboration::elaborate(
            reduction_shift_process.design,
            "sv:work.reduction_shift_process");
    assert(elaborated_reduction_shift.ok());
    const auto reduction_value =
        elaborated_reduction_shift.design->find_signal("value");
    const auto shift_amount =
        elaborated_reduction_shift.design->find_signal("amount");
    const auto reduced_and =
        elaborated_reduction_shift.design->find_signal("reduced_and");
    const auto reduced_or =
        elaborated_reduction_shift.design->find_signal("reduced_or");
    const auto reduced_xor =
        elaborated_reduction_shift.design->find_signal("reduced_xor");
    const auto reduced_nand =
        elaborated_reduction_shift.design->find_signal("reduced_nand");
    const auto reduced_nor =
        elaborated_reduction_shift.design->find_signal("reduced_nor");
    const auto reduced_xnor =
        elaborated_reduction_shift.design->find_signal("reduced_xnor");
    const auto reduced_xnor_alias =
        elaborated_reduction_shift.design->find_signal(
            "reduced_xnor_alias");
    const auto xnor_value =
        elaborated_reduction_shift.design->find_signal("xnor_value");
    const auto xnor_value_alias =
        elaborated_reduction_shift.design->find_signal(
            "xnor_value_alias");
    const auto shifted_left =
        elaborated_reduction_shift.design->find_signal("shifted_left");
    const auto shifted_right =
        elaborated_reduction_shift.design->find_signal("shifted_right");
    assert(
        reduction_value && shift_amount && reduced_and
        && reduced_or && reduced_xor && reduced_nand
        && reduced_nor && reduced_xnor
        && reduced_xnor_alias && xnor_value
        && xnor_value_alias && shifted_left && shifted_right);
    auto reduction_shift_interpreter =
        elaborated_reduction_shift.design->create_interpreter();
    const auto run_reduction_shift =
        [&](const std::string_view value,
            const std::string_view amount,
            const std::string_view expected_and,
            const std::string_view expected_or,
            const std::string_view expected_xor,
            const std::string_view expected_nand,
            const std::string_view expected_nor,
            const std::string_view expected_xnor,
            const std::string_view expected_xnor_value,
            const std::string_view expected_left,
            const std::string_view expected_right) {
          reduction_shift_interpreter->deposit_signal(
              *reduction_value,
              fsim::runtime::PackedLogic4::from_msb_string(value));
          reduction_shift_interpreter->deposit_signal(
              *shift_amount,
              fsim::runtime::PackedLogic4::from_msb_string(amount));
          (void)reduction_shift_interpreter->run();
          assert(
              reduction_shift_interpreter
                  ->signal_value(*reduced_and)
                  .to_msb_string()
              == expected_and);
          assert(
              reduction_shift_interpreter
                  ->signal_value(*reduced_or)
                  .to_msb_string()
              == expected_or);
          assert(
              reduction_shift_interpreter
                  ->signal_value(*reduced_xor)
                  .to_msb_string()
              == expected_xor);
          assert(
              reduction_shift_interpreter
                  ->signal_value(*reduced_nand)
                  .to_msb_string()
              == expected_nand);
          assert(
              reduction_shift_interpreter
                  ->signal_value(*reduced_nor)
                  .to_msb_string()
              == expected_nor);
          assert(
              reduction_shift_interpreter
                  ->signal_value(*reduced_xnor)
                  .to_msb_string()
              == expected_xnor);
          assert(
              reduction_shift_interpreter
                  ->signal_value(*reduced_xnor_alias)
                  .to_msb_string()
              == expected_xnor);
          assert(
              reduction_shift_interpreter
                  ->signal_value(*xnor_value)
                  .to_msb_string()
              == expected_xnor_value);
          assert(
              reduction_shift_interpreter
                  ->signal_value(*xnor_value_alias)
                  .to_msb_string()
              == expected_xnor_value);
          assert(
              reduction_shift_interpreter
                  ->signal_value(*shifted_left)
                  .to_msb_string()
              == expected_left);
          assert(
              reduction_shift_interpreter
                  ->signal_value(*shifted_right)
                  .to_msb_string()
              == expected_right);
        };
    run_reduction_shift(
        "1111", "001", "1", "1", "0", "0", "0", "1",
        "1010", "1110", "0111");
    run_reduction_shift(
        "1011", "000", "0", "1", "1", "1", "0", "0",
        "1110", "1011", "1011");
    run_reduction_shift(
        "10X1", "001", "0", "1", "X", "1", "0", "X",
        "11X0", "0X10", "010X");
    run_reduction_shift(
        "11X1", "011", "X", "1", "X", "X", "0", "X",
        "10X0", "1000", "0001");
    run_reduction_shift(
        "00X0", "0X1", "0", "X", "X", "1", "X", "X",
        "01X1", "XXXX", "XXXX");
    run_reduction_shift(
        "Z001", "100", "0", "1", "X", "1", "0", "X",
        "X100", "0000", "0000");

    const auto arithmetic_process =
        fsim::frontend::parse_text(
            "arithmetic_process.sv",
            R"(
module arithmetic_process;
  logic [7:0] lhs;
  logic [7:0] rhs;
  logic [7:0] difference;
  logic [7:0] product;
  logic [7:0] quotient;
  logic [7:0] remainder;
  logic [7:0] positive;
  logic [7:0] negative;
  always_comb begin
    difference = lhs - rhs;
    product = lhs * rhs;
    quotient = lhs / rhs;
    remainder = lhs % rhs;
    positive = +lhs;
    negative = -lhs;
  end
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(arithmetic_process.ok());
    const auto elaborated_arithmetic =
        fsim::elaboration::elaborate(
            arithmetic_process.design,
            "sv:work.arithmetic_process");
    assert(elaborated_arithmetic.ok());
    const auto arithmetic_lhs =
        elaborated_arithmetic.design->find_signal("lhs");
    const auto arithmetic_rhs =
        elaborated_arithmetic.design->find_signal("rhs");
    const std::array arithmetic_outputs{
        elaborated_arithmetic.design->find_signal("difference"),
        elaborated_arithmetic.design->find_signal("product"),
        elaborated_arithmetic.design->find_signal("quotient"),
        elaborated_arithmetic.design->find_signal("remainder"),
        elaborated_arithmetic.design->find_signal("positive"),
        elaborated_arithmetic.design->find_signal("negative")};
    assert(arithmetic_lhs && arithmetic_rhs);
    assert(std::ranges::all_of(
        arithmetic_outputs,
        [](const auto& signal) {
          return signal.has_value();
        }));
    auto arithmetic_interpreter =
        elaborated_arithmetic.design->create_interpreter();
    const auto run_arithmetic =
        [&](const std::string_view lhs,
            const std::string_view rhs,
            const std::array<std::string_view, 6>& expected) {
          arithmetic_interpreter->deposit_signal(
              *arithmetic_lhs,
              fsim::runtime::PackedLogic4::from_msb_string(lhs));
          arithmetic_interpreter->deposit_signal(
              *arithmetic_rhs,
              fsim::runtime::PackedLogic4::from_msb_string(rhs));
          (void)arithmetic_interpreter->run();
          for (std::size_t index = 0;
               index < arithmetic_outputs.size(); ++index) {
            assert(
                arithmetic_interpreter
                    ->signal_value(*arithmetic_outputs[index])
                    .to_msb_string()
                == expected[index]);
          }
        };
    run_arithmetic(
        "11001000",
        "00000111",
        {"11000001", "01111000", "00011100",
         "00000100", "11001000", "00111000"});
    run_arithmetic(
        "10X01000",
        "00000111",
        {"XXXXXXXX", "XXXXXXXX", "XXXXXXXX",
         "XXXXXXXX", "10X01000", "XXXXXXXX"});
    run_arithmetic(
        "11001000",
        "00000000",
        {"11001000", "00000000", "XXXXXXXX",
         "XXXXXXXX", "11001000", "00111000"});

    const auto signed_arithmetic = fsim::frontend::parse_text(
        "signed_arithmetic.sv",
        R"(
module signed_arithmetic;
  logic signed [7:0] lhs;
  logic signed [7:0] rhs;
  logic [7:0] unsigned_rhs;
  logic signed [7:0] sum;
  logic signed [7:0] difference;
  logic signed [7:0] product;
  logic signed [7:0] quotient;
  logic signed [7:0] remainder;
  logic less;
  logic mixed_less;
  always_comb begin
    sum = lhs + rhs;
    difference = lhs - rhs;
    product = lhs * rhs;
    quotient = lhs / rhs;
    remainder = lhs % rhs;
    less = lhs < rhs;
    mixed_less = lhs < unsigned_rhs;
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(signed_arithmetic.ok());
    const auto elaborated_signed_arithmetic =
        fsim::elaboration::elaborate(
            signed_arithmetic.design,
            "sv:work.signed_arithmetic");
    assert(elaborated_signed_arithmetic.ok());
    const auto signed_lhs =
        elaborated_signed_arithmetic.design->find_signal("lhs");
    const auto signed_rhs =
        elaborated_signed_arithmetic.design->find_signal("rhs");
    const auto unsigned_rhs =
        elaborated_signed_arithmetic.design->find_signal(
            "unsigned_rhs");
    const std::array signed_outputs{
        elaborated_signed_arithmetic.design->find_signal("sum"),
        elaborated_signed_arithmetic.design->find_signal(
            "difference"),
        elaborated_signed_arithmetic.design->find_signal("product"),
        elaborated_signed_arithmetic.design->find_signal(
            "quotient"),
        elaborated_signed_arithmetic.design->find_signal(
            "remainder"),
        elaborated_signed_arithmetic.design->find_signal("less"),
        elaborated_signed_arithmetic.design->find_signal(
            "mixed_less")};
    assert(signed_lhs && signed_rhs && unsigned_rhs);
    assert(std::ranges::all_of(
        signed_outputs,
        [](const auto& signal) {
          return signal.has_value();
        }));
    auto signed_interpreter =
        elaborated_signed_arithmetic.design->create_interpreter();
    const auto run_signed =
        [&](const std::string_view lhs,
            const std::string_view rhs,
            const std::string_view unsigned_value,
            const std::array<std::string_view, 7>& expected) {
          signed_interpreter->deposit_signal(
              *signed_lhs,
              fsim::runtime::PackedLogic4::from_msb_string(lhs));
          signed_interpreter->deposit_signal(
              *signed_rhs,
              fsim::runtime::PackedLogic4::from_msb_string(rhs));
          signed_interpreter->deposit_signal(
              *unsigned_rhs,
              fsim::runtime::PackedLogic4::from_msb_string(
                  unsigned_value));
          (void)signed_interpreter->run();
          for (std::size_t index = 0;
               index < signed_outputs.size(); ++index) {
            assert(
                signed_interpreter
                    ->signal_value(*signed_outputs[index])
                    .to_msb_string()
                == expected[index]);
          }
        };
    run_signed(
        "11111011",
        "00000011",
        "00000001",
        {"11111110", "11111000", "11110001",
         "11111111", "11111110", "1", "0"});
    run_signed(
        "00000101",
        "11111101",
        "11111111",
        {"00000010", "00001000", "11110001",
         "11111111", "00000010", "0", "1"});

    const auto vhdl_signed_arithmetic =
        fsim::frontend::parse_text(
            "vhdl_signed_arithmetic.vhd",
            R"(
entity vhdl_signed_arithmetic is
  port (
    lhs : in signed(7 downto 0);
    rhs : in signed(7 downto 0);
    sum : out signed(7 downto 0);
    difference : out signed(7 downto 0);
    product : out signed(7 downto 0);
    quotient : out signed(7 downto 0);
    remainder : out signed(7 downto 0);
    modulo : out signed(7 downto 0);
    less : out std_logic;
    shifted_left : out signed(7 downto 0);
    shifted_right : out signed(7 downto 0);
    shifted_arithmetic : out signed(7 downto 0);
    shifted_arithmetic_left : out signed(7 downto 0);
    rotated_left : out signed(7 downto 0);
    rotated_right : out signed(7 downto 0);
    reversed_logical_left : out signed(7 downto 0);
    reversed_arithmetic_right : out signed(7 downto 0);
    reversed_rotate_left : out signed(7 downto 0);
    absolute : out signed(7 downto 0)
  );
end entity;

architecture rtl of vhdl_signed_arithmetic is
begin
  calculate: process(lhs, rhs)
  begin
    sum <= lhs + rhs;
    difference <= lhs - rhs;
    product <= lhs * rhs;
    quotient <= lhs / rhs;
    remainder <= lhs rem rhs;
    modulo <= lhs mod rhs;
    less <= lhs < rhs;
    shifted_left <= lhs sll 1;
    shifted_right <= lhs srl 1;
    shifted_arithmetic <= lhs sra 1;
    shifted_arithmetic_left <= lhs sla 1;
    rotated_left <= lhs rol 1;
    rotated_right <= lhs ror 1;
    reversed_logical_left <= lhs sll -1;
    reversed_arithmetic_right <= lhs sra -1;
    reversed_rotate_left <= lhs rol -1;
    absolute <= abs lhs;
  end process;
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(vhdl_signed_arithmetic.ok());
    const auto elaborated_vhdl_signed_arithmetic =
        fsim::elaboration::elaborate(
            vhdl_signed_arithmetic.design,
            "vhdl:work.vhdl_signed_arithmetic(rtl)");
    if (!elaborated_vhdl_signed_arithmetic.ok()) {
      for (const auto& diagnostic :
           elaborated_vhdl_signed_arithmetic.diagnostics) {
        std::cerr << diagnostic.code << ": "
                  << diagnostic.message << '\n';
      }
    }
    assert(elaborated_vhdl_signed_arithmetic.ok());
    const auto vhdl_signed_lhs =
        elaborated_vhdl_signed_arithmetic.design->find_signal(
            "lhs");
    const auto vhdl_signed_rhs =
        elaborated_vhdl_signed_arithmetic.design->find_signal(
            "rhs");
    const std::array vhdl_signed_outputs{
        elaborated_vhdl_signed_arithmetic.design->find_signal(
            "sum"),
        elaborated_vhdl_signed_arithmetic.design->find_signal(
            "difference"),
        elaborated_vhdl_signed_arithmetic.design->find_signal(
            "product"),
        elaborated_vhdl_signed_arithmetic.design->find_signal(
            "quotient"),
        elaborated_vhdl_signed_arithmetic.design->find_signal(
            "remainder"),
        elaborated_vhdl_signed_arithmetic.design->find_signal(
            "modulo"),
        elaborated_vhdl_signed_arithmetic.design->find_signal(
            "less"),
        elaborated_vhdl_signed_arithmetic.design->find_signal(
            "shifted_left"),
        elaborated_vhdl_signed_arithmetic.design->find_signal(
            "shifted_right"),
        elaborated_vhdl_signed_arithmetic.design->find_signal(
            "shifted_arithmetic"),
        elaborated_vhdl_signed_arithmetic.design->find_signal(
            "shifted_arithmetic_left"),
        elaborated_vhdl_signed_arithmetic.design->find_signal(
            "rotated_left"),
        elaborated_vhdl_signed_arithmetic.design->find_signal(
            "rotated_right"),
        elaborated_vhdl_signed_arithmetic.design->find_signal(
            "reversed_logical_left"),
        elaborated_vhdl_signed_arithmetic.design->find_signal(
            "reversed_arithmetic_right"),
        elaborated_vhdl_signed_arithmetic.design->find_signal(
            "reversed_rotate_left"),
        elaborated_vhdl_signed_arithmetic.design->find_signal(
            "absolute")};
    assert(vhdl_signed_lhs && vhdl_signed_rhs);
    assert(std::ranges::all_of(
        vhdl_signed_outputs,
        [](const auto& signal) {
          return signal.has_value();
        }));
    auto vhdl_signed_interpreter =
        elaborated_vhdl_signed_arithmetic.design
            ->create_interpreter();
    vhdl_signed_interpreter->deposit_signal(
        *vhdl_signed_lhs,
        fsim::runtime::PackedLogic4::from_msb_string(
            "11111011"));
    vhdl_signed_interpreter->deposit_signal(
        *vhdl_signed_rhs,
        fsim::runtime::PackedLogic4::from_msb_string(
            "00000011"));
    (void)vhdl_signed_interpreter->run();
    const std::array<std::string_view, 17>
        expected_vhdl_signed{
            "11111110",
            "11111000",
            "11110001",
            "11111111",
            "11111110",
            "00000001",
            "1",
            "11110110",
            "01111101",
            "11111101",
            "11110111",
            "11110111",
            "11111101",
            "01111101",
            "11110111",
            "11111101",
            "00000101"};
    for (std::size_t index = 0;
         index < vhdl_signed_outputs.size(); ++index) {
      assert(
          vhdl_signed_interpreter
              ->signal_value(*vhdl_signed_outputs[index])
              .to_msb_string()
          == expected_vhdl_signed[index]);
    }
    vhdl_signed_interpreter->deposit_signal(
        *vhdl_signed_lhs,
        fsim::runtime::PackedLogic4::from_msb_string(
            "10X01000"));
    (void)vhdl_signed_interpreter->run();
    const std::array<std::string_view, 9>
        expected_unknown_shifts{
            "0X010000",
            "010X0100",
            "110X0100",
            "0X010000",
            "0X010001",
            "010X0100",
            "010X0100",
            "0X010000",
            "010X0100"};
    for (std::size_t index = 0;
         index < expected_unknown_shifts.size(); ++index) {
      assert(
          vhdl_signed_interpreter
              ->signal_value(*vhdl_signed_outputs[index + 7])
              .to_msb_string()
          == expected_unknown_shifts[index]);
    }
    assert(
        vhdl_signed_interpreter
            ->signal_value(*vhdl_signed_outputs.back())
            .to_msb_string()
        == "XXXXXXXX");
    vhdl_signed_interpreter->deposit_signal(
        *vhdl_signed_lhs,
        fsim::runtime::PackedLogic4::from_msb_string(
            "10000000"));
    (void)vhdl_signed_interpreter->run();
    assert(
        vhdl_signed_interpreter
            ->signal_value(*vhdl_signed_outputs.back())
            .to_msb_string()
        == "10000000");
    vhdl_signed_interpreter->deposit_signal(
        *vhdl_signed_lhs,
        fsim::runtime::PackedLogic4::from_msb_string(
            "00000101"));
    (void)vhdl_signed_interpreter->run();
    assert(
        vhdl_signed_interpreter
            ->signal_value(*vhdl_signed_outputs.back())
            .to_msb_string()
        == "00000101");

    const auto dynamic_vhdl_shift =
        fsim::frontend::parse_text(
            "dynamic_vhdl_shift.vhd",
            R"(
entity dynamic_vhdl_shift is
  port (
    value : in std_logic_vector(7 downto 0);
    count : in integer;
    logical_left : out std_logic_vector(7 downto 0);
    logical_right : out std_logic_vector(7 downto 0);
    arithmetic_left : out std_logic_vector(7 downto 0);
    arithmetic_right : out std_logic_vector(7 downto 0);
    rotate_left : out std_logic_vector(7 downto 0);
    rotate_right : out std_logic_vector(7 downto 0);
    integer_result : out integer
  );
end entity;

architecture rtl of dynamic_vhdl_shift is
  signal natural_value : natural;
  signal positive_value : positive;
  signal constrained_value : integer range -3 to 4;
  signal reverse_value : integer range 3 downto -2;
begin
  calculate: process(value, count)
    variable adjusted : integer := -1;
    variable bounded : integer range -2 to 2 := -2;
  begin
    adjusted := count + 1;
    bounded := adjusted mod 3;
    integer_result <= abs adjusted;
    logical_left <= value sll count;
    logical_right <= value srl count;
    arithmetic_left <= value sla count;
    arithmetic_right <= value sra count;
    rotate_left <= value rol count;
    rotate_right <= value ror count;
  end process;
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(dynamic_vhdl_shift.ok());
    const auto elaborated_dynamic_vhdl_shift =
        fsim::elaboration::elaborate(
            dynamic_vhdl_shift.design,
            "vhdl:work.dynamic_vhdl_shift(rtl)");
    if (!elaborated_dynamic_vhdl_shift.ok()) {
      for (const auto& diagnostic :
           elaborated_dynamic_vhdl_shift.diagnostics) {
        std::cerr << diagnostic.code << ": "
                  << diagnostic.message << '\n';
      }
    }
    assert(elaborated_dynamic_vhdl_shift.ok());
    const auto& dynamic_design =
        *elaborated_dynamic_vhdl_shift.design;
    const auto dynamic_count =
        dynamic_design.find_signal("count");
    const auto dynamic_value =
        dynamic_design.find_signal("value");
    const auto natural_value =
        dynamic_design.find_signal("natural_value");
    const auto positive_value =
        dynamic_design.find_signal("positive_value");
    const auto constrained_value =
        dynamic_design.find_signal("constrained_value");
    const auto reverse_value =
        dynamic_design.find_signal("reverse_value");
    const std::array dynamic_outputs{
        dynamic_design.find_signal("logical_left"),
        dynamic_design.find_signal("logical_right"),
        dynamic_design.find_signal("arithmetic_left"),
        dynamic_design.find_signal("arithmetic_right"),
        dynamic_design.find_signal("rotate_left"),
        dynamic_design.find_signal("rotate_right"),
        dynamic_design.find_signal("integer_result")};
    assert(
        dynamic_count && dynamic_value && natural_value
        && positive_value && constrained_value && reverse_value);
    assert(std::ranges::all_of(
        dynamic_outputs,
        [](const auto& signal) {
          return signal.has_value();
        }));
    const auto& count_info =
        dynamic_design.signals().at(*dynamic_count);
    assert(count_info.width == 32);
    assert(
        count_info.source_domain
        == fsim::frontend::ValueDomain::Integer);
    assert(count_info.is_signed);
    const auto& natural_info =
        dynamic_design.signals().at(*natural_value);
    const auto& positive_info =
        dynamic_design.signals().at(*positive_value);
    const auto& constrained_info =
        dynamic_design.signals().at(*constrained_value);
    const auto& reverse_info =
        dynamic_design.signals().at(*reverse_value);
    assert(
        natural_info.integer_range
        && natural_info.integer_range->left == 0
        && positive_info.integer_range
        && positive_info.integer_range->left == 1
        && constrained_info.integer_range
        && constrained_info.integer_range->left == -3
        && constrained_info.integer_range->right == 4
        && reverse_info.integer_range
        && reverse_info.integer_range->left == 3
        && reverse_info.integer_range->right == -2
        && reverse_info.integer_range->descending);
    assert(dynamic_design.processes().size() == 1);
    const auto signed_shift_count = std::ranges::count_if(
        dynamic_design.processes().front().operations,
        [](const auto& operation) {
          const auto* shift =
              std::get_if<fsim::runtime::simir::Shift>(
                  &operation);
          return shift != nullptr && shift->signed_amount;
        });
    assert(signed_shift_count == 6);
    assert(
        std::ranges::count_if(
            dynamic_design.processes().front().operations,
            [](const auto& operation) {
              return std::holds_alternative<
                  fsim::runtime::simir::IntegerCheck>(
                  operation);
            })
        >= 4);
    assert(std::ranges::any_of(
        dynamic_design.processes().front().operations,
        [](const auto& operation) {
          const auto* binary =
              std::get_if<
                  fsim::runtime::simir::IntegerBinary>(
                  &operation);
          return binary != nullptr
              && binary->operation
                  == fsim::runtime::simir::
                      IntegerBinaryOperator::add;
        }));
    assert(std::ranges::any_of(
        dynamic_design.processes().front().operations,
        [](const auto& operation) {
          return std::holds_alternative<
              fsim::runtime::simir::IntegerUnary>(
              operation);
        }));
    assert(
        dynamic_design.processes().front().debug_locals.size() == 2
        && dynamic_design.processes().front()
               .debug_locals[1].integer_lower
        && *dynamic_design.processes().front()
                .debug_locals[1].integer_lower
            == -2
        && dynamic_design.processes().front()
               .debug_locals[1].integer_upper
        && *dynamic_design.processes().front()
                .debug_locals[1].integer_upper
            == 2);

    auto dynamic_interpreter =
        dynamic_design.create_interpreter();
    assert(
        dynamic_interpreter
            ->signal_value(*natural_value)
            .to_msb_string()
        == "00000000000000000000000000000000");
    assert(
        dynamic_interpreter
            ->signal_value(*positive_value)
            .to_msb_string()
        == "00000000000000000000000000000001");
    assert(
        dynamic_interpreter
            ->signal_value(*constrained_value)
            .to_msb_string()
        == "11111111111111111111111111111101");
    assert(
        dynamic_interpreter
            ->signal_value(*reverse_value)
            .to_msb_string()
        == "00000000000000000000000000000011");
    assert(
        dynamic_interpreter
            ->signal_value(*dynamic_count)
            .to_msb_string()
        == "10000000000000000000000000000000");
    dynamic_interpreter->deposit_signal(
        *dynamic_value,
        fsim::runtime::PackedLogic4::from_msb_string(
            "10X0000Z"));
    dynamic_interpreter->deposit_signal(
        *dynamic_count,
        fsim::runtime::PackedLogic4::from_msb_string(
            "11111111111111111111111111111111"));
    (void)dynamic_interpreter->run();
    const std::array<std::string_view, 7>
        negative_expected{
            "010X0000",
            "0X0000Z0",
            "110X0000",
            "0X0000ZZ",
            "Z10X0000",
            "0X0000Z1",
            "00000000000000000000000000000000"};
    for (std::size_t index = 0;
         index < dynamic_outputs.size(); ++index) {
      assert(
          dynamic_interpreter
              ->signal_value(*dynamic_outputs[index])
              .to_msb_string()
          == negative_expected[index]);
    }

    const auto vhdl_subtype_source =
        fsim::frontend::parse_text(
            "vhdl_subtype_execution.vhd",
            R"(
package Subtype_Types is
  subtype Count_Base_T is natural range 0 to 15;
  subtype Count_T is Count_Base_T range 2 to 9;
  constant Default_Count : Count_T := 5;
  type Packet_T is record
    Data : std_logic_vector(3 downto 0);
    Valid : boolean;
  end record Packet_T;
  subtype Packet_Alias_T is Packet_T;
end package;

use work.subtype_types.all;
entity subtype_execution is
  generic (
    Width : natural := 4;
    Initial_Count : Count_T := Default_Count
  );
  subtype Entity_Word_T is std_logic_vector(Width - 1 downto 0);
end entity;

use work.subtype_types.all;
architecture rtl of subtype_execution is
  subtype Local_Count_T is Count_T range 4 to 7;
  subtype Local_Packet_T is Packet_Alias_T;
  signal source : Entity_Word_T;
  signal result : Entity_Word_T;
  signal count : Local_Count_T;
  signal count_result : Count_T;
  signal packet : Local_Packet_T;
begin
  drive : process
    variable local_count : Local_Count_T := Initial_Count;
    variable local_packet : Local_Packet_T :=
      (Valid => true, Data => "ULH-");
  begin
    source <= "10Z-";
    count <= local_count;
    packet <= local_packet;
    wait;
  end process;

  result <= source;
  count_result <= count;
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(vhdl_subtype_source.ok());
    const auto vhdl_subtype_design =
        fsim::elaboration::elaborate(
            vhdl_subtype_source.design,
            "vhdl:work.subtype_execution(rtl)");
    if (!vhdl_subtype_design.ok()) {
      for (const auto& diagnostic :
           vhdl_subtype_design.diagnostics) {
        std::cerr << diagnostic.code << ": "
                  << diagnostic.message << " at "
                  << diagnostic.span.begin.line << ":"
                  << diagnostic.span.begin.column << '\n';
      }
    }
    assert(vhdl_subtype_design.ok());
    const auto subtype_source_signal =
        vhdl_subtype_design.design->find_signal("source");
    const auto subtype_result_signal =
        vhdl_subtype_design.design->find_signal("result");
    const auto subtype_count_signal =
        vhdl_subtype_design.design->find_signal("count");
    const auto subtype_count_result_signal =
        vhdl_subtype_design.design->find_signal(
            "count_result");
    const auto subtype_packet_signal =
        vhdl_subtype_design.design->find_signal("packet");
    assert(
        subtype_source_signal && subtype_result_signal
        && subtype_count_signal
        && subtype_count_result_signal
        && subtype_packet_signal);
    const auto& subtype_source_info =
        vhdl_subtype_design.design->signals().at(
            *subtype_source_signal);
    const auto& subtype_count_info =
        vhdl_subtype_design.design->signals().at(
            *subtype_count_signal);
    const auto& subtype_count_result_info =
        vhdl_subtype_design.design->signals().at(
            *subtype_count_result_signal);
    const auto& subtype_packet_info =
        vhdl_subtype_design.design->signals().at(
            *subtype_packet_signal);
    assert(
        subtype_source_info.width == 4
        && subtype_source_info.source_domain
            == fsim::frontend::ValueDomain::Logic9
        && subtype_count_info.width == 32
        && subtype_count_info.integer_range
        && subtype_count_info.integer_range->left == 4
        && subtype_count_info.integer_range->right == 7
        && subtype_count_result_info.integer_range
        && subtype_count_result_info.integer_range->left == 2
        && subtype_count_result_info.integer_range->right == 9
        && subtype_packet_info.width == 5
        && subtype_packet_info.packed_members.size() == 2);
    assert(
        std::ranges::any_of(
            vhdl_subtype_design.design->processes(),
            [](const auto& process) {
                return std::ranges::any_of(
                    process.operations,
                    [](const auto& operation) {
                        return std::holds_alternative<
                            fsim::runtime::simir::IntegerCheck>(
                            operation);
                    });
            }));
    auto subtype_interpreter =
        vhdl_subtype_design.design->create_interpreter();
    assert(
        subtype_interpreter
            ->signal_value(*subtype_count_signal)
            .to_msb_string()
        == "00000000000000000000000000000100");
    const auto subtype_run = subtype_interpreter->run();
    assert(
        subtype_run.status
        == fsim::runtime::RunStatus::completed);
    assert(
        subtype_interpreter
            ->signal_value(*subtype_source_signal)
            .to_msb_string()
        == "10Z-");
    assert(
        subtype_interpreter
            ->signal_value(*subtype_result_signal)
            .to_msb_string()
        == "10Z-");
    assert(
        subtype_interpreter
            ->signal_value(*subtype_count_signal)
            .to_msb_string()
        == "00000000000000000000000000000101");
    assert(
        subtype_interpreter
            ->signal_value(*subtype_count_result_signal)
            .to_msb_string()
        == "00000000000000000000000000000101");
    assert(
        subtype_interpreter
            ->signal_value(*subtype_packet_signal)
            .to_msb_string()
        == "ULH-1");

    const auto invalid_vhdl_subtypes =
        fsim::frontend::parse_text(
            "invalid_vhdl_subtypes.vhd",
            R"(
package Invalid_Subtype_Types is
  subtype Imported_Count_T is natural range 2 to 9;
  subtype Imported_Vector_T is bit_vector(3 downto 0);
  constant Bad_Constant : Imported_Count_T := 10;
end package;

use work.invalid_subtype_types.all;
entity invalid_vhdl_subtypes is
  generic (
    Bad_Generic : Imported_Count_T := 10;
    Bad_Vector_Generic : Imported_Vector_T := "0000"
  );
  subtype Later_Port_T is bit;
  port (Bad_Port : in Later_Port_T);
end entity;

use work.invalid_subtype_types.all;
architecture rtl of invalid_vhdl_subtypes is
  subtype Bad_Scalar_T is boolean range 0 to 1;
  subtype Count_Base_T is integer range 0 to 3;
  subtype Bad_Count_T is Count_Base_T range 0 to 4;
  subtype Null_Count_T is integer range 3 to 2;
  subtype Bad_Packed_T is bit(3 downto 0);
  subtype Vector_T is bit_vector(3 downto 0);
  subtype Reconstraint_T is Vector_T(1 downto 0);
  subtype Unknown_T is Missing_T;
  subtype Cycle_A_T is Cycle_B_T;
  subtype Cycle_B_T is Cycle_A_T;
begin
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(invalid_vhdl_subtypes.ok());
    const auto rejected_vhdl_subtypes =
        fsim::elaboration::elaborate(
            invalid_vhdl_subtypes.design,
            "vhdl:work.invalid_vhdl_subtypes(rtl)");
    assert(!rejected_vhdl_subtypes.ok());
    const bool all_subtype_diagnostics =
        has_diagnostic(
            rejected_vhdl_subtypes,
            "FSIM-ELAB-VHSUBTYPE-001")
        && has_diagnostic(
            rejected_vhdl_subtypes,
            "FSIM-ELAB-VHSUBTYPE-002")
        && has_diagnostic(
            rejected_vhdl_subtypes,
            "FSIM-ELAB-VHSUBTYPE-003")
        && has_diagnostic(
            rejected_vhdl_subtypes,
            "FSIM-ELAB-VHSUBTYPE-004")
        && has_diagnostic(
            rejected_vhdl_subtypes,
            "FSIM-ELAB-INTEGER-002")
        && has_diagnostic(
            rejected_vhdl_subtypes,
            "FSIM-ELAB-PKG-006")
        && has_diagnostic(
            rejected_vhdl_subtypes,
            "FSIM-ELAB-GENERIC-008")
        && has_diagnostic(
            rejected_vhdl_subtypes,
            "FSIM-ELAB-GENERIC-010")
        && has_diagnostic(
            rejected_vhdl_subtypes,
            "FSIM-ELAB-VHTYPE-001")
        && has_diagnostic(
            rejected_vhdl_subtypes,
            "FSIM-ELAB-VHTYPE-002");
    if (!all_subtype_diagnostics) {
      for (const auto& diagnostic :
           rejected_vhdl_subtypes.diagnostics) {
        std::cerr << diagnostic.code << ": "
                  << diagnostic.message << '\n';
      }
    }
    assert(all_subtype_diagnostics);
    assert(std::ranges::any_of(
        rejected_vhdl_subtypes.diagnostics,
        [](const auto& diagnostic) {
            return diagnostic.code
                    == "FSIM-ELAB-VHTYPE-001"
                && diagnostic.message.find("later_port_t")
                    != std::string::npos;
        }));

    const auto vhdl_enumeration_source =
        fsim::frontend::parse_text(
            "vhdl_enumeration_execution.vhd",
            R"(
package State_Types is
  type State_T is (Idle, Load, Running, Done);
  subtype State_Alias_T is State_T;
  constant Initial_State : State_T := Load;
  type Symbol_T is ('A', 'B', 'C');
end package;

use work.state_types.all;
entity enumeration_execution is
  generic (Reset_State : State_T := Initial_State);
end entity;

use work.state_types.all;
architecture rtl of enumeration_execution is
  signal current : State_Alias_T;
  signal selected : State_T;
  signal symbol : Symbol_T;
  signal equal_result : boolean;
  signal ordered_result : boolean;
  signal position_result : integer;
  signal successor_result : State_T;
begin
  drive : process
    variable local_state : State_T := Reset_State;
    variable local_symbol : Symbol_T := 'A';
  begin
    current <= local_state;
    local_state := Running;
    case local_state is
      when Running =>
        selected <= state_types.Done;
      when others =>
        selected <= Idle;
    end case;
    local_symbol := 'C';
    symbol <= local_symbol;
    wait;
  end process;

  equal_result <= current /= Done;
  ordered_result <= current < Done;
  position_result <= State_T'pos(current);
  successor_result <= State_T'succ(current);
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(vhdl_enumeration_source.ok());
    const auto vhdl_enumeration_design =
        fsim::elaboration::elaborate(
            vhdl_enumeration_source.design,
            "vhdl:work.enumeration_execution(rtl)");
    if (!vhdl_enumeration_design.ok()) {
      for (const auto& diagnostic :
           vhdl_enumeration_design.diagnostics) {
        std::cerr << diagnostic.code << ": "
                  << diagnostic.message << " at "
                  << diagnostic.span.begin.line << ":"
                  << diagnostic.span.begin.column << '\n';
      }
    }
    assert(vhdl_enumeration_design.ok());
    const auto enum_current =
        vhdl_enumeration_design.design->find_signal("current");
    const auto enum_selected =
        vhdl_enumeration_design.design->find_signal("selected");
    const auto enum_symbol =
        vhdl_enumeration_design.design->find_signal("symbol");
    const auto enum_equal =
        vhdl_enumeration_design.design->find_signal(
            "equal_result");
    const auto enum_ordered =
        vhdl_enumeration_design.design->find_signal(
            "ordered_result");
    const auto enum_position =
        vhdl_enumeration_design.design->find_signal(
            "position_result");
    const auto enum_successor =
        vhdl_enumeration_design.design->find_signal(
            "successor_result");
    assert(
        enum_current && enum_selected && enum_symbol
        && enum_equal && enum_ordered
        && enum_position && enum_successor);
    const auto& enum_info =
        vhdl_enumeration_design.design->signals().at(
            *enum_current);
    const std::vector<std::string> expected_state_literals{
        "idle", "load", "running", "done"};
    const std::vector<std::string> expected_symbol_literals{
        "'A'", "'B'", "'C'"};
    assert(
        enum_info.width == 2
        && enum_info.source_domain
            == fsim::frontend::ValueDomain::Bit2
        && !enum_info.nominal_type.empty()
        && enum_info.enumeration_literals
            == expected_state_literals);
    const auto enum_process = std::find_if(
        vhdl_enumeration_design.design->processes().begin(),
        vhdl_enumeration_design.design->processes().end(),
        [](const auto& process) {
            return process.name.ends_with(".drive");
        });
    assert(
        enum_process
            != vhdl_enumeration_design.design->processes().end()
        && enum_process->debug_locals.size() == 2
        && enum_process->debug_locals[0].enumeration_literals
            == enum_info.enumeration_literals
        && enum_process->debug_locals[1].enumeration_literals
            == expected_symbol_literals);
    auto enum_interpreter =
        vhdl_enumeration_design.design->create_interpreter();
    assert(
        enum_interpreter->signal_value(*enum_current)
            .to_msb_string()
        == "00");
    const auto enum_run = enum_interpreter->run();
    assert(enum_run.status == fsim::runtime::RunStatus::completed);
    assert(
        enum_interpreter->signal_value(*enum_current)
                .to_msb_string()
            == "01"
        && enum_interpreter->signal_value(*enum_selected)
                .to_msb_string()
            == "11"
        && enum_interpreter->signal_value(*enum_symbol)
                .to_msb_string()
            == "10"
        && enum_interpreter->signal_value(*enum_equal)
                .to_msb_string()
            == "1"
        && enum_interpreter->signal_value(*enum_ordered)
                .to_msb_string()
            == "1"
        && enum_interpreter->signal_value(*enum_position)
                .to_msb_string()
            == "00000000000000000000000000000001"
        && enum_interpreter->signal_value(*enum_successor)
                .to_msb_string()
            == "10");
    assert(
        std::ranges::any_of(
            vhdl_enumeration_design.design->processes(),
            [](const auto& process) {
                return std::ranges::any_of(
                    process.operations,
                    [](const auto& operation) {
                        return std::holds_alternative<
                                   fsim::runtime::simir::IntegerCheck>(
                                   operation)
                            || std::holds_alternative<
                                   fsim::runtime::simir::IntegerBinary>(
                                   operation);
                    });
            }));

    const auto invalid_vhdl_enumerations =
        fsim::frontend::parse_text(
            "invalid_vhdl_enumerations.vhd",
            R"(
entity invalid_vhdl_enumerations is
end entity;
architecture rtl of invalid_vhdl_enumerations is
  type First_T is (Low, High);
  type Second_T is (Low, High);
  signal first : First_T;
  signal second : Second_T;
  signal position : integer;
begin
  first <= second;
  second <= Missing;
  first <= first + first;
  first <= First_T'val(2);
  position <= First_T'pos(1);
  first <= First_T'val(A);
  first <= First_T'left(A);
  first <= first'right;
  first <= Second_T'left;
  position <= First_T'pos(first);
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(invalid_vhdl_enumerations.ok());
    const auto rejected_vhdl_enumerations =
        fsim::elaboration::elaborate(
            invalid_vhdl_enumerations.design,
            "vhdl:work.invalid_vhdl_enumerations(rtl)");
    assert(
        !rejected_vhdl_enumerations.ok()
        && has_diagnostic(
            rejected_vhdl_enumerations,
            "FSIM-ELAB-VHENUM-001")
        && has_diagnostic(
            rejected_vhdl_enumerations,
            "FSIM-ELAB-VHENUM-002")
        && has_diagnostic(
            rejected_vhdl_enumerations,
            "FSIM-ELAB-VHENUM-003")
        && has_diagnostic(
            rejected_vhdl_enumerations,
            "FSIM-ELAB-VHENUMATTR-001")
        && has_diagnostic(
            rejected_vhdl_enumerations,
            "FSIM-ELAB-VHENUMATTR-002"));
    assert(std::ranges::any_of(
        rejected_vhdl_enumerations.diagnostics,
        [](const auto& diagnostic) {
            return diagnostic.code
                    == "FSIM-ELAB-VHENUMATTR-001"
                && diagnostic.message.find(
                       "requires a value of enumeration type")
                    != std::string::npos;
        }));
    assert(std::ranges::any_of(
        rejected_vhdl_enumerations.diagnostics,
        [](const auto& diagnostic) {
            return diagnostic.code
                    == "FSIM-ELAB-VHENUMATTR-001"
                && diagnostic.message.find(
                       "'left on enumeration type")
                    != std::string::npos
                && diagnostic.message.find(
                       "requires 0 arguments")
                    != std::string::npos;
        }));
    assert(std::ranges::any_of(
        rejected_vhdl_enumerations.diagnostics,
        [](const auto& diagnostic) {
            return diagnostic.code
                    == "FSIM-ELAB-VHENUMATTR-001"
                && diagnostic.message.find(
                       "requires an integer-family argument")
                    != std::string::npos;
        }));

    const auto enumeration_boundary_vhdl =
        fsim::frontend::parse_text(
            "enumeration_boundaries.vhd",
            R"(
package Enumeration_Boundary_Types is
  type First_T is (Low, High);
  type Second_T is (Low, High);
end package;

use work.enumeration_boundary_types.all;
entity Enumeration_Child is
  port (Value : in First_T);
end entity;
architecture rtl of enumeration_child is
begin
end architecture;

use work.enumeration_boundary_types.all;
entity Enumeration_Native_Parent is
end entity;
use work.enumeration_boundary_types.all;
architecture rtl of enumeration_native_parent is
  signal Actual : Second_T;
begin
  Child : entity work.Enumeration_Child(rtl)
    port map (Value => Actual);
end architecture;

use work.enumeration_boundary_types.all;
entity Enumeration_Mixed_Parent is
end entity;
use work.enumeration_boundary_types.all;
architecture rtl of enumeration_mixed_parent is
  signal Actual : First_T;
begin
  Child : Foreign_Enumeration_Child
    port map (Value => Actual);
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(enumeration_boundary_vhdl.ok());
    const auto rejected_native_enumeration_boundary =
        fsim::elaboration::elaborate(
            enumeration_boundary_vhdl.design,
            "vhdl:work.enumeration_native_parent(rtl)");
    assert(
        !rejected_native_enumeration_boundary.ok()
        && has_diagnostic(
            rejected_native_enumeration_boundary,
            "FSIM-ELAB-BIND-053"));

    const auto enumeration_boundary_sv =
        fsim::frontend::parse_text(
            "foreign_enumeration_child.sv",
            R"(
module foreign_enumeration_child(input bit value);
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(enumeration_boundary_sv.ok());
    auto mixed_enumeration_boundary =
        enumeration_boundary_vhdl.design;
    mixed_enumeration_boundary.units.insert(
        mixed_enumeration_boundary.units.end(),
        enumeration_boundary_sv.design.units.begin(),
        enumeration_boundary_sv.design.units.end());
    const std::vector<fsim::elaboration::Binding>
        enumeration_boundary_binding{
            {
                "enumeration_mixed_parent.child",
                "sv:work.foreign_enumeration_child",
                std::nullopt}};
    const auto rejected_mixed_enumeration_boundary =
        fsim::elaboration::elaborate(
            mixed_enumeration_boundary,
            "vhdl:work.enumeration_mixed_parent(rtl)",
            enumeration_boundary_binding);
    assert(
        !rejected_mixed_enumeration_boundary.ok()
        && has_diagnostic(
            rejected_mixed_enumeration_boundary,
            "FSIM-ELAB-BIND-052"));

    const auto invalid_vhdl_shift =
        fsim::frontend::parse_text(
            "invalid_vhdl_shift.vhd",
            R"(
entity invalid_vhdl_shift is
  port (
    lhs : in signed(7 downto 0);
    result : out signed(7 downto 0)
  );
end entity;

architecture rtl of invalid_vhdl_shift is
begin
  result <= lhs sll lhs;
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(invalid_vhdl_shift.ok());
    const auto rejected_vhdl_shift =
        fsim::elaboration::elaborate(
            invalid_vhdl_shift.design,
            "vhdl:work.invalid_vhdl_shift(rtl)");
    assert(!rejected_vhdl_shift.ok());
    assert(has_diagnostic(
        rejected_vhdl_shift, "FSIM-ELAB-070"));

    const auto invalid_vhdl_abs =
        fsim::frontend::parse_text(
            "invalid_vhdl_abs.vhd",
            R"(
entity invalid_vhdl_abs is
  port (
    lhs : in unsigned(7 downto 0);
    result : out unsigned(7 downto 0)
  );
end entity;

architecture rtl of invalid_vhdl_abs is
begin
  result <= abs lhs;
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(invalid_vhdl_abs.ok());
    const auto rejected_vhdl_abs =
        fsim::elaboration::elaborate(
            invalid_vhdl_abs.design,
            "vhdl:work.invalid_vhdl_abs(rtl)");
    assert(!rejected_vhdl_abs.ok());
    assert(has_diagnostic(
        rejected_vhdl_abs, "FSIM-ELAB-082"));

    const auto generic_integer_constraint =
        fsim::frontend::parse_text(
            "generic_integer_constraint.vhd",
            R"(
entity generic_integer_constraint is
  generic (limit : natural := 4);
  port (value : out integer range -limit to limit);
end entity;
architecture rtl of generic_integer_constraint is
begin
  value <= limit;
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(generic_integer_constraint.ok());
    const auto elaborated_integer_constraint =
        fsim::elaboration::elaborate(
            generic_integer_constraint.design,
            "vhdl:work.generic_integer_constraint(rtl)");
    assert(elaborated_integer_constraint.ok());
    const auto generic_value =
        elaborated_integer_constraint.design->find_signal("value");
    assert(generic_value);
    const auto& generic_value_info =
        elaborated_integer_constraint.design
            ->signals().at(*generic_value);
    assert(
        generic_value_info.integer_range
        && generic_value_info.integer_range->left == -4
        && generic_value_info.integer_range->right == 4);

    const auto invalid_integer_constraints =
        fsim::frontend::parse_text(
            "invalid_integer_constraints.vhd",
            R"(
entity invalid_integer_constraints is
end entity;
architecture rtl of invalid_integer_constraints is
  signal outside_base : natural range -1 to 2;
  signal null_range : integer range 3 to -2;
  signal assigned : positive;
  signal bits : bit_vector(31 downto 0);
  signal integer_target : integer;
begin
  assigned <= 0;
  integer_target <= bits;
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(invalid_integer_constraints.ok());
    const auto rejected_integer_constraints =
        fsim::elaboration::elaborate(
            invalid_integer_constraints.design,
            "vhdl:work.invalid_integer_constraints(rtl)");
    assert(!rejected_integer_constraints.ok());
    assert(has_diagnostic(
        rejected_integer_constraints,
        "FSIM-ELAB-INTEGER-002"));
    assert(has_diagnostic(
        rejected_integer_constraints,
        "FSIM-ELAB-INTEGER-003"));
    assert(has_diagnostic(
        rejected_integer_constraints,
        "FSIM-ELAB-INTEGER-004"));

    const auto mixed_vhdl_arithmetic =
        fsim::frontend::parse_text(
            "mixed_vhdl_arithmetic.vhd",
            R"(
entity mixed_vhdl_arithmetic is
  port (
    lhs : in signed(7 downto 0);
    rhs : in unsigned(7 downto 0);
    result : out signed(7 downto 0)
  );
end entity;
architecture rtl of mixed_vhdl_arithmetic is
begin
  result <= lhs + rhs;
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(mixed_vhdl_arithmetic.ok());
    const auto rejected_mixed_vhdl_arithmetic =
        fsim::elaboration::elaborate(
            mixed_vhdl_arithmetic.design,
            "vhdl:work.mixed_vhdl_arithmetic(rtl)");
    assert(!rejected_mixed_vhdl_arithmetic.ok());
    assert(has_diagnostic(
        rejected_mixed_vhdl_arithmetic, "FSIM-ELAB-067"));

    const auto select_concat_process =
        fsim::frontend::parse_text(
            "select_concat_process.sv",
            R"(
module select_concat_process;
  logic [15:8] descending;
  logic [0:7] ascending;
  logic selected_descending;
  logic selected_ascending;
  logic selected_local;
  logic [3:0] descending_part;
  logic [3:0] ascending_part;
  logic [8:0] joined;
  always_comb begin
    logic [5:2] local_copy;
    local_copy = descending[15:12];
    selected_descending = descending[10];
    selected_ascending = ascending[2];
    selected_local = local_copy[3];
    descending_part = descending[15:12];
    ascending_part = ascending[2:5];
    joined = {
      descending[15:12], descending[10], ascending[4:7]
    };
  end
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(select_concat_process.ok());
    const auto elaborated_select_concat =
        fsim::elaboration::elaborate(
            select_concat_process.design,
            "sv:work.select_concat_process");
    assert(elaborated_select_concat.ok());
    const auto descending =
        elaborated_select_concat.design->find_signal("descending");
    const auto ascending =
        elaborated_select_concat.design->find_signal("ascending");
    const std::array select_concat_outputs{
        elaborated_select_concat.design->find_signal(
            "selected_descending"),
        elaborated_select_concat.design->find_signal(
            "selected_ascending"),
        elaborated_select_concat.design->find_signal(
            "selected_local"),
        elaborated_select_concat.design->find_signal(
            "descending_part"),
        elaborated_select_concat.design->find_signal(
            "ascending_part"),
        elaborated_select_concat.design->find_signal("joined")};
    assert(descending && ascending);
    assert(std::ranges::all_of(
        select_concat_outputs,
        [](const auto& signal) {
          return signal.has_value();
        }));
    auto select_concat_interpreter =
        elaborated_select_concat.design->create_interpreter();
    select_concat_interpreter->deposit_signal(
        *descending,
        fsim::runtime::PackedLogic4::from_msb_string("10XZ0110"));
    select_concat_interpreter->deposit_signal(
        *ascending,
        fsim::runtime::PackedLogic4::from_msb_string("01ZX1100"));
    (void)select_concat_interpreter->run();
    const std::array<std::string_view, 6> expected_select_concat{
        "1", "Z", "X", "10XZ", "ZX11", "10XZ11100"};
    for (std::size_t index = 0;
         index < select_concat_outputs.size(); ++index) {
      assert(
          select_concat_interpreter
              ->signal_value(*select_concat_outputs[index])
              .to_msb_string()
          == expected_select_concat[index]);
    }

    const auto selected_assignment =
        fsim::frontend::parse_text(
            "selected_assignment.sv",
            R"(
module selected_assignment;
  logic [15:8] descending;
  logic [0:7] ascending;
  logic [5:2] local_result;
  initial begin
    logic [5:2] local_copy;
    descending[8] <= 1'b1;
    descending = 8'b00000000;
    descending[9] = 1'b1;
    descending[15:12] = 4'b10xz;
    ascending <= 8'b10101010;
    ascending[4:5] <= 2'bxz;
    local_copy = 4'b0000;
    local_copy[3] = 1'b1;
    local_copy[5:4] = 2'bxz;
    local_result = local_copy;
    descending[11:10] <= #5 2'b11;
  end
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(selected_assignment.ok());
    const auto elaborated_selected_assignment =
        fsim::elaboration::elaborate(
            selected_assignment.design,
            "sv:work.selected_assignment");
    if (!elaborated_selected_assignment.ok()) {
      for (const auto& diagnostic :
           elaborated_selected_assignment.diagnostics) {
        std::cerr << diagnostic.code << ": "
                  << diagnostic.message << '\n';
      }
    }
    assert(elaborated_selected_assignment.ok());
    const auto selected_descending =
        elaborated_selected_assignment.design->find_signal(
            "descending");
    const auto selected_ascending =
        elaborated_selected_assignment.design->find_signal(
            "ascending");
    const auto selected_local =
        elaborated_selected_assignment.design->find_signal(
            "local_result");
    assert(
        selected_descending && selected_ascending
        && selected_local);
    auto selected_assignment_interpreter =
        elaborated_selected_assignment.design->create_interpreter();
    const auto selected_assignment_result =
        selected_assignment_interpreter->run();
    assert(
        selected_assignment_result.status
            == fsim::runtime::RunStatus::completed
        && selected_assignment_result.time == 5);
    assert(
        selected_assignment_interpreter
            ->signal_value(*selected_descending)
            .to_msb_string()
        == "10XZ1111");
    assert(
        selected_assignment_interpreter
            ->signal_value(*selected_ascending)
            .to_msb_string()
        == "1010XZ10");
    assert(
        selected_assignment_interpreter
            ->signal_value(*selected_local)
            .to_msb_string()
        == "XZ10");

    const auto reversed_assignment_select =
        fsim::frontend::parse_text(
            "reversed_assignment_select.sv",
            R"(
module reversed_assignment_select;
  logic [0:7] value;
  initial value[5:2] = 4'b1010;
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(reversed_assignment_select.ok());
    const auto rejected_reversed_assignment_select =
        fsim::elaboration::elaborate(
            reversed_assignment_select.design,
            "sv:work.reversed_assignment_select");
    assert(!rejected_reversed_assignment_select.ok());
    assert(has_diagnostic(
        rejected_reversed_assignment_select, "FSIM-ELAB-068"));

    const auto reversed_select = fsim::frontend::parse_text(
        "reversed_select.sv",
        R"(
module reversed_select;
  logic [0:7] value;
  logic [3:0] result;
  always_comb result = value[5:2];
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(reversed_select.ok());
    const auto rejected_reversed_select =
        fsim::elaboration::elaborate(
            reversed_select.design, "sv:work.reversed_select");
    assert(!rejected_reversed_select.ok());
    assert(has_diagnostic(
        rejected_reversed_select, "FSIM-ELAB-068"));

    const auto dynamic_select = fsim::frontend::parse_text(
        "dynamic_select.sv",
        R"(
module dynamic_select;
  logic [7:0] value;
  logic [2:0] index;
  logic result;
  always_comb result = value[index];
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(dynamic_select.ok());
    const auto rejected_dynamic_select =
        fsim::elaboration::elaborate(
            dynamic_select.design, "sv:work.dynamic_select");
    assert(!rejected_dynamic_select.ok());
    assert(has_diagnostic(
        rejected_dynamic_select, "FSIM-ELAB-068"));

    const auto empty_concatenation =
        fsim::frontend::parse_text(
            "empty_concatenation.sv",
            R"(
module empty_concatenation;
  logic result;
  always_comb result = {};
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(empty_concatenation.ok());
    const auto rejected_empty_concatenation =
        fsim::elaboration::elaborate(
            empty_concatenation.design,
            "sv:work.empty_concatenation");
    assert(!rejected_empty_concatenation.ok());
    assert(has_diagnostic(
        rejected_empty_concatenation, "FSIM-ELAB-069"));

    const auto vhdl_select_concat =
        fsim::frontend::parse_text(
            "vhdl_select_concat.vhd",
            R"(
entity vhdl_select_concat is
  port (
    descending : in std_logic_vector(7 downto 4);
    ascending : in std_logic_vector(2 to 5);
    selected_descending : out std_logic;
    selected_ascending : out std_logic;
    selected_local : out std_logic;
    descending_part : out std_logic_vector(1 downto 0);
    ascending_part : out std_logic_vector(1 downto 0);
    joined : out std_logic_vector(5 downto 0)
  );
end entity;

architecture rtl of vhdl_select_concat is
begin
  observe: process(descending, ascending)
    variable local_copy : std_logic_vector(9 downto 8);
  begin
    local_copy := descending(7 downto 6);
    selected_descending <= descending(5);
    selected_ascending <= ascending(4);
    selected_local <= local_copy(8);
    descending_part <= descending(7 downto 6);
    ascending_part <= ascending(3 to 4);
    joined <= descending(7 downto 6) & "10" & ascending(4 to 5);
  end process;
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(vhdl_select_concat.ok());
    const auto elaborated_vhdl_select_concat =
        fsim::elaboration::elaborate(
            vhdl_select_concat.design,
            "vhdl:work.vhdl_select_concat(rtl)");
    assert(elaborated_vhdl_select_concat.ok());
    const auto vhdl_descending =
        elaborated_vhdl_select_concat.design->find_signal(
            "descending");
    const auto vhdl_ascending =
        elaborated_vhdl_select_concat.design->find_signal(
            "ascending");
    const std::array vhdl_select_concat_outputs{
        elaborated_vhdl_select_concat.design->find_signal(
            "selected_descending"),
        elaborated_vhdl_select_concat.design->find_signal(
            "selected_ascending"),
        elaborated_vhdl_select_concat.design->find_signal(
            "selected_local"),
        elaborated_vhdl_select_concat.design->find_signal(
            "descending_part"),
        elaborated_vhdl_select_concat.design->find_signal(
            "ascending_part"),
        elaborated_vhdl_select_concat.design->find_signal("joined")};
    assert(vhdl_descending && vhdl_ascending);
    assert(std::ranges::all_of(
        vhdl_select_concat_outputs,
        [](const auto& signal) {
          return signal.has_value();
        }));
    auto vhdl_select_concat_interpreter =
        elaborated_vhdl_select_concat.design->create_interpreter();
    vhdl_select_concat_interpreter->deposit_signal(
        *vhdl_descending,
        fsim::runtime::PackedLogic4::from_msb_string("1XZ0"));
    vhdl_select_concat_interpreter->deposit_signal(
        *vhdl_ascending,
        fsim::runtime::PackedLogic4::from_msb_string("01Z1"));
    (void)vhdl_select_concat_interpreter->run();
    const std::array<std::string_view, 6>
        expected_vhdl_select_concat{
            "Z", "Z", "X", "1X", "1Z", "1X10Z1"};
    for (std::size_t index = 0;
         index < vhdl_select_concat_outputs.size(); ++index) {
      assert(
          vhdl_select_concat_interpreter
              ->signal_value(*vhdl_select_concat_outputs[index])
              .to_msb_string()
          == expected_vhdl_select_concat[index]);
    }

    const auto vhdl_selected_assignment =
        fsim::frontend::parse_text(
            "vhdl_selected_assignment.vhd",
            R"(
entity vhdl_selected_assignment is
end entity;

architecture rtl of vhdl_selected_assignment is
  signal trigger : std_logic;
  signal descending : std_logic_vector(15 downto 8);
  signal ascending : std_logic_vector(0 to 7);
  signal local_result : std_logic_vector(5 downto 2);
begin
  update: process(trigger)
    variable local_copy : std_logic_vector(5 downto 2);
  begin
    descending <= "00000000";
    descending(9) <= '1';
    descending(15 downto 12) <= "10XZ";
    ascending <= "10101010";
    ascending(4 to 5) <= "XZ";
    local_copy := "0000";
    local_copy(3) := '1';
    local_copy(5 downto 4) := "XZ";
    local_result <= local_copy;
    descending(11 downto 10) <= "11" after 5 ns;
  end process;
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(vhdl_selected_assignment.ok());
    const auto elaborated_vhdl_selected_assignment =
        fsim::elaboration::elaborate(
            vhdl_selected_assignment.design,
            "vhdl:work.vhdl_selected_assignment(rtl)");
    if (!elaborated_vhdl_selected_assignment.ok()) {
      for (const auto& diagnostic :
           elaborated_vhdl_selected_assignment.diagnostics) {
        std::cerr << diagnostic.code << ": "
                  << diagnostic.message << '\n';
      }
    }
    assert(elaborated_vhdl_selected_assignment.ok());
    const auto vhdl_assigned_descending =
        elaborated_vhdl_selected_assignment.design->find_signal(
            "descending");
    const auto vhdl_assigned_ascending =
        elaborated_vhdl_selected_assignment.design->find_signal(
            "ascending");
    const auto vhdl_assigned_local =
        elaborated_vhdl_selected_assignment.design->find_signal(
            "local_result");
    assert(
        vhdl_assigned_descending && vhdl_assigned_ascending
        && vhdl_assigned_local);
    auto vhdl_selected_assignment_interpreter =
        elaborated_vhdl_selected_assignment.design
            ->create_interpreter();
    const auto vhdl_selected_assignment_result =
        vhdl_selected_assignment_interpreter->run();
    assert(
        vhdl_selected_assignment_result.status
            == fsim::runtime::RunStatus::completed
        && vhdl_selected_assignment_result.time == 5);
    assert(
        vhdl_selected_assignment_interpreter
            ->signal_value(*vhdl_assigned_descending)
            .to_msb_string()
        == "10XZ1110");
    assert(
        vhdl_selected_assignment_interpreter
            ->signal_value(*vhdl_assigned_ascending)
            .to_msb_string()
        == "1010XZ10");
    assert(
        vhdl_selected_assignment_interpreter
            ->signal_value(*vhdl_assigned_local)
            .to_msb_string()
        == "XZ10");

    const auto reversed_vhdl_select =
        fsim::frontend::parse_text(
            "reversed_vhdl_select.vhd",
            R"(
entity reversed_vhdl_select is
  port (
    value : in std_logic_vector(2 to 5);
    result : out std_logic_vector(1 downto 0)
  );
end entity;
architecture rtl of reversed_vhdl_select is
begin
  result <= value(4 downto 3);
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(reversed_vhdl_select.ok());
    const auto rejected_reversed_vhdl_select =
        fsim::elaboration::elaborate(
            reversed_vhdl_select.design,
            "vhdl:work.reversed_vhdl_select(rtl)");
    assert(!rejected_reversed_vhdl_select.ok());
    assert(has_diagnostic(
        rejected_reversed_vhdl_select, "FSIM-ELAB-068"));

    const auto empty_wildcard = fsim::frontend::parse_text(
        "empty_wildcard.sv",
        R"(
module empty_wildcard;
  logic q;
  always @* q = 1'b0;
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(empty_wildcard.ok());
    const auto rejected_empty_wildcard =
        fsim::elaboration::elaborate(
            empty_wildcard.design, "sv:work.empty_wildcard");
    assert(!rejected_empty_wildcard.ok());
    assert(has_diagnostic(
        rejected_empty_wildcard, "FSIM-ELAB-061"));

    const auto dynamic_wildcard = fsim::frontend::parse_text(
        "dynamic_wildcard.sv",
        R"(
module dynamic_wildcard;
  logic trigger;
  logic observed;
  initial @* observed = trigger;
  initial begin
    trigger = 1'b0;
    #1 trigger = 1'b1;
    #1 $finish;
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(dynamic_wildcard.ok());
    const auto elaborated_dynamic_wildcard =
        fsim::elaboration::elaborate(
            dynamic_wildcard.design, "sv:work.dynamic_wildcard");
    assert(elaborated_dynamic_wildcard.ok());
    const auto& dynamic_wait_process =
        elaborated_dynamic_wildcard.design->processes().front();
    const auto dynamic_wait = std::find_if(
        dynamic_wait_process.operations.begin(),
        dynamic_wait_process.operations.end(),
        [](const fsim::runtime::simir::Operation& operation) {
          return std::holds_alternative<
              fsim::runtime::simir::WaitOn>(operation);
        });
    assert(dynamic_wait != dynamic_wait_process.operations.end());
    assert(
        std::get<fsim::runtime::simir::WaitOn>(*dynamic_wait)
            .signals.size()
        == 1);
    auto dynamic_wildcard_interpreter =
        elaborated_dynamic_wildcard.design->create_interpreter();
    const auto dynamic_observed =
        elaborated_dynamic_wildcard.design->find_signal("observed");
    assert(dynamic_observed);
    const auto dynamic_wildcard_run =
        dynamic_wildcard_interpreter->run();
    assert(
        dynamic_wildcard_run.status
        == fsim::runtime::RunStatus::stopped);
    assert(
        dynamic_wildcard_interpreter
            ->signal_value(*dynamic_observed)
            .to_msb_string()
        == "0");

    const auto empty_dynamic_wildcard =
        fsim::frontend::parse_text(
            "empty_dynamic_wildcard.sv",
            R"(
module empty_dynamic_wildcard;
  initial @*;
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(empty_dynamic_wildcard.ok());
    const auto rejected_empty_dynamic_wildcard =
        fsim::elaboration::elaborate(
            empty_dynamic_wildcard.design,
            "sv:work.empty_dynamic_wildcard");
    assert(!rejected_empty_dynamic_wildcard.ok());
    assert(has_diagnostic(
        rejected_empty_dynamic_wildcard, "FSIM-ELAB-062"));

    const auto parsed_sv_conditionals =
        fsim::frontend::parse_text(
            "conditional_flow.sv",
            R"(
module conditional_flow;
  logic zero_case;
  logic one_x_case;
  logic unknown_case;
  logic [3:0] nested_case;
  initial begin
    if (4'b0000)
      zero_case = 1'b1;
    else
      zero_case = 1'b0;
    if (4'bx001)
      one_x_case = 1'b1;
    else
      one_x_case = 1'b0;
    if (4'bx000)
      unknown_case = 1'b1;
    else
      unknown_case = 1'b0;
    if (4'b0010) begin
      if (1'b0)
        nested_case = 4'b0001;
      else
        nested_case = 4'b0010;
    end else begin
      nested_case = 4'b0011;
    end
  end
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(parsed_sv_conditionals.ok());
    const auto elaborated_sv_conditionals =
        fsim::elaboration::elaborate(
            parsed_sv_conditionals.design,
            "sv:work.conditional_flow");
    assert(elaborated_sv_conditionals.ok());
    const auto& sv_conditional_process =
        elaborated_sv_conditionals.design->processes().front();
    assert(
        std::count_if(
            sv_conditional_process.operations.begin(),
            sv_conditional_process.operations.end(),
            [](const fsim::runtime::simir::Operation& operation) {
                return std::holds_alternative<
                    fsim::runtime::simir::Branch>(operation);
            })
        == 5);
    assert(
        std::count_if(
            sv_conditional_process.operations.begin(),
            sv_conditional_process.operations.end(),
            [](const fsim::runtime::simir::Operation& operation) {
                return std::holds_alternative<
                    fsim::runtime::simir::LogicalNot>(operation);
            })
        == 10);
    auto sv_conditional_interpreter =
        elaborated_sv_conditionals.design->create_interpreter();
    const auto sv_conditional_result =
        sv_conditional_interpreter->run();
    assert(
        sv_conditional_result.status
        == fsim::runtime::RunStatus::completed);
    const auto zero_case =
        elaborated_sv_conditionals.design->find_signal("zero_case");
    const auto one_x_case =
        elaborated_sv_conditionals.design->find_signal("one_x_case");
    const auto unknown_case =
        elaborated_sv_conditionals.design->find_signal("unknown_case");
    const auto nested_case =
        elaborated_sv_conditionals.design->find_signal("nested_case");
    assert(zero_case && one_x_case && unknown_case && nested_case);
    assert(
        sv_conditional_interpreter
            ->signal_value(*zero_case)
            .to_msb_string()
        == "0");
    assert(
        sv_conditional_interpreter
            ->signal_value(*one_x_case)
            .to_msb_string()
        == "1");
    assert(
        sv_conditional_interpreter
            ->signal_value(*unknown_case)
            .to_msb_string()
        == "0");
    assert(
        sv_conditional_interpreter
            ->signal_value(*nested_case)
            .to_msb_string()
        == "0010");

    const auto parsed_vhdl_conditionals =
        fsim::frontend::parse_text(
            "conditional_flow.vhd",
            R"(
entity conditional_flow is
  port (
    true_case : out boolean;
    elsif_case : out boolean;
    nested_case : out boolean;
    boolean_expression_case : out boolean
  );
end entity;

architecture rtl of conditional_flow is
  signal trigger : std_logic;
begin
  choose: process(trigger)
  begin
    if true then
      true_case <= true;
    else
      true_case <= false;
    end if;
    if false then
      elsif_case <= false;
    elsif true /= false then
      elsif_case <= true;
    else
      elsif_case <= false;
    end if;
    if 1 = 1 then
      if false then
        nested_case <= false;
      else
        nested_case <= true;
      end if;
    else
      nested_case <= false;
    end if;
    if (not false) and (true nand false)
       and (false nor false) and (true xnor true)
       and (true /= false) then
      boolean_expression_case <= true;
    else
      boolean_expression_case <= false;
    end if;
  end process;
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(parsed_vhdl_conditionals.ok());
    const auto elaborated_vhdl_conditionals =
        fsim::elaboration::elaborate(
            parsed_vhdl_conditionals.design,
            "vhdl:work.conditional_flow(rtl)");
    if (!elaborated_vhdl_conditionals.ok()) {
        for (const auto& diagnostic :
             elaborated_vhdl_conditionals.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(elaborated_vhdl_conditionals.ok());
    auto vhdl_conditional_interpreter =
        elaborated_vhdl_conditionals.design->create_interpreter();
    const auto vhdl_conditional_result =
        vhdl_conditional_interpreter->run();
    assert(
        vhdl_conditional_result.status
        == fsim::runtime::RunStatus::completed);
    for (const auto name :
         {"true_case", "elsif_case", "nested_case",
          "boolean_expression_case"}) {
        const auto signal =
            elaborated_vhdl_conditionals.design->find_signal(name);
        assert(signal);
        assert(
            vhdl_conditional_interpreter
                ->signal_value(*signal)
                .to_msb_string()
            == "1");
    }

    const auto vhdl_sequential_loops =
        fsim::frontend::parse_text(
            "sequential_loops.vhd",
            R"(
entity sequential_loops is
  port (
    kick : in std_logic;
    observed : out std_logic_vector(3 downto 0);
    null_range_observed : out std_logic_vector(1 downto 0)
  );
end entity;
architecture rtl of sequential_loops is
begin
  populate: process(kick)
    variable assembled : std_logic_vector(3 downto 0) := "0000";
    variable untouched : std_logic_vector(1 downto 0) := "00";
  begin
    for lane in 0 to 3 loop
      assembled(lane) := '1';
    end loop;
    for lane in 3 downto 2 loop
      assembled(lane) := '0';
    end loop;
    for lane in 2 to 1 loop
      untouched(0) := '1';
    end loop;
    observed <= assembled;
    null_range_observed <= untouched;
  end process;
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(vhdl_sequential_loops.ok());
    const auto elaborated_vhdl_sequential_loops =
        fsim::elaboration::elaborate(
            vhdl_sequential_loops.design,
            "vhdl:work.sequential_loops(rtl)");
    if (!elaborated_vhdl_sequential_loops.ok()) {
        for (const auto& diagnostic :
             elaborated_vhdl_sequential_loops.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(elaborated_vhdl_sequential_loops.ok());
    auto vhdl_loop_interpreter =
        elaborated_vhdl_sequential_loops.design
            ->create_interpreter();
    const auto vhdl_loop_result =
        vhdl_loop_interpreter->run();
    assert(
        vhdl_loop_result.status
        == fsim::runtime::RunStatus::completed);
    const auto loop_observed =
        elaborated_vhdl_sequential_loops.design
            ->find_signal("observed");
    const auto null_range_observed =
        elaborated_vhdl_sequential_loops.design
            ->find_signal("null_range_observed");
    assert(loop_observed && null_range_observed);
    assert(
        vhdl_loop_interpreter
            ->signal_value(*loop_observed)
            .to_msb_string()
        == "0011");
    assert(
        vhdl_loop_interpreter
            ->signal_value(*null_range_observed)
            .to_msb_string()
        == "00");

    const auto invalid_vhdl_loops =
        fsim::frontend::parse_text(
            "invalid_sequential_loops.vhd",
            R"(
entity invalid_sequential_loops is
  port (dynamic_bound : in std_logic);
end entity;
architecture rtl of invalid_sequential_loops is
begin
  invalid: process(dynamic_bound)
  begin
    for lane in dynamic_bound to 1 loop
      null;
    end loop;
    for lane in 0 to dynamic_bound loop
      null;
    end loop;
    for lane in 0 to 1000000 loop
      null;
    end loop;
    for lane in 0 to 1 loop
      lane := lane + 1;
    end loop;
  end process;
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(invalid_vhdl_loops.ok());
    const auto rejected_vhdl_loops =
        fsim::elaboration::elaborate(
            invalid_vhdl_loops.design,
            "vhdl:work.invalid_sequential_loops(rtl)");
    assert(!rejected_vhdl_loops.ok());
    for (const auto code :
         {"FSIM-ELAB-071", "FSIM-ELAB-072",
          "FSIM-ELAB-073", "FSIM-ELAB-074"}) {
        assert(has_diagnostic(rejected_vhdl_loops, code));
    }

    const auto systemverilog_procedural_loops =
        fsim::frontend::parse_text(
            "procedural_loops.sv",
            R"(
module procedural_loops;
  logic [3:0] observed;
  logic [1:0] null_range_observed;
  initial begin
    observed = 4'b0000;
    null_range_observed = 2'b00;
    for (int lane = 0; lane < 4; lane++)
      observed[lane] = 1'b1;
    for (int lane = 3; lane >= 2; --lane)
      observed[lane] = 1'b0;
    for (int lane = 2; lane < 1; lane += 1)
      null_range_observed[0] = 1'b1;
  end
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(systemverilog_procedural_loops.ok());
    const auto elaborated_systemverilog_procedural_loops =
        fsim::elaboration::elaborate(
            systemverilog_procedural_loops.design,
            "sv:work.procedural_loops");
    if (!elaborated_systemverilog_procedural_loops.ok()) {
        for (const auto& diagnostic :
             elaborated_systemverilog_procedural_loops.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(elaborated_systemverilog_procedural_loops.ok());
    auto systemverilog_loop_interpreter =
        elaborated_systemverilog_procedural_loops.design
            ->create_interpreter();
    const auto systemverilog_loop_result =
        systemverilog_loop_interpreter->run();
    assert(
        systemverilog_loop_result.status
        == fsim::runtime::RunStatus::completed);
    const auto systemverilog_loop_observed =
        elaborated_systemverilog_procedural_loops.design
            ->find_signal("observed");
    const auto systemverilog_null_range_observed =
        elaborated_systemverilog_procedural_loops.design
            ->find_signal("null_range_observed");
    assert(
        systemverilog_loop_observed
        && systemverilog_null_range_observed);
    assert(
        systemverilog_loop_interpreter
            ->signal_value(*systemverilog_loop_observed)
            .to_msb_string()
        == "0011");
    assert(
        systemverilog_loop_interpreter
            ->signal_value(*systemverilog_null_range_observed)
            .to_msb_string()
        == "00");

    const auto invalid_systemverilog_loops =
        fsim::frontend::parse_text(
            "invalid_procedural_loops.sv",
            R"(
module invalid_procedural_loops;
  logic dynamic_bound;
  initial begin
    for (int lane = dynamic_bound; lane < 1; lane++);
    for (int lane = 0; lane < dynamic_bound; lane++);
    for (int lane = 0; lane < 1000001; lane++);
    for (int lane = 0; lane < 1; lane++) lane = 2;
  end
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(invalid_systemverilog_loops.ok());
    const auto rejected_systemverilog_loops =
        fsim::elaboration::elaborate(
            invalid_systemverilog_loops.design,
            "sv:work.invalid_procedural_loops");
    assert(!rejected_systemverilog_loops.ok());
    for (const auto code :
         {"FSIM-ELAB-071", "FSIM-ELAB-072",
          "FSIM-ELAB-073", "FSIM-ELAB-074"}) {
        assert(
            has_diagnostic(
                rejected_systemverilog_loops, code));
    }

    const auto repeat_statements =
        fsim::frontend::parse_text(
            "repeat_statements.sv",
            R"(
module repeat_statements;
  logic [2:0] observed;
  initial begin
    observed = 3'b000;
    repeat (3) observed = observed + 1;
    repeat (0) observed = 3'b111;
  end
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(repeat_statements.ok());
    const auto elaborated_repeat_statements =
        fsim::elaboration::elaborate(
            repeat_statements.design,
            "sv:work.repeat_statements");
    assert(elaborated_repeat_statements.ok());
    auto repeat_interpreter =
        elaborated_repeat_statements.design
            ->create_interpreter();
    const auto repeat_result = repeat_interpreter->run();
    assert(
        repeat_result.status
        == fsim::runtime::RunStatus::completed);
    const auto repeat_observed =
        elaborated_repeat_statements.design
            ->find_signal("observed");
    assert(repeat_observed);
    assert(
        repeat_interpreter
            ->signal_value(*repeat_observed)
            .to_msb_string()
        == "011");

    const auto invalid_repeat_statements =
        fsim::frontend::parse_text(
            "invalid_repeat_statements.sv",
            R"(
module invalid_repeat_statements;
  logic dynamic_count;
  initial begin
    repeat (dynamic_count);
    repeat (-1);
    repeat (1000001);
  end
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(invalid_repeat_statements.ok());
    const auto rejected_repeat_statements =
        fsim::elaboration::elaborate(
            invalid_repeat_statements.design,
            "sv:work.invalid_repeat_statements");
    assert(!rejected_repeat_statements.ok());
    for (const auto code :
         {"FSIM-ELAB-073", "FSIM-ELAB-075",
          "FSIM-ELAB-076"}) {
        assert(
            has_diagnostic(
                rejected_repeat_statements, code));
    }

    const auto runtime_loop_statements =
        fsim::frontend::parse_text(
            "runtime_loop_statements.sv",
            R"(
module runtime_loop_statements;
  logic [2:0] observed;
  logic unknown_body;
  logic clock;
  initial begin
    observed = 3'b000;
    unknown_body = 1'b0;
    while (observed < 3) observed = observed + 1;
    while (1'bx) unknown_body = 1'b1;
  end
  initial begin
    clock = 1'b0;
    forever #1 clock = ~clock;
  end
  initial #3 $finish;
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(runtime_loop_statements.ok());
    const auto elaborated_runtime_loop_statements =
        fsim::elaboration::elaborate(
            runtime_loop_statements.design,
            "sv:work.runtime_loop_statements");
    if (!elaborated_runtime_loop_statements.ok()) {
        for (const auto& diagnostic :
             elaborated_runtime_loop_statements.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(elaborated_runtime_loop_statements.ok());
    auto runtime_loop_interpreter =
        elaborated_runtime_loop_statements.design
            ->create_interpreter();
    const auto runtime_loop_result =
        runtime_loop_interpreter->run();
    assert(
        runtime_loop_result.status
        == fsim::runtime::RunStatus::stopped);
    assert(runtime_loop_result.time == 3);
    const auto runtime_loop_observed =
        elaborated_runtime_loop_statements.design
            ->find_signal("observed");
    const auto runtime_loop_unknown_body =
        elaborated_runtime_loop_statements.design
            ->find_signal("unknown_body");
    assert(runtime_loop_observed && runtime_loop_unknown_body);
    assert(
        runtime_loop_interpreter
            ->signal_value(*runtime_loop_observed)
            .to_msb_string()
        == "011");
    assert(
        runtime_loop_interpreter
            ->signal_value(*runtime_loop_unknown_body)
            .to_msb_string()
        == "0");

    const auto systemverilog_loop_control =
        fsim::frontend::parse_text(
            "systemverilog_loop_control.sv",
            R"(
module systemverilog_loop_control;
  logic [3:0] static_result;
  logic [3:0] break_result;
  logic [3:0] runtime_result;
  logic [3:0] nested_result;
  logic [3:0] cursor;
  initial begin
    static_result = 4'b0000;
    repeat (3) begin
      static_result = static_result + 1;
      continue;
      static_result = static_result + 4;
    end
    break_result = 4'b0000;
    repeat (3) begin
      break_result = break_result + 1;
      break;
      break_result = break_result + 4;
    end
    runtime_result = 4'b0000;
    cursor = 4'b0000;
    while (cursor < 6) begin
      cursor = cursor + 1;
      if (cursor == 2) continue;
      if (cursor == 5) break;
      runtime_result = runtime_result + cursor;
    end
    nested_result = 4'b0000;
    for (int outer = 0; outer < 2; outer++) begin
      for (int inner = 0; inner < 3; inner++) begin
        nested_result = nested_result + 1;
        break;
      end
    end
  end
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(systemverilog_loop_control.ok());
    const auto elaborated_systemverilog_loop_control =
        fsim::elaboration::elaborate(
            systemverilog_loop_control.design,
            "sv:work.systemverilog_loop_control");
    if (!elaborated_systemverilog_loop_control.ok()) {
        for (const auto& diagnostic :
             elaborated_systemverilog_loop_control.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(elaborated_systemverilog_loop_control.ok());
    auto systemverilog_loop_control_interpreter =
        elaborated_systemverilog_loop_control.design
            ->create_interpreter();
    const auto systemverilog_loop_control_result =
        systemverilog_loop_control_interpreter->run();
    assert(
        systemverilog_loop_control_result.status
        == fsim::runtime::RunStatus::completed);
    for (const auto& [name, expected] :
         std::initializer_list<std::pair<
             std::string_view, std::string_view>>{
             {"static_result", "0011"},
             {"break_result", "0001"},
             {"runtime_result", "1000"},
             {"nested_result", "0010"}}) {
        const auto signal =
            elaborated_systemverilog_loop_control.design
                ->find_signal(name);
        assert(signal);
        const auto actual =
            systemverilog_loop_control_interpreter
                ->signal_value(*signal)
                .to_msb_string();
        if (actual != expected) {
            std::cerr << name << ": expected " << expected
                      << ", got " << actual << '\n';
        }
        assert(
            actual == expected);
    }

    const auto orphan_loop_control =
        fsim::frontend::parse_text(
            "orphan_loop_control.sv",
            R"(
module orphan_loop_control;
  initial break;
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(!orphan_loop_control.ok());
    const auto rejected_orphan_loop_control =
        fsim::elaboration::elaborate(
            orphan_loop_control.design,
            "sv:work.orphan_loop_control");
    assert(!rejected_orphan_loop_control.ok());
    assert(has_diagnostic(
        rejected_orphan_loop_control, "FSIM-ELAB-078"));

    const auto vhdl_runtime_loop =
        fsim::frontend::parse_text(
            "vhdl_runtime_loop.vhd",
            R"(
entity vhdl_runtime_loop is
  port (
    trigger : in std_logic;
    observed : out boolean
  );
end entity;
architecture rtl of vhdl_runtime_loop is
begin
  execute: process(trigger)
    variable keep_going : boolean := true;
    variable result : boolean := false;
  begin
    while keep_going loop
      result := true;
      keep_going := false;
    end loop;
    observed <= result;
  end process;
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(vhdl_runtime_loop.ok());
    const auto elaborated_vhdl_runtime_loop =
        fsim::elaboration::elaborate(
            vhdl_runtime_loop.design,
            "vhdl:work.vhdl_runtime_loop(rtl)");
    assert(elaborated_vhdl_runtime_loop.ok());
    auto vhdl_runtime_loop_interpreter =
        elaborated_vhdl_runtime_loop.design
            ->create_interpreter();
    const auto vhdl_runtime_loop_result =
        vhdl_runtime_loop_interpreter->run();
    assert(
        vhdl_runtime_loop_result.status
        == fsim::runtime::RunStatus::completed);
    const auto vhdl_runtime_loop_observed =
        elaborated_vhdl_runtime_loop.design
            ->find_signal("observed");
    assert(vhdl_runtime_loop_observed);
    assert(
        vhdl_runtime_loop_interpreter
            ->signal_value(*vhdl_runtime_loop_observed)
            .to_msb_string()
        == "1");

    const auto vhdl_loop_control =
        fsim::frontend::parse_text(
            "vhdl_loop_control.vhd",
            R"(
entity vhdl_loop_control is
  port (
    trigger : in std_logic;
    static_ok : out boolean;
    runtime_ok : out boolean;
    nested_ok : out boolean;
    targeted_ok : out boolean
  );
end entity;
architecture rtl of vhdl_loop_control is
begin
  execute: process(trigger)
    variable static_result : boolean := false;
    variable runtime_result : boolean := false;
    variable nested_result : boolean := false;
    variable targeted_result : boolean := false;
    variable keep_going : boolean := true;
    variable skipped : boolean := false;
  begin
    for lane in 0 to 4 loop
      next when lane = 0;
      static_result := true;
      exit;
    end loop;
    while keep_going loop
      if not skipped then
        skipped := true;
        next;
      end if;
      runtime_result := true;
      exit;
    end loop;
    for outer in 0 to 1 loop
      for inner in 0 to 2 loop
        nested_result := true;
        exit;
      end loop;
    end loop;
    outer_loop: for outer in 0 to 1 loop
      inner_loop: loop
        if outer = 0 then
          next outer_loop;
        end if;
        targeted_result := true;
        exit outer_loop;
      end loop inner_loop;
      targeted_result := false;
    end loop outer_loop;
    static_ok <= static_result;
    runtime_ok <= runtime_result;
    nested_ok <= nested_result;
    targeted_ok <= targeted_result;
  end process;
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    if (!vhdl_loop_control.ok()) {
        for (const auto& diagnostic :
             vhdl_loop_control.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(vhdl_loop_control.ok());
    const auto elaborated_vhdl_loop_control =
        fsim::elaboration::elaborate(
            vhdl_loop_control.design,
            "vhdl:work.vhdl_loop_control(rtl)");
    if (!elaborated_vhdl_loop_control.ok()) {
        for (const auto& diagnostic :
             elaborated_vhdl_loop_control.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(elaborated_vhdl_loop_control.ok());
    auto vhdl_loop_control_interpreter =
        elaborated_vhdl_loop_control.design
            ->create_interpreter();
    const auto vhdl_loop_control_result =
        vhdl_loop_control_interpreter->run();
    assert(
        vhdl_loop_control_result.status
        == fsim::runtime::RunStatus::completed);
    for (const auto name :
         {"static_ok", "runtime_ok", "nested_ok", "targeted_ok"}) {
        const auto signal =
            elaborated_vhdl_loop_control.design
                ->find_signal(name);
        assert(signal);
        assert(
            vhdl_loop_control_interpreter
                ->signal_value(*signal)
                .to_msb_string()
            == "1");
    }

    const auto orphan_vhdl_loop_target =
        fsim::frontend::parse_text(
            "orphan_vhdl_loop_target.vhd",
            R"(
entity orphan_vhdl_loop_target is
  port (trigger : in std_logic);
end entity;
architecture rtl of orphan_vhdl_loop_target is
begin
  execute: process(trigger)
  begin
    outer_loop: loop
      exit missing_loop;
    end loop outer_loop;
  end process;
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(!orphan_vhdl_loop_target.ok());
    const auto rejected_orphan_vhdl_loop_target =
        fsim::elaboration::elaborate(
            orphan_vhdl_loop_target.design,
            "vhdl:work.orphan_vhdl_loop_target(rtl)");
    assert(!rejected_orphan_vhdl_loop_target.ok());
    assert(has_diagnostic(
        rejected_orphan_vhdl_loop_target,
        "FSIM-ELAB-080"));

    const auto post_test_loop =
        fsim::frontend::parse_text(
            "post_test_loop.sv",
            R"(
module post_test_loop;
  logic [3:0] controlled;
  logic [3:0] executes_once;
  initial begin
    controlled = 4'b0000;
    do begin
      controlled = controlled + 1;
      if (controlled == 1) continue;
      if (controlled == 4) break;
      controlled = controlled + 1;
    end while (controlled < 6);
    executes_once = 4'b0000;
    do executes_once = executes_once + 1;
    while (1'b0);
  end
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(post_test_loop.ok());
    const auto elaborated_post_test_loop =
        fsim::elaboration::elaborate(
            post_test_loop.design,
            "sv:work.post_test_loop");
    if (!elaborated_post_test_loop.ok()) {
        for (const auto& diagnostic :
             elaborated_post_test_loop.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(elaborated_post_test_loop.ok());
    auto post_test_loop_interpreter =
        elaborated_post_test_loop.design
            ->create_interpreter();
    const auto post_test_loop_result =
        post_test_loop_interpreter->run();
    assert(
        post_test_loop_result.status
        == fsim::runtime::RunStatus::completed);
    for (const auto& [name, expected] :
         std::initializer_list<std::pair<
             std::string_view, std::string_view>>{
             {"controlled", "0100"},
             {"executes_once", "0001"}}) {
        const auto signal =
            elaborated_post_test_loop.design
                ->find_signal(name);
        assert(signal);
        assert(
            post_test_loop_interpreter
                ->signal_value(*signal)
                .to_msb_string()
            == expected);
    }

    const auto unconditional_vhdl_loop =
        fsim::frontend::parse_text(
            "unconditional_vhdl_loop.vhd",
            R"(
entity unconditional_vhdl_loop is
  port (
    trigger : in std_logic;
    observed : out boolean
  );
end entity;
architecture rtl of unconditional_vhdl_loop is
begin
  execute: process(trigger)
    variable skipped : boolean := false;
    variable result : boolean := false;
  begin
    loop
      if not skipped then
        skipped := true;
        next;
      end if;
      result := true;
      exit;
    end loop;
    observed <= result;
  end process;
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(unconditional_vhdl_loop.ok());
    const auto elaborated_unconditional_vhdl_loop =
        fsim::elaboration::elaborate(
            unconditional_vhdl_loop.design,
            "vhdl:work.unconditional_vhdl_loop(rtl)");
    assert(elaborated_unconditional_vhdl_loop.ok());
    auto unconditional_vhdl_loop_interpreter =
        elaborated_unconditional_vhdl_loop.design
            ->create_interpreter();
    const auto unconditional_vhdl_loop_result =
        unconditional_vhdl_loop_interpreter->run();
    assert(
        unconditional_vhdl_loop_result.status
        == fsim::runtime::RunStatus::completed);
    const auto unconditional_vhdl_loop_observed =
        elaborated_unconditional_vhdl_loop.design
            ->find_signal("observed");
    assert(unconditional_vhdl_loop_observed);
    assert(
        unconditional_vhdl_loop_interpreter
            ->signal_value(*unconditional_vhdl_loop_observed)
            .to_msb_string()
        == "1");

    const auto invalid_vhdl_runtime_loop =
        fsim::frontend::parse_text(
            "invalid_vhdl_runtime_loop.vhd",
            R"(
entity invalid_vhdl_runtime_loop is
  port (condition : in std_logic);
end entity;
architecture rtl of invalid_vhdl_runtime_loop is
begin
  execute: process(condition)
  begin
    while condition loop
      null;
    end loop;
  end process;
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(invalid_vhdl_runtime_loop.ok());
    const auto rejected_vhdl_runtime_loop =
        fsim::elaboration::elaborate(
            invalid_vhdl_runtime_loop.design,
            "vhdl:work.invalid_vhdl_runtime_loop(rtl)");
    assert(!rejected_vhdl_runtime_loop.ok());
    assert(
        has_diagnostic(
            rejected_vhdl_runtime_loop,
            "FSIM-ELAB-077"));

    const auto invalid_vhdl_condition =
        fsim::frontend::parse_text(
            "invalid_condition.vhd",
            R"(
entity invalid_condition is
  port (gate : in std_logic);
end entity;
architecture rtl of invalid_condition is
begin
  invalid: process(gate)
  begin
    if gate then
      null;
    end if;
  end process;
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(invalid_vhdl_condition.ok());
    const auto rejected_vhdl_condition =
        fsim::elaboration::elaborate(
            invalid_vhdl_condition.design,
            "vhdl:work.invalid_condition(rtl)");
    assert(!rejected_vhdl_condition.ok());
    assert(has_diagnostic(
        rejected_vhdl_condition, "FSIM-ELAB-048"));

    const auto invalid_vhdl_assertion =
        fsim::frontend::parse_text(
            "invalid_assertion.vhd",
            R"(
entity invalid_assertion is
  port (gate : in std_logic);
end entity;
architecture rtl of invalid_assertion is
begin
  invalid: process(gate)
  begin
    assert gate;
  end process;
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(invalid_vhdl_assertion.ok());
    const auto rejected_vhdl_assertion =
        fsim::elaboration::elaborate(
            invalid_vhdl_assertion.design,
            "vhdl:work.invalid_assertion(rtl)");
    assert(!rejected_vhdl_assertion.ok());
    assert(has_diagnostic(
        rejected_vhdl_assertion, "FSIM-ELAB-051"));

    const auto concurrent_vhdl_assertion =
        fsim::frontend::parse_text(
            "concurrent_assertion.vhd",
            R"(
entity concurrent_assertion is
end entity;
architecture rtl of concurrent_assertion is
  signal gate : boolean;
begin
  gate_check: assert gate
    report "concurrent gate failed" severity failure;
  driver: process
  begin
    wait for 1 ns;
    gate <= false;
    wait;
  end process;
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(concurrent_vhdl_assertion.ok());
    const auto elaborated_concurrent_vhdl_assertion =
        fsim::elaboration::elaborate(
            concurrent_vhdl_assertion.design,
            "vhdl:work.concurrent_assertion(rtl)");
    assert(elaborated_concurrent_vhdl_assertion.ok());
    assert(
        elaborated_concurrent_vhdl_assertion.design
            ->processes().size()
        == 2);
    const auto& concurrent_assertion_process =
        elaborated_concurrent_vhdl_assertion.design
            ->processes().front();
    assert(
        concurrent_assertion_process.name
            == "concurrent_assertion.gate_check"
        && concurrent_assertion_process.static_sensitivity.size()
            == 1);
    const auto concurrent_gate =
        elaborated_concurrent_vhdl_assertion.design
            ->find_signal("gate");
    assert(concurrent_gate);
    auto concurrent_assertion_interpreter =
        elaborated_concurrent_vhdl_assertion.design
            ->create_interpreter();
    concurrent_assertion_interpreter->deposit_signal(
        *concurrent_gate,
        fsim::runtime::PackedLogic4::from_msb_string("1"));
    bool saw_concurrent_assertion = false;
    try {
        (void)concurrent_assertion_interpreter->run();
    } catch (const fsim::runtime::simir::AssertionError& error) {
        saw_concurrent_assertion = true;
        assert(
            std::string_view{error.what()}.find(
                "concurrent gate failed")
            != std::string_view::npos);
        assert(
            error.source().path == "concurrent_assertion.vhd");
    }
    assert(saw_concurrent_assertion);

    const auto vector_assertion =
        fsim::frontend::parse_text(
            "vector_assertion.sv",
            R"(
module vector_assertion;
  initial begin
    assert (4'bx001);
    assert (4'bx000) else $error("vector condition failed");
  end
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(vector_assertion.ok());
    const auto elaborated_vector_assertion =
        fsim::elaboration::elaborate(
            vector_assertion.design,
            "sv:work.vector_assertion");
    assert(elaborated_vector_assertion.ok());
    auto vector_assertion_interpreter =
        elaborated_vector_assertion.design->create_interpreter();
    std::vector<std::string> vector_assertion_reports;
    vector_assertion_interpreter->set_report_hook(
        [&vector_assertion_reports](
            const fsim::runtime::simir::ProcessId,
            const std::string_view message,
            const fsim::runtime::simir::AssertionSeverity severity,
            const fsim::runtime::simir::SourceLocation&,
            const fsim::runtime::SimulationTick,
            const std::uint64_t) {
          assert(
              severity
              == fsim::runtime::simir::AssertionSeverity::error);
          vector_assertion_reports.emplace_back(message);
        });
    const auto vector_assertion_result =
        vector_assertion_interpreter->run();
    assert(
        vector_assertion_result.status
            == fsim::runtime::RunStatus::completed
        && vector_assertion_reports
            == std::vector<std::string>{
                "vector condition failed"});

    const auto conflicting_systemc_alias_source =
        fsim::frontend::parse_text(
            "conflicting_systemc_alias.sv",
            R"(
module conflict_host;
  logic first;
  logic second;
  conflict_placeholder u_conflict(
    .first(first),
    .second(second));
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(conflicting_systemc_alias_source.ok());
    const fsim::elaboration::SystemCInstanceDescription
        conflicting_systemc_instance{
            "conflict_host.u_conflict",
            "systemc:models.conflict",
            1000,
            0,
            {},
            {
                {1001,
                 "first",
                 systemc_logic,
                 fsim::frontend::PortDirection::Input,
                 1003},
                {1002,
                 "second",
                 systemc_logic,
                 fsim::frontend::PortDirection::Input,
                 1003},
            },
            {},
            {},
            {},
            {{1003, "shared"}},
            {{1003,
              "shared",
              systemc_logic,
              fsim::runtime::PackedLogic4::from_msb_string("0")}},
            {},
            {}};
    const std::vector<fsim::elaboration::Binding>
        conflicting_systemc_bindings{
            {"conflict_host.u_conflict",
             "systemc:models.conflict",
             std::nullopt},
        };
    const auto rejected_systemc_alias =
        fsim::elaboration::elaborate(
            conflicting_systemc_alias_source.design,
            "sv:work.conflict_host",
            conflicting_systemc_bindings,
            std::span{&conflicting_systemc_instance, 1});
    assert(!rejected_systemc_alias.ok());
    assert(has_diagnostic(
        rejected_systemc_alias, "FSIM-ELAB-BIND-046"));

    auto invalid_native_hierarchy = conflicting_systemc_instance;
    for (auto& port : invalid_native_hierarchy.ports) {
        port.bound_object = 0;
    }
    fsim::elaboration::SystemCInstanceDescription invalid_native_child;
    invalid_native_child.path = "conflict_host.u_conflict.leaf";
    invalid_native_child.target = "systemc:models.conflict";
    invalid_native_child.handle = 2000;
    invalid_native_child.parent = 9999;
    invalid_native_hierarchy.native_children.push_back(
        std::move(invalid_native_child));
    const std::array invalid_native_instances{
        invalid_native_hierarchy};
    const auto rejected_native_hierarchy =
        fsim::elaboration::elaborate(
            conflicting_systemc_alias_source.design,
            "sv:work.conflict_host",
            conflicting_systemc_bindings,
            invalid_native_instances);
    assert(!rejected_native_hierarchy.ok());
    assert(has_diagnostic(
        rejected_native_hierarchy, "FSIM-ELAB-BIND-047"));

    const auto named_event_source = fsim::frontend::parse_text(
        "named_event.sv",
        R"(
module named_event;
  event fired;
  logic observed;
  initial begin
    #1 -> fired;
    #1 ->> fired;
    #1 ->> #2 fired;
    #3 $finish;
  end
  initial begin
    @(fired);
    observed = 1'b1;
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(named_event_source.ok());
    const auto named_event_design =
        fsim::elaboration::elaborate(
            named_event_source.design, "sv:work.named_event");
    assert(named_event_design.ok());
    const auto event_signal =
        named_event_design.design->find_signal("fired");
    assert(event_signal);
    const auto named_event_interpreter =
        named_event_design.design->create_interpreter();
    assert(
        named_event_interpreter->signal_value(*event_signal)
            .to_msb_string()
        == "0");
    const auto& trigger_process =
        named_event_design.design->processes().front();
    assert(
        std::count_if(
            trigger_process.operations.begin(),
            trigger_process.operations.end(),
            [](const fsim::runtime::simir::Operation& operation) {
              return std::holds_alternative<
                  fsim::runtime::simir::WriteBlocking>(operation);
            })
        == 1);
    assert(
        std::count_if(
            trigger_process.operations.begin(),
            trigger_process.operations.end(),
            [](const fsim::runtime::simir::Operation& operation) {
              const auto* delayed =
                  std::get_if<fsim::runtime::simir::WriteAfter>(
                      &operation);
              return delayed != nullptr && delayed->delay == 2;
            })
        == 1);
    assert(
        std::count_if(
            trigger_process.operations.begin(),
            trigger_process.operations.end(),
            [](const fsim::runtime::simir::Operation& operation) {
              return std::holds_alternative<
                  fsim::runtime::simir::WriteUpdate>(operation);
            })
        == 1);
    const auto& waiting_process =
        named_event_design.design->processes().back();
    assert(
        std::count_if(
            waiting_process.operations.begin(),
            waiting_process.operations.end(),
            [](const fsim::runtime::simir::Operation& operation) {
              return std::holds_alternative<
                  fsim::runtime::simir::WaitOn>(operation);
            })
        == 1);

    const auto invalid_event_source = fsim::frontend::parse_text(
        "invalid_event.sv",
        R"(
module invalid_event;
  logic ordinary;
  initial begin
    -> missing;
    -> ordinary;
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(invalid_event_source.ok());
    const auto invalid_event_design =
        fsim::elaboration::elaborate(
            invalid_event_source.design, "sv:work.invalid_event");
    assert(!invalid_event_design.ok());
    assert(has_diagnostic(invalid_event_design, "FSIM-ELAB-100"));
    assert(has_diagnostic(invalid_event_design, "FSIM-ELAB-101"));

    const auto display_source = fsim::frontend::parse_text(
        "display.sv",
        R"(
module display;
  logic q;
  initial begin
    $display("first");
    $strobe("postponed");
    $write("continued");
    #1 $display;
    $write;
    $monitor("literal replacement");
    $monitor("q=%b", q);
    $monitoroff;
    $monitoron;
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(display_source.ok());
    const auto display_design =
        fsim::elaboration::elaborate(
            display_source.design, "sv:work.display");
    assert(display_design.ok());
    const auto& display_operations =
        display_design.design->processes().front().operations;
    std::vector<fsim::runtime::simir::Display> displays;
    for (const auto& operation : display_operations) {
        if (const auto* display =
                std::get_if<fsim::runtime::simir::Display>(
                    &operation)) {
            displays.push_back(*display);
        }
    }
    assert(
        displays.size() == 5
        && displays[0].text == "first"
        && displays[0].newline
        && !displays[0].postponed
        && displays[1].text == "postponed"
        && displays[1].newline
        && displays[1].postponed
        && displays[2].text == "continued"
        && !displays[2].newline
        && !displays[2].postponed
        && displays[3].text.empty()
        && displays[3].newline
        && displays[4].text.empty()
        && !displays[4].newline);
    std::vector<fsim::runtime::simir::MonitorInstall> monitors;
    for (const auto& operation : display_operations) {
        if (const auto* monitor =
                std::get_if<fsim::runtime::simir::MonitorInstall>(
                    &operation)) {
            monitors.push_back(*monitor);
        }
    }
    assert(
        monitors.size() == 2
        && monitors[0].values.empty()
        && monitors[0].trailing_text == "literal replacement");
    const auto& monitor = monitors[1];
    assert(
        monitor.values.size() == 1
        && monitor.values.front().kind
            == fsim::runtime::simir::MonitorValueKind::signal
        && monitor.values.front().format
            == fsim::runtime::simir::OutputFormat::binary
        && monitor.values.front().prefix == "q=");
    std::vector<bool> monitor_controls;
    for (const auto& operation : display_operations) {
        if (const auto* control =
                std::get_if<fsim::runtime::simir::MonitorControl>(
                    &operation)) {
            monitor_controls.push_back(control->enabled);
        }
    }
    assert(
        monitor_controls == std::vector<bool>({false, true}));

    const auto vhdl_record_source =
        fsim::frontend::parse_text(
            "record_execution.vhd",
            R"(
entity record_execution is
end entity;

architecture rtl of record_execution is
  type Packet_T is record
    Upper : std_logic_vector(3 downto 0);
    Lower : bit_vector(0 to 3);
    Flag  : boolean;
  end record Packet_T;
  signal source : Packet_T;
  signal result : Packet_T;
  signal equal_result : boolean;
begin
  drive_source : process
  begin
    source.Upper <= "ULH-";
    source.Lower <= "1010";
    source.Flag <= true;
    wait;
  end process;

  copy_record : process(source)
    variable local : Packet_T;
  begin
    local := source;
    local.Upper(1 downto 0) := source.Upper(3 downto 2);
    local.Lower(0) := source.Lower(3);
    result <= local;
  end process;

  equal_result <= result = source;
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(vhdl_record_source.ok());
    const auto vhdl_record_design =
        fsim::elaboration::elaborate(
            vhdl_record_source.design,
            "vhdl:work.record_execution(rtl)");
    if (!vhdl_record_design.ok()) {
        for (const auto& diagnostic :
             vhdl_record_design.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << " at "
                      << diagnostic.span.begin.line << ":"
                      << diagnostic.span.begin.column << '\n';
        }
    }
    assert(vhdl_record_design.ok());
    const auto record_source_signal =
        vhdl_record_design.design->find_signal("source");
    const auto record_result_signal =
        vhdl_record_design.design->find_signal("result");
    const auto record_equal_signal =
        vhdl_record_design.design->find_signal("equal_result");
    assert(
        record_source_signal && record_result_signal
        && record_equal_signal);
    const auto& record_source_info =
        vhdl_record_design.design->signals().at(
            *record_source_signal);
    assert(
        record_source_info.width == 9
        && record_source_info.source_domain
            == fsim::frontend::ValueDomain::Logic9
        && record_source_info.packed_members.size() == 3
        && record_source_info.packed_members[0].name == "upper"
        && record_source_info.packed_members[0].lsb_offset == 5
        && record_source_info.packed_members[1].name == "lower"
        && record_source_info.packed_members[1].lsb_offset == 1
        && record_source_info.packed_members[2].name == "flag"
        && record_source_info.packed_members[2].lsb_offset == 0);
    auto vhdl_record_interpreter =
        vhdl_record_design.design->create_interpreter();
    assert(
        vhdl_record_interpreter
            ->signal_value(*record_source_signal)
            .to_msb_string()
        == "UUUU00000");
    const auto vhdl_record_result =
        vhdl_record_interpreter->run();
    assert(
        vhdl_record_result.status
        == fsim::runtime::RunStatus::completed);
    assert(
        vhdl_record_interpreter
            ->signal_value(*record_source_signal)
            .to_msb_string()
        == "ULH-10101");
    assert(
        vhdl_record_interpreter
            ->signal_value(*record_result_signal)
            .to_msb_string()
        == "ULUL00101");
    assert(
        vhdl_record_interpreter
            ->signal_value(*record_equal_signal)
            .to_msb_string()
        == "0");

    const auto vhdl_record_aggregate_source =
        fsim::frontend::parse_text(
            "record_aggregate_execution.vhd",
            R"(
entity record_aggregate_execution is
end entity;

architecture rtl of record_aggregate_execution is
  type Packet_T is record
    Data : std_logic_vector(3 downto 0);
    Valid : boolean;
  end record Packet_T;
  signal source : Packet_T;
  signal result : Packet_T;
  signal conditional_result : Packet_T;
  signal equal_result : boolean;
  signal different_result : boolean;
begin
  drive_source : process
    variable local : Packet_T :=
      (Valid => true, Data => "ULH-");
  begin
    local := ("10Z-", false);
    source <= local;
    wait;
  end process;

  result <= (Data => "ULH-", others => true);
  conditional_result <=
    (Data => "01LH", Valid => true) when true else
    ("0000", false);
  equal_result <=
    result = (Valid => true, Data => "ULH-");
  different_result <=
    (Data => "ULH-", Valid => true) /= source;
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(vhdl_record_aggregate_source.ok());
    const auto vhdl_record_aggregate_design =
        fsim::elaboration::elaborate(
            vhdl_record_aggregate_source.design,
            "vhdl:work.record_aggregate_execution(rtl)");
    if (!vhdl_record_aggregate_design.ok()) {
        for (const auto& diagnostic :
             vhdl_record_aggregate_design.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << " at "
                      << diagnostic.span.begin.line << ":"
                      << diagnostic.span.begin.column << '\n';
        }
    }
    assert(vhdl_record_aggregate_design.ok());
    const auto aggregate_source_signal =
        vhdl_record_aggregate_design.design->find_signal("source");
    const auto aggregate_result_signal =
        vhdl_record_aggregate_design.design->find_signal("result");
    const auto aggregate_conditional_signal =
        vhdl_record_aggregate_design.design->find_signal(
            "conditional_result");
    const auto aggregate_equal_signal =
        vhdl_record_aggregate_design.design->find_signal(
            "equal_result");
    const auto aggregate_different_signal =
        vhdl_record_aggregate_design.design->find_signal(
            "different_result");
    assert(
        aggregate_source_signal
        && aggregate_result_signal
        && aggregate_conditional_signal
        && aggregate_equal_signal
        && aggregate_different_signal);
    std::size_t aggregate_insert_count = 0;
    for (const auto& process :
         vhdl_record_aggregate_design.design->processes()) {
        aggregate_insert_count += static_cast<std::size_t>(
            std::count_if(
                process.operations.begin(),
                process.operations.end(),
                [](const auto& operation) {
                    return std::holds_alternative<
                        fsim::runtime::simir::Insert>(operation);
                }));
    }
    assert(aggregate_insert_count == 14);
    auto aggregate_interpreter =
        vhdl_record_aggregate_design.design->create_interpreter();
    assert(
        aggregate_interpreter
            ->signal_value(*aggregate_source_signal)
            .to_msb_string()
        == "UUUU0");
    const auto aggregate_run = aggregate_interpreter->run();
    assert(
        aggregate_run.status
        == fsim::runtime::RunStatus::completed);
    assert(
        aggregate_interpreter
            ->signal_value(*aggregate_source_signal)
            .to_msb_string()
        == "10Z-0");
    assert(
        aggregate_interpreter
            ->signal_value(*aggregate_result_signal)
            .to_msb_string()
        == "ULH-1");
    assert(
        aggregate_interpreter
            ->signal_value(*aggregate_conditional_signal)
            .to_msb_string()
        == "01LH1");
    assert(
        aggregate_interpreter
            ->signal_value(*aggregate_equal_signal)
            .to_msb_string()
        == "1");
    assert(
        aggregate_interpreter
            ->signal_value(*aggregate_different_signal)
            .to_msb_string()
        == "1");

    const auto invalid_vhdl_record_aggregates =
        fsim::frontend::parse_text(
            "invalid_record_aggregate_execution.vhd",
            R"(
entity invalid_record_aggregate_execution is
end entity;

architecture rtl of invalid_record_aggregate_execution is
  type Packet_T is record
    Data : std_logic_vector(3 downto 0);
    Valid : boolean;
  end record Packet_T;
  signal result : Packet_T;
  signal flag : boolean;
begin
  invalid_forms : process
  begin
    flag <= (true, false);
    result <=
      (Unknown => "0000", Data => "0000", Valid => true);
    result <=
      (Data => "0000", DATA => "1111", Valid => true);
    result <= ("0000", true, false);
    result <= (Data => "0000");
    result <= (Data => "00", Valid => true);
    result <= (Data => "0000", Valid => 'X');
    wait;
  end process;
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(invalid_vhdl_record_aggregates.ok());
    const auto invalid_aggregate_design =
        fsim::elaboration::elaborate(
            invalid_vhdl_record_aggregates.design,
            "vhdl:work.invalid_record_aggregate_execution(rtl)");
    const auto has_aggregate_diagnostic =
        [&](const std::string_view code) {
            return std::ranges::any_of(
                invalid_aggregate_design.diagnostics,
                [&](const auto& diagnostic) {
                    return diagnostic.code == code;
                });
        };
    assert(
        !invalid_aggregate_design.ok()
        && has_aggregate_diagnostic("FSIM-ELAB-VHAGG-001")
        && has_aggregate_diagnostic("FSIM-ELAB-VHAGG-003")
        && has_aggregate_diagnostic("FSIM-ELAB-VHAGG-004")
        && has_aggregate_diagnostic("FSIM-ELAB-VHAGG-005")
        && has_aggregate_diagnostic("FSIM-ELAB-VHAGG-006")
        && has_aggregate_diagnostic("FSIM-ELAB-VHAGG-007"));

    auto malformed_aggregate_source =
        fsim::frontend::parse_text(
            "malformed_record_aggregate_metadata.vhd",
            R"(
entity malformed_record_aggregate_metadata is
end entity;
architecture rtl of malformed_record_aggregate_metadata is
  type Pair_T is record
    Left, Right : bit;
  end record Pair_T;
  signal value : Pair_T;
begin
  value <= ('0', '1');
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(malformed_aggregate_source.ok());
    auto& malformed_expression =
        malformed_aggregate_source.design.units.back()
            .concurrent_statements.front().value;
    assert(
        malformed_expression.kind
        == fsim::frontend::ExpressionKind::Aggregate);
    malformed_expression.aggregate_choices.pop_back();
    const auto malformed_aggregate_design =
        fsim::elaboration::elaborate(
            malformed_aggregate_source.design,
            "vhdl:work.malformed_record_aggregate_metadata(rtl)");
    assert(
        !malformed_aggregate_design.ok()
        && std::ranges::any_of(
            malformed_aggregate_design.diagnostics,
            [](const auto& diagnostic) {
                return diagnostic.code
                    == "FSIM-ELAB-VHAGG-002";
            }));

    const auto package_record_source =
        fsim::frontend::parse_text(
            "package_record_hierarchy.vhd",
            R"(
package Packet_Types is
  type Packet_T is record
    Data : std_logic_vector(3 downto 0);
    Valid : boolean;
  end record Packet_T;
end package;

use work.packet_types.all;
entity Record_Child is
  port (
    Source : in packet_t;
    Result : out work.packet_types.packet_t
  );
end entity;

architecture rtl of record_child is
begin
  result <= source;
end architecture;

use work.packet_types.packet_t;
entity Package_Record_Hierarchy is
end entity;

use work.packet_types.packet_t;
architecture rtl of package_record_hierarchy is
  signal source : packet_t;
  signal result : packet_types.packet_t;
  signal equal_result : boolean;
begin
  drive_source : process
  begin
    source.Data <= "ULH-";
    source.Valid <= true;
    wait;
  end process;

  child : entity work.record_child(rtl)
    port map (
      Source => source,
      Result => result
    );

  equal_result <= result = source;
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(package_record_source.ok());
    const auto package_record_design =
        fsim::elaboration::elaborate(
            package_record_source.design,
            "vhdl:work.package_record_hierarchy(rtl)");
    if (!package_record_design.ok()) {
        for (const auto& diagnostic :
             package_record_design.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << " at "
                      << diagnostic.span.begin.line << ":"
                      << diagnostic.span.begin.column << '\n';
        }
    }
    assert(package_record_design.ok());
    assert(
        package_record_design.design->specializations().size()
        == 2);
    const auto package_record_input =
        package_record_design.design->find_signal("source");
    const auto package_record_output =
        package_record_design.design->find_signal("result");
    const auto package_record_equal =
        package_record_design.design->find_signal("equal_result");
    assert(
        package_record_input && package_record_output
        && package_record_equal);
    assert(
        package_record_design.design
            ->signals()
            .at(*package_record_input)
            .packed_members.size()
        == 2);
    auto package_record_interpreter =
        package_record_design.design->create_interpreter();
    assert(
        package_record_interpreter
            ->signal_value(*package_record_input)
            .to_msb_string()
        == "UUUU0");
    const auto package_record_run =
        package_record_interpreter->run();
    assert(
        package_record_run.status
        == fsim::runtime::RunStatus::completed);
    assert(
        package_record_interpreter
            ->signal_value(*package_record_input)
            .to_msb_string()
        == "ULH-1");
    assert(
        package_record_interpreter
            ->signal_value(*package_record_output)
            .to_msb_string()
        == "ULH-1");
    assert(
        package_record_interpreter
            ->signal_value(*package_record_equal)
            .to_msb_string()
        == "1");

    const auto unknown_record_type =
        fsim::frontend::parse_text(
            "unknown_record_type.vhd",
            R"(
entity unknown_record_type is
  port (value : in missing_packet_t);
end entity;
architecture rtl of unknown_record_type is
  type missing_packet_t is record
    value : bit;
  end record;
begin
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(unknown_record_type.ok());
    const auto rejected_unknown_record_type =
        fsim::elaboration::elaborate(
            unknown_record_type.design,
            "vhdl:work.unknown_record_type(rtl)");
    assert(!rejected_unknown_record_type.ok());
    assert(has_diagnostic(
        rejected_unknown_record_type,
        "FSIM-ELAB-VHTYPE-001"));

    const auto missing_imported_record_type =
        fsim::frontend::parse_text(
            "missing_imported_record_type.vhd",
            R"(
package Available_Types is
  type Packet_T is record
    value : bit;
  end record;
end package;
use work.available_types.missing_t;
entity missing_imported_record_type is
  port (value : in missing_t);
end entity;
architecture rtl of missing_imported_record_type is
begin
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(missing_imported_record_type.ok());
    const auto rejected_missing_imported_record_type =
        fsim::elaboration::elaborate(
            missing_imported_record_type.design,
            "vhdl:work.missing_imported_record_type(rtl)");
    assert(!rejected_missing_imported_record_type.ok());
    assert(has_diagnostic(
        rejected_missing_imported_record_type,
        "FSIM-ELAB-PKG-003"));

    const auto missing_selected_record_type =
        fsim::frontend::parse_text(
            "missing_selected_record_type.vhd",
            R"(
package Selected_Types is
  type Packet_T is record
    value : bit;
  end record;
end package;
entity missing_selected_record_type is
end entity;
architecture rtl of missing_selected_record_type is
  signal value : selected_types.missing_t;
begin
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(missing_selected_record_type.ok());
    const auto rejected_missing_selected_record_type =
        fsim::elaboration::elaborate(
            missing_selected_record_type.design,
            "vhdl:work.missing_selected_record_type(rtl)");
    assert(!rejected_missing_selected_record_type.ok());
    assert(has_diagnostic(
        rejected_missing_selected_record_type,
        "FSIM-ELAB-VHTYPE-004"));

    const auto conflicting_record_types =
        fsim::frontend::parse_text(
            "conflicting_record_types.vhd",
            R"(
package First_Types is
  type Packet_T is record
    Value : bit;
  end record;
end package;
package Second_Types is
  type Packet_T is record
    Value : bit;
  end record;
end package;
use work.first_types.all;
use work.second_types.all;
entity conflicting_record_types is
  port (value : in packet_t);
end entity;
architecture rtl of conflicting_record_types is
begin
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(conflicting_record_types.ok());
    const auto rejected_conflicting_record_types =
        fsim::elaboration::elaborate(
            conflicting_record_types.design,
            "vhdl:work.conflicting_record_types(rtl)");
    assert(!rejected_conflicting_record_types.ok());
    assert(has_diagnostic(
        rejected_conflicting_record_types,
        "FSIM-ELAB-VHTYPE-003"));

    auto package_record_boundary =
        package_record_source.design;
    const auto package_record_sv_parent =
        fsim::frontend::parse_text(
            "package_record_parent.sv",
            R"(
module package_record_parent;
  logic [4:0] source;
  record_child child(.source(source));
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(package_record_sv_parent.ok());
    package_record_boundary.units.insert(
        package_record_boundary.units.end(),
        package_record_sv_parent.design.units.begin(),
        package_record_sv_parent.design.units.end());
    const std::vector<fsim::elaboration::Binding>
        package_record_binding{
            {"package_record_parent.child",
             "vhdl:work.record_child(rtl)",
             std::nullopt},
        };
    const auto rejected_package_record_boundary =
        fsim::elaboration::elaborate(
            package_record_boundary,
            "sv:work.package_record_parent",
            package_record_binding);
    assert(!rejected_package_record_boundary.ok());
    assert(has_diagnostic(
        rejected_package_record_boundary,
        "FSIM-ELAB-BIND-049"));

    const auto invalid_monitor_source =
        fsim::frontend::parse_text(
            "invalid_monitor.sv",
            R"(
module invalid_monitor;
  logic q;
  initial $monitor("%b", q + 1'b1);
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(invalid_monitor_source.ok());
    const auto invalid_monitor_design =
        fsim::elaboration::elaborate(
            invalid_monitor_source.design,
            "sv:work.invalid_monitor");
    assert(!invalid_monitor_design.ok());
    assert(
        has_diagnostic(
            invalid_monitor_design, "FSIM-ELAB-103"));

    const auto random_source = fsim::frontend::parse_text(
        "random.sv",
        R"(
module random_test;
  logic [31:0] a, b, c, d, e, f;
  initial begin
    a = $urandom;
    b = $urandom();
    c = $random;
    d = $random();
    e = $urandom_range(9);
    f = $urandom_range(3, 9);
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(random_source.ok());
    const auto random_design = fsim::elaboration::elaborate(
        random_source.design, "sv:work.random_test");
    assert(random_design.ok());
    std::vector<fsim::runtime::simir::RandomValue> random_operations;
    for (const auto& operation :
         random_design.design->processes().front().operations) {
        if (const auto* random =
                std::get_if<fsim::runtime::simir::RandomValue>(
                    &operation)) {
            random_operations.push_back(*random);
        }
    }
    assert(
        random_operations.size() == 6
        && random_operations[0].kind
            == fsim::runtime::simir::RandomKind::urandom
        && random_operations[1].kind
            == fsim::runtime::simir::RandomKind::urandom
        && random_operations[2].kind
            == fsim::runtime::simir::RandomKind::random
        && random_operations[3].kind
            == fsim::runtime::simir::RandomKind::random
        && random_operations[4].kind
            == fsim::runtime::simir::RandomKind::urandom_range
        && random_operations[4].maximum
        && !random_operations[4].minimum
        && random_operations[5].maximum
        && random_operations[5].minimum);

    const auto invalid_random_source =
        fsim::frontend::parse_text(
            "invalid_random.sv",
            R"(
module invalid_random;
  logic [31:0] q;
  initial begin
    q = $urandom(1);
    q = $urandom_range();
    q = $urandom_range(1, 2, 3);
  end
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(invalid_random_source.ok());
    const auto invalid_random_design =
        fsim::elaboration::elaborate(
            invalid_random_source.design,
            "sv:work.invalid_random");
    assert(!invalid_random_design.ok());
    assert(
        has_diagnostic(
            invalid_random_design, "FSIM-ELAB-104"));

    const auto verilog_random_source =
        fsim::frontend::parse_text(
            "verilog_random.v",
            R"(
module verilog_random;
  reg [31:0] q;
  initial q = $random;
endmodule
)",
            fsim::frontend::Language::Verilog2005);
    assert(verilog_random_source.ok());
    const auto verilog_random_design =
        fsim::elaboration::elaborate(
            verilog_random_source.design,
            "verilog:work.verilog_random");
    assert(verilog_random_design.ok());
    assert(std::ranges::any_of(
        verilog_random_design.design->processes().front().operations,
        [](const auto& operation) {
          const auto* random =
              std::get_if<fsim::runtime::simir::RandomValue>(
                  &operation);
          return random
              && random->kind
                  == fsim::runtime::simir::RandomKind::random;
        }));

    const auto invalid_verilog_random_source =
        fsim::frontend::parse_text(
            "invalid_verilog_random.v",
            R"(
module invalid_verilog_random;
  reg [31:0] q;
  initial q = $urandom;
endmodule
)",
            fsim::frontend::Language::Verilog2005);
    assert(invalid_verilog_random_source.ok());
    const auto invalid_verilog_random_design =
        fsim::elaboration::elaborate(
            invalid_verilog_random_source.design,
            "verilog:work.invalid_verilog_random");
    assert(!invalid_verilog_random_design.ok());
    assert(
        has_diagnostic(
            invalid_verilog_random_design, "FSIM-ELAB-104"));

    std::cout << "elaborator tests passed\n";
}
