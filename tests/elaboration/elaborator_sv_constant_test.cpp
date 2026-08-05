// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

#include "fsim/frontend/parser.hpp"

#include <cassert>
#include <iostream>
#include <ranges>
#include <string_view>

namespace fsim::tests::elaboration {
namespace {

const fsim::elaboration::SpecializationInfo* specialization_at(
    const fsim::elaboration::ElaboratedDesign& design,
    const std::string_view instance) {
    const auto found = std::ranges::find_if(
        design.specializations(),
        [&](const auto& specialization) {
            return specialization.instance == instance;
        });
    return found == design.specializations().end()
        ? nullptr
        : &*found;
}

std::string parameter_value(
    const fsim::elaboration::SpecializationInfo& specialization,
    const std::string_view name) {
    const auto found = std::ranges::find_if(
        specialization.parameter_values,
        [&](const auto& parameter) {
            return parameter.first == name;
        });
    assert(found != specialization.parameter_values.end());
    return found->second;
}

std::string parameter_identity(
    const fsim::elaboration::SpecializationInfo& specialization,
    const std::string_view name) {
    const auto found = std::ranges::find_if(
        specialization.parameter_identity_values,
        [&](const auto& parameter) {
            return parameter.first == name;
        });
    assert(found != specialization.parameter_identity_values.end());
    return found->second;
}

} // namespace

void test_systemverilog_typed_constants() {
    const auto parsed = fsim::frontend::parse_text(
        "typed-constants.sv",
        R"(
module typed_constant_child #(
  parameter longint unsigned VALUE = 0
) (
  output logic [63:0] q
);
  initial q = VALUE;
endmodule

module typed_constant_top #(
  parameter longint unsigned MAX_VALUE = 64'hffffffffffffffff,
  parameter longint unsigned MAX_MINUS_ONE = MAX_VALUE - 1,
  parameter longint unsigned LOGICAL_SHIFT = MAX_VALUE >> 60,
  parameter longint SIGNED_CAST = $signed(8'hff),
  parameter logic [7:0] CONCAT_VALUE = {4'ha, 4'h5},
  parameter logic [7:0] REPEAT_VALUE = {2{4'hc}},
  parameter logic [7:0] MIXED_WIDTH = 4'hf + 8'h01,
  parameter logic [7:0] CONDITIONAL_VALUE =
      1'b0 ? 4'hf : 8'h81,
  parameter logic [3:0] UNKNOWN_VALUE = 4'b10x1,
  parameter logic UNKNOWN_FLAG = $isunknown(UNKNOWN_VALUE),
  parameter logic MEMBER_EXACT = 8'h15 inside {8'h14, 8'h15},
  parameter logic MEMBER_RANGE = 8'h15 inside {[8'h10:8'h1f]},
  parameter logic MEMBER_WILDCARD = 8'ha5 inside {8'b10xz_0101},
  parameter logic MEMBER_UNKNOWN = 8'bx001_0001 inside {8'b0001_0001},
  parameter logic MEMBER_REVERSED = 8'h15 inside {[8'h1f:8'h10]},
  parameter IMPLICIT_EIGHT = 8'hff,
  parameter IMPLICIT_SIXTEEN = 16'h00ff
) ();
  typedef enum logic [63:0] {
    ENUM_MAX = 64'hffffffffffffffff,
    ENUM_PREVIOUS = 64'hfffffffffffffffe
  } wide_enum_t;
  logic [63:0] max_q;
  logic [7:0] mixed_q;
  logic [15:0] signed_q;
  logic [3:0] unknown_q;
  logic [63:0] child_q;

  initial begin
    max_q = MAX_VALUE;
    mixed_q = 4'hf + 8'h01;
    signed_q = $signed(8'h80);
    unknown_q = UNKNOWN_VALUE;
  end

  typed_constant_child #(.VALUE(MAX_VALUE)) child(.q(child_q));

  generate
    if (MAX_VALUE) begin : generated_true
      localparam longint unsigned GENERATED_VALUE = MAX_VALUE - 2;
      logic [63:0] generated_q;
      initial generated_q = GENERATED_VALUE;
    end
    case (MAX_VALUE)
      64'hffffffffffffffff: begin : generated_case
        logic selected;
        initial selected = 1'b1;
      end
      default: begin : generated_default
        logic selected;
        initial selected = 1'b0;
      end
    endcase
  endgenerate
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(parsed.ok());
    const auto elaborated = fsim::elaboration::elaborate(
        parsed.design, "sv:work.typed_constant_top");
    if (!elaborated.ok()) {
        for (const auto& diagnostic : elaborated.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(elaborated.ok());

    const auto* top =
        specialization_at(*elaborated.design, "typed_constant_top");
    const auto* child = specialization_at(
        *elaborated.design, "typed_constant_top.child");
    assert(top != nullptr && child != nullptr);
    assert(
        parameter_value(*top, "MAX_VALUE")
        == "18446744073709551615");
    assert(
        parameter_value(*top, "MAX_MINUS_ONE")
        == "18446744073709551614");
    assert(parameter_value(*top, "LOGICAL_SHIFT") == "15");
    assert(parameter_value(*top, "SIGNED_CAST") == "-1");
    assert(parameter_value(*top, "CONCAT_VALUE") == "165");
    assert(parameter_value(*top, "REPEAT_VALUE") == "204");
    assert(parameter_value(*top, "MIXED_WIDTH") == "16");
    assert(parameter_value(*top, "CONDITIONAL_VALUE") == "129");
    assert(parameter_value(*top, "UNKNOWN_VALUE") == "4'b10x1");
    assert(parameter_value(*top, "UNKNOWN_FLAG") == "1");
    assert(parameter_value(*top, "MEMBER_EXACT") == "1");
    assert(parameter_value(*top, "MEMBER_RANGE") == "1");
    assert(parameter_value(*top, "MEMBER_WILDCARD") == "1");
    assert(parameter_value(*top, "MEMBER_UNKNOWN") == "1'bx");
    assert(parameter_value(*top, "MEMBER_REVERSED") == "0");
    assert(parameter_value(*top, "IMPLICIT_EIGHT") == "255");
    assert(parameter_value(*top, "IMPLICIT_SIXTEEN") == "255");
    assert(
        parameter_value(*top, "ENUM_MAX")
        == "18446744073709551615");
    assert(
        parameter_value(*top, "ENUM_PREVIOUS")
        == "18446744073709551614");
    assert(
        parameter_value(*child, "VALUE")
        == "18446744073709551615");

    const auto eight_identity =
        parameter_identity(*top, "IMPLICIT_EIGHT");
    const auto sixteen_identity =
        parameter_identity(*top, "IMPLICIT_SIXTEEN");
    assert(eight_identity.starts_with("svconst-v1:w=8:s=0:"));
    assert(sixteen_identity.starts_with("svconst-v1:w=16:s=0:"));
    assert(eight_identity != sixteen_identity);
    assert(
        parameter_identity(*top, "MAX_VALUE")
            == parameter_identity(*child, "VALUE"));

    const auto max_q =
        elaborated.design->find_signal("typed_constant_top.max_q");
    const auto mixed_q =
        elaborated.design->find_signal("typed_constant_top.mixed_q");
    const auto signed_q =
        elaborated.design->find_signal("typed_constant_top.signed_q");
    const auto unknown_q =
        elaborated.design->find_signal("typed_constant_top.unknown_q");
    const auto child_q =
        elaborated.design->find_signal(
            "typed_constant_top.child.q");
    const auto generated_q =
        elaborated.design->find_signal(
            "typed_constant_top.generated_true.generated_q");
    const auto generated_selected =
        elaborated.design->find_signal(
            "typed_constant_top.generated_case.selected");
    assert(
        max_q && mixed_q && signed_q && unknown_q && child_q
        && generated_q && generated_selected);
    assert(
        !elaborated.design->find_signal(
            "typed_constant_top.generated_default.selected"));
    auto interpreter = elaborated.design->create_interpreter();
    interpreter->start();
    (void)interpreter->run();
    assert(
        interpreter->signal_value(*max_q).to_msb_string()
        == "1111111111111111111111111111111111111111111111111111111111111111");
    assert(
        interpreter->signal_value(*mixed_q).to_msb_string()
        == "00010000");
    assert(
        interpreter->signal_value(*signed_q).to_msb_string()
        == "1111111110000000");
    const auto unknown_value =
        interpreter->signal_value(*unknown_q).to_msb_string();
    if (unknown_value != "10X1") {
        std::cerr << "unexpected UNKNOWN_VALUE runtime result: "
                  << unknown_value << '\n';
    }
    assert(unknown_value == "10X1");
    assert(
        interpreter->signal_value(*child_q).to_msb_string()
        == "1111111111111111111111111111111111111111111111111111111111111111");
    assert(
        interpreter->signal_value(*generated_q).to_msb_string()
        == "1111111111111111111111111111111111111111111111111111111111111101");
    assert(
        interpreter->signal_value(*generated_selected).to_msb_string()
        == "1");

    const auto lossy = fsim::frontend::parse_text(
        "lossy-two-state-constant.sv",
        R"(
module lossy_two_state_constant #(
  parameter bit [3:0] BAD = 4'b10x1
) ();
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(lossy.ok());
    const auto rejected = fsim::elaboration::elaborate(
        lossy.design, "sv:work.lossy_two_state_constant");
    assert(!rejected.ok());
    assert(has_diagnostic(rejected, "FSIM-ELAB-SVCONST-001"));

    const auto boundary_parent = fsim::frontend::parse_text(
        "unsigned-boundary.sv",
        R"(
module unsigned_boundary #(
  parameter longint unsigned VALUE = 64'hffffffffffffffff
) ();
  foreign_integer #(.value(VALUE)) child();
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    const auto boundary_child = fsim::frontend::parse_text(
        "foreign-integer.vhd",
        R"(
entity foreign_integer is
  generic (value : natural := 0);
end entity;
architecture rtl of foreign_integer is
begin
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(boundary_parent.ok() && boundary_child.ok());
    auto boundary_design = boundary_parent.design;
    boundary_design.units.insert(
        boundary_design.units.end(),
        boundary_child.design.units.begin(),
        boundary_child.design.units.end());
    const std::vector<fsim::elaboration::Binding> bindings{
        {"unsigned_boundary.child",
         "vhdl:work.foreign_integer(rtl)",
         std::nullopt},
    };
    const auto rejected_boundary = fsim::elaboration::elaborate(
        boundary_design,
        "sv:work.unsigned_boundary",
        bindings);
    assert(!rejected_boundary.ok());
    assert(has_diagnostic(
        rejected_boundary, "FSIM-ELAB-GENERIC-004"));

    const auto scalar_parameters = fsim::frontend::parse_text(
        "scalar-parameters.sv",
        R"(
timeunit 1ns / 1ps;
package scalar_values;
  localparam real PACKAGE_BASE = 1.25;
  localparam realtime PACKAGE_PERIOD = 2.5ns;
endpackage

module scalar_parameter_child #(
  parameter real VALUE = 0.0,
  parameter time TICKS = 0
) ();
endmodule

module scalar_parameter_top #(
  parameter real BASE = PACKAGE_BASE,
  localparam real DERIVED = BASE + 2.0,
  parameter realtime PERIOD = PACKAGE_PERIOD,
  localparam time TICKS = PERIOD
) ();
  import scalar_values::*;
  scalar_parameter_child #(.VALUE(DERIVED), .TICKS(TICKS)) inherited();
  scalar_parameter_child #(.VALUE(4.5), .TICKS(1.5)) override();
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    if (!scalar_parameters.ok()) {
        for (const auto& diagnostic : scalar_parameters.diagnostics) {
            std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
        }
    }
    assert(scalar_parameters.ok());
    const auto scalar_elaborated = fsim::elaboration::elaborate(
        scalar_parameters.design, "sv:work.scalar_parameter_top");
    if (!scalar_elaborated.ok()) {
        for (const auto& diagnostic : scalar_elaborated.diagnostics) {
            std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
        }
    }
    assert(scalar_elaborated.ok());
    const auto* scalar_top = specialization_at(
        *scalar_elaborated.design, "scalar_parameter_top");
    const auto* inherited = specialization_at(
        *scalar_elaborated.design, "scalar_parameter_top.inherited");
    const auto* overridden = specialization_at(
        *scalar_elaborated.design, "scalar_parameter_top.override");
    assert(scalar_top && inherited && overridden);
    assert(parameter_value(*scalar_top, "PACKAGE_BASE") == "1.25");
    assert(parameter_value(*scalar_top, "BASE") == "1.25");
    assert(parameter_value(*scalar_top, "DERIVED") == "3.25");
    assert(parameter_value(*scalar_top, "PERIOD") == "2.5");
    assert(parameter_value(*scalar_top, "TICKS") == "3");
    assert(parameter_value(*inherited, "VALUE") == "3.25");
    assert(parameter_value(*inherited, "TICKS") == "3");
    assert(parameter_value(*overridden, "VALUE") == "4.5");
    assert(parameter_value(*overridden, "TICKS") == "2");
    assert(parameter_identity(*scalar_top, "BASE").starts_with("svscalar-v1:"));
    assert(parameter_identity(*scalar_top, "BASE")
           != parameter_identity(*scalar_top, "DERIVED"));
    assert(parameter_identity(*inherited, "VALUE")
           != parameter_identity(*overridden, "VALUE"));

    const auto chandle_parameters = fsim::frontend::parse_text(
        "chandle-parameters.sv",
        R"(
module chandle_parameter_top #(
  parameter chandle EMPTY = null,
  parameter chandle ALIAS = EMPTY,
  parameter bit IS_NULL = (ALIAS == null),
  parameter chandle CAST_NULL = chandle'(null)
) ();
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(chandle_parameters.ok());
    const auto chandle_elaborated = fsim::elaboration::elaborate(
        chandle_parameters.design, "sv:work.chandle_parameter_top");
    if (!chandle_elaborated.ok()) {
        for (const auto& diagnostic : chandle_elaborated.diagnostics) {
            std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
        }
    }
    assert(chandle_elaborated.ok());
    const auto* chandle_top = specialization_at(
        *chandle_elaborated.design, "chandle_parameter_top");
    assert(chandle_top != nullptr);
    assert(parameter_value(*chandle_top, "EMPTY") == "null");
    assert(parameter_value(*chandle_top, "ALIAS") == "null");
    assert(parameter_value(*chandle_top, "IS_NULL") == "1");
    assert(parameter_value(*chandle_top, "CAST_NULL") == "null");
    assert(parameter_identity(*chandle_top, "EMPTY").starts_with(
        "svscalar-v1:"));
    assert(parameter_identity(*chandle_top, "EMPTY")
           == parameter_identity(*chandle_top, "ALIAS"));

    const auto invalid_chandle_actual = fsim::frontend::parse_text(
        "invalid-chandle-actual.sv",
        R"(
module chandle_actual_child #(parameter chandle HANDLE = null) ();
endmodule
module invalid_chandle_actual_top;
  chandle_actual_child #(.HANDLE(1)) child();
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(invalid_chandle_actual.ok());
    const auto rejected_chandle_actual = fsim::elaboration::elaborate(
        invalid_chandle_actual.design,
        "sv:work.invalid_chandle_actual_top");
    assert(!rejected_chandle_actual.ok());
    assert(has_diagnostic(
        rejected_chandle_actual, "FSIM-ELAB-PARAM-004"));

    const auto invalid_dependency = fsim::frontend::parse_text(
        "scalar-parameter-forward.sv",
        R"(
module scalar_parameter_forward #(
  parameter real FIRST = LATER + 1.0,
  parameter real LATER = 2.0
) ();
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(invalid_dependency.ok());
    const auto rejected_dependency = fsim::elaboration::elaborate(
        invalid_dependency.design, "sv:work.scalar_parameter_forward");
    assert(!rejected_dependency.ok());
    assert(has_diagnostic(rejected_dependency, "FSIM-ELAB-PARAM-005"));

    const auto scalar_profiles = fsim::frontend::parse_text(
        "scalar-profiles.sv",
        R"(
module scalar_profile_child(
  input var real source,
  output var shortreal narrowed,
  inout wire time ticks,
  input chandle foreign
);
  function automatic real scale(
      input real value,
      input realtime factor = 1.0);
    return value;
  endfunction
  task automatic adjust(
      ref real value,
      input time delay = 2);
  endtask
endmodule

module scalar_profile_top;
  wire real source;
  shortreal narrowed;
  wire time ticks;
  chandle foreign;
  scalar_profile_child child(source, narrowed, ticks, foreign);
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(scalar_profiles.ok());
    const auto profile_elaborated = fsim::elaboration::elaborate(
        scalar_profiles.design, "sv:work.scalar_profile_top");
    if (!profile_elaborated.ok()) {
        for (const auto& diagnostic : profile_elaborated.diagnostics) {
            std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
        }
    }
    assert(profile_elaborated.ok());
    const auto source_signal = profile_elaborated.design->find_signal(
        "scalar_profile_top.source");
    const auto narrow_signal = profile_elaborated.design->find_signal(
        "scalar_profile_top.narrowed");
    const auto tick_signal = profile_elaborated.design->find_signal(
        "scalar_profile_top.ticks");
    const auto foreign_signal = profile_elaborated.design->find_signal(
        "scalar_profile_top.foreign");
    assert(source_signal && narrow_signal && tick_signal && foreign_signal);
    const auto& profile_signals = profile_elaborated.design->signals();
    assert(profile_signals[*source_signal].systemverilog_scalar
           == fsim::frontend::SystemVerilogScalarKind::Real);
    assert(profile_signals[*narrow_signal].systemverilog_scalar
           == fsim::frontend::SystemVerilogScalarKind::ShortReal);
    assert(profile_signals[*tick_signal].systemverilog_scalar
           == fsim::frontend::SystemVerilogScalarKind::Time);
    assert(profile_signals[*foreign_signal].systemverilog_scalar
           == fsim::frontend::SystemVerilogScalarKind::Chandle);

    const auto mismatched_profile = fsim::frontend::parse_text(
        "scalar-profile-mismatch.sv",
        R"(
module scalar_profile_mismatch_child(
  input shortreal value,
  input chandle foreign);
endmodule
module scalar_profile_mismatch_top;
  real value;
  real foreign;
  scalar_profile_mismatch_child child(value, foreign);
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(mismatched_profile.ok());
    const auto rejected_profile = fsim::elaboration::elaborate(
        mismatched_profile.design, "sv:work.scalar_profile_mismatch_top");
    assert(!rejected_profile.ok());
    assert(has_diagnostic(rejected_profile, "FSIM-ELAB-BIND-019"));

    const auto scalar_negative_matrix = fsim::frontend::parse_text(
        "scalar-negative-matrix.sv",
        R"(
module scalar_conversion_overflow;
  shortreal value;
  initial value = 1.0e400;
endmodule

module scalar_unsupported_operator;
  real left;
  real right;
  real result;
  initial result = left % right;
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(scalar_negative_matrix.ok());
    const auto rejected_scalar_conversion = fsim::elaboration::elaborate(
        scalar_negative_matrix.design,
        "sv:work.scalar_conversion_overflow");
    assert(!rejected_scalar_conversion.ok());
    assert(has_diagnostic(
        rejected_scalar_conversion, "FSIM-ELAB-SVSCALAR-001"));
    const auto rejected_scalar_operator = fsim::elaboration::elaborate(
        scalar_negative_matrix.design,
        "sv:work.scalar_unsupported_operator");
    assert(!rejected_scalar_operator.ok());
    assert(has_diagnostic(
        rejected_scalar_operator, "FSIM-ELAB-SVSCALAR-002"));
}

} // namespace fsim::tests::elaboration
