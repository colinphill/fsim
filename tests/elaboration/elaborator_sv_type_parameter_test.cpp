// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

namespace fsim::tests::elaboration {

void test_systemverilog_type_parameters() {
    const auto parsed = fsim::frontend::parse_text(
        "sv-type-parameters.sv",
        R"(
package types_pkg;
  typedef logic [11:0] word_t;
  parameter type pkg_element_t = logic [2:0];
endpackage

module typed_copy #(
  parameter type T = logic [3:0]
) (
  input T input_value,
  output T output_value
);
  assign output_value = input_value;
endmodule

module typed_wrapper #(
  parameter type W = logic [1:0]
) (
  input W input_value,
  output W output_value
);
  W nested_value;
  typed_copy #(.T(W)) nested (
    .input_value(input_value),
    .output_value(nested_value)
  );
  assign output_value = nested_value;
endmodule

module dependent_type #(
  parameter WIDTH = 5,
  parameter type T = logic [WIDTH - 1:0],
  parameter T RESET = '0
) (
  input T input_value,
  output T output_value
);
  typedef T alias_t;
  localparam T LOCAL_RESET = RESET;
  alias_t local_value;
  assign local_value = input_value | LOCAL_RESET;
  assign output_value = local_value;
endmodule

module typed_top;
  typedef bit [7:0] byte_t;
  logic [3:0] default_input;
  logic [3:0] default_output;
  byte_t byte_input;
  byte_t byte_output;
  logic [5:0] direct_input;
  logic [5:0] direct_output;
  logic [11:0] package_input;
  logic [11:0] package_output;
  logic [2:0] package_parameter_input;
  logic [2:0] package_parameter_output;
  byte_t wrapped_input;
  byte_t wrapped_output;
  logic [5:0] dependent_input;
  logic [5:0] dependent_output;

  typed_copy default_copy (
    .input_value(default_input),
    .output_value(default_output)
  );
  typed_copy #(.T(byte_t)) byte_copy (
    .input_value(byte_input),
    .output_value(byte_output)
  );
  typed_copy #(.T(logic [5:0])) direct_copy (
    .input_value(direct_input),
    .output_value(direct_output)
  );
  typed_copy #(.T(types_pkg::word_t)) package_copy (
    .input_value(package_input),
    .output_value(package_output)
  );
  typed_copy #(.T(types_pkg::pkg_element_t)) package_parameter_copy (
    .input_value(package_parameter_input),
    .output_value(package_parameter_output)
  );
  typed_wrapper #(.W(byte_t)) wrapper (
    .input_value(wrapped_input),
    .output_value(wrapped_output)
  );
  dependent_type #(.WIDTH(6)) dependent (
    .input_value(dependent_input),
    .output_value(dependent_output)
  );
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(parsed.ok());
    const auto elaborated = fsim::elaboration::elaborate(
        parsed.design, "sv:work.typed_top");
    if (!elaborated.ok()) {
        for (const auto& diagnostic : elaborated.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(elaborated.ok());
    assert(elaborated.design->specializations().size() == 9);

    const auto find_specialization =
        [&](const std::string_view instance) {
          return std::ranges::find_if(
              elaborated.design->specializations(),
              [&](const auto& specialization) {
                return specialization.instance == instance;
              });
        };
    const auto default_copy =
        find_specialization("typed_top.default_copy");
    const auto byte_copy =
        find_specialization("typed_top.byte_copy");
    const auto direct_copy =
        find_specialization("typed_top.direct_copy");
    const auto package_copy =
        find_specialization("typed_top.package_copy");
    const auto wrapper =
        find_specialization("typed_top.wrapper");
    const auto package_parameter_copy =
        find_specialization(
            "typed_top.package_parameter_copy");
    const auto nested =
        find_specialization("typed_top.wrapper.nested");
    const auto dependent =
        find_specialization("typed_top.dependent");
    const auto end = elaborated.design->specializations().end();
    assert(
        default_copy != end && byte_copy != end
        && direct_copy != end && package_copy != end
        && wrapper != end && nested != end && dependent != end
        && package_parameter_copy != end);
    assert(
        default_copy->parameter_values.size() == 1
        && default_copy->parameter_values[0].second.starts_with(
            "sv-type-v1;")
        && byte_copy->parameter_values[0].second.starts_with(
            "sv-type-v1;")
        && default_copy->parameter_values[0].second
            != byte_copy->parameter_values[0].second
        && byte_copy->parameter_values[0].second
            == nested->parameter_values[0].second);
    assert(
        dependent->parameter_values.size() >= 3
        && dependent->parameter_values[0].first == "WIDTH"
        && dependent->parameter_values[0].second == "6"
        && dependent->parameter_values[1].first == "T"
        && dependent->parameter_values[1].second.starts_with(
            "sv-type-v1;")
        && dependent->parameter_values[2].first == "RESET");

    const auto expect_width =
        [&](const std::string_view path, const std::size_t width) {
          const auto signal = elaborated.design->find_signal(path);
          assert(signal);
          assert(
              elaborated.design->signals().at(*signal).width
              == width);
        };
    expect_width("default_output", 4);
    expect_width("byte_output", 8);
    expect_width("direct_output", 6);
    expect_width("package_output", 12);
    expect_width("package_parameter_output", 3);
    expect_width("wrapped_output", 8);
    expect_width("dependent_output", 6);

    const auto missing_parsed = fsim::frontend::parse_text(
        "sv-type-parameter-missing.sv",
        R"(
module required_type #(parameter type T) ();
endmodule
module missing_top;
  required_type child();
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(missing_parsed.ok());
    const auto missing = fsim::elaboration::elaborate(
        missing_parsed.design, "sv:work.missing_top");
    assert(
        !missing.ok()
        && has_diagnostic(
            missing, "FSIM-ELAB-SVTYPEPARAM-001"));

    const auto mismatch_parsed = fsim::frontend::parse_text(
        "sv-type-parameter-mismatch.sv",
        R"(
module value_child #(parameter WIDTH = 4) ();
endmodule
module type_child #(parameter type T = logic) ();
endmodule
module mismatch_top;
  value_child #(.WIDTH(logic)) wrong_value();
  type_child #(.T(7)) wrong_type();
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(mismatch_parsed.ok());
    const auto mismatch = fsim::elaboration::elaborate(
        mismatch_parsed.design, "sv:work.mismatch_top");
    assert(
        !mismatch.ok()
        && has_diagnostic(
            mismatch, "FSIM-ELAB-SVTYPEPARAM-002")
        && has_diagnostic(
            mismatch, "FSIM-ELAB-SVTYPEPARAM-003"));

    auto boundary_vhdl = fsim::frontend::parse_text(
        "sv-type-parameter-boundary.vhd",
        R"(
entity type_boundary_top is
end entity;
architecture rtl of type_boundary_top is
  signal value : bit;
begin
  child: foreign_child
    generic map (T => 1)
    port map (value => value);
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    auto boundary_sv = fsim::frontend::parse_text(
        "sv-type-parameter-boundary.sv",
        R"(
module foreign_child #(
  parameter type T = logic
) (
  input logic value
);
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(boundary_vhdl.ok() && boundary_sv.ok());
    for (auto& unit : boundary_sv.design.units) {
        boundary_vhdl.design.units.push_back(std::move(unit));
    }
    const std::array<fsim::elaboration::Binding, 1> bindings{{
        {"type_boundary_top.child",
         "sv:work.foreign_child",
         std::nullopt},
    }};
    const auto boundary = fsim::elaboration::elaborate(
        boundary_vhdl.design,
        "vhdl:work.type_boundary_top(rtl)",
        bindings);
    assert(
        !boundary.ok()
        && has_diagnostic(
            boundary, "FSIM-ELAB-SVTYPEPARAM-004"));
}

} // namespace fsim::tests::elaboration
