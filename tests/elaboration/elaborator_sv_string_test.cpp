// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

namespace fsim::tests::elaboration {

void test_systemverilog_string_constants() {
    const auto parsed = fsim::frontend::parse_text(
        "sv-string-constants.sv",
        R"(
package string_pkg;
  localparam string PACKAGE_LABEL = "package";
endpackage

module string_child #(
  parameter string LABEL = "base",
  parameter string DECORATED = {LABEL, "!"},
  parameter PICK = 1,
  parameter string CHOICE = PICK ? DECORATED : "no",
  parameter string BYTES = "A\000B",
  parameter MATCH = LABEL == "base",
  parameter DIFFERENT = LABEL != "base",
  parameter string COMPARED = DIFFERENT ? "different" : LABEL
) ();
  initial begin
    $display(LABEL);
    $write("%s", DECORATED);
    $strobe(CHOICE);
    $info(LABEL);
    assert (1'b0) else $warning(DECORATED);
  end
  if (LABEL == "go") begin : chosen
    localparam string LOCAL_LABEL = {LABEL, "-local"};
    logic selected;
    initial selected = 1'b1;
  end
  case (LABEL)
    "go": begin : case_chosen
      logic case_selected;
      initial case_selected = 1'b1;
    end
    default: begin : case_other
      logic case_selected;
      initial case_selected = 1'b0;
    end
  endcase
endmodule

module package_string_child #(
  parameter string LABEL = string_pkg::PACKAGE_LABEL
) ();
endmodule

module string_wrapper #(
  parameter string NAME = "wrapper"
) ();
  string_child #(.LABEL(NAME)) nested();
endmodule

module string_top;
  string_child defaults();
  string_child #(
    .LABEL("go"),
    .DECORATED({"go", "?"}),
    .PICK(0),
    .CHOICE("manual")
  ) selected();
  string_child #("positional") positional();
  string_wrapper #(.NAME("nested")) wrapper();
  package_string_child package_child();
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(parsed.ok());
    const auto elaborated = fsim::elaboration::elaborate(
        parsed.design, "sv:work.string_top");
    if (!elaborated.ok()) {
        for (const auto& diagnostic : elaborated.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(elaborated.ok());
    assert(elaborated.design->specializations().size() == 7);

    const auto find_specialization =
        [&](const std::string_view instance) {
          return std::ranges::find_if(
              elaborated.design->specializations(),
              [&](const auto& specialization) {
                return specialization.instance == instance;
              });
        };
    const auto defaults =
        find_specialization("string_top.defaults");
    const auto selected =
        find_specialization("string_top.selected");
    const auto positional =
        find_specialization("string_top.positional");
    const auto wrapper =
        find_specialization("string_top.wrapper");
    const auto nested =
        find_specialization("string_top.wrapper.nested");
    const auto package_child =
        find_specialization("string_top.package_child");
    const auto end = elaborated.design->specializations().end();
    assert(
        defaults != end && selected != end && positional != end
        && wrapper != end && nested != end
        && package_child != end);
    const auto dump_values = [](const auto& specialization) {
      for (const auto& [name, value] :
           specialization.parameter_values) {
        std::cerr << specialization.instance << ": "
                  << name << '=' << value << '\n';
      }
    };
    if (defaults->parameter_values.size() != 8
        || selected->parameter_values.size() != 8) {
      dump_values(*defaults);
      dump_values(*selected);
    }
    assert((
        defaults->parameter_values.size() == 8
        && defaults->parameter_values[0]
            == std::pair<std::string, std::string>{
                "LABEL", "\"base\""}
        && defaults->parameter_values[1]
            == std::pair<std::string, std::string>{
                "DECORATED", "\"base!\""}
        && defaults->parameter_values[3]
            == std::pair<std::string, std::string>{
                "CHOICE", "\"base!\""}
        && defaults->parameter_values[4].second
            == "\"A\\000B\""
        && defaults->parameter_identity_values[4].second
            .ends_with("hex=410042")
        && defaults->parameter_values[5].second == "1"
        && defaults->parameter_values[6].second == "0"
        && defaults->parameter_values[7].second == "\"base\""
        && defaults->parameter_identity_values[0].second
            .starts_with("svstring-v1;")
        && selected->parameter_values[0].second == "\"go\""
        && selected->parameter_values[1].second == "\"go?\""
        && selected->parameter_values[3].second == "\"manual\""
        && selected->parameter_values[5].second == "0"
        && selected->parameter_values[6].second == "1"
        && selected->parameter_values[7].second == "\"different\""
        && positional->parameter_values[0].second
            == "\"positional\""
        && wrapper->parameter_values[0].second == "\"nested\""
        && nested->parameter_values[0].second == "\"nested\""
        && package_child->parameter_values[0].second
            == "\"package\""));
    const auto selected_signal =
        elaborated.design->find_signal(
            "string_top.selected.chosen.selected");
    assert(selected_signal);
    const auto case_selected_signal =
        elaborated.design->find_signal(
            "string_top.selected.case_chosen.case_selected");
    assert(case_selected_signal);

    const auto invalid_parsed = fsim::frontend::parse_text(
        "sv-string-invalid.sv",
        R"(
module integral_child #(parameter VALUE = 1) ();
endmodule
module string_child #(parameter string LABEL = "ok") ();
endmodule
module bad_default #(parameter string LABEL = 7) ();
endmodule
module bad_operator #(
  parameter string LABEL = "left" + "right"
) ();
endmodule
module bad_cycle;
  localparam string A = B;
  localparam string B = A;
endmodule
module bad_output #(parameter VALUE = 1) ();
  initial $info(VALUE);
endmodule
module invalid_top;
  integral_child #(.VALUE("wrong")) integral_mismatch();
  string_child #(.LABEL(7)) string_mismatch();
  bad_default default_mismatch();
  bad_operator operator_mismatch();
  bad_cycle cycle();
  bad_output output_mismatch();
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(invalid_parsed.ok());
    const auto invalid = fsim::elaboration::elaborate(
        invalid_parsed.design, "sv:work.invalid_top");
    assert(
        !invalid.ok()
        && has_diagnostic(
            invalid, "FSIM-ELAB-SVSTRING-001")
        && has_diagnostic(
            invalid, "FSIM-ELAB-SVSTRING-002")
        && has_diagnostic(
            invalid, "FSIM-ELAB-SVSTRING-003"));

    auto boundary_vhdl = fsim::frontend::parse_text(
        "sv-string-boundary.vhd",
        R"(
entity string_boundary_top is
end entity;
architecture rtl of string_boundary_top is
  signal value : bit;
begin
  child: foreign_string_child
    generic map (LABEL => 1)
    port map (value => value);
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    auto boundary_sv = fsim::frontend::parse_text(
        "sv-string-boundary.sv",
        R"(
module foreign_string_child #(
  parameter string LABEL = "default"
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
        {"string_boundary_top.child",
         "sv:work.foreign_string_child",
         std::nullopt},
    }};
    const auto boundary = fsim::elaboration::elaborate(
        boundary_vhdl.design,
        "vhdl:work.string_boundary_top(rtl)",
        bindings);
    assert(
        !boundary.ok()
        && has_diagnostic(
            boundary, "FSIM-ELAB-SVSTRING-004"));
}

} // namespace fsim::tests::elaboration
