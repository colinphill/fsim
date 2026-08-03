// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

#include <algorithm>
#include <array>
#include <ranges>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::tests::elaboration {

namespace {

void require_elaboration(
    const fsim::elaboration::ElaborationResult& result,
    const std::string_view context) {
  if (result.ok()) {
    return;
  }
  std::cerr << context << " failed:\n";
  for (const auto& diagnostic : result.diagnostics) {
    std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
  }
  assert(false);
}

}  // namespace

void test_systemverilog_public_conformance_elaboration() {
  // These fixtures are independently authored from the bounded semantic
  // expectations identified by SRC-SV-TESTS, SRC-SURELOG, and SRC-SLANG.
  // FSIM-CONFORMANCE CF-SV-ELAB-P01 source=SRC-SV-TESTS expectation=accept
  // FSIM-CONFORMANCE CF-SV-ELAB-P02 source=SRC-SURELOG expectation=accept
  const auto parsed = fsim::frontend::parse_text(
      "public-conformance-elaboration.sv",
      R"(package conformance_config;
  parameter int WIDTH = 3;
  typedef logic [WIDTH-1:0] word_t;
endpackage : conformance_config

interface conformance_bus #(
  parameter int WIDTH = conformance_config::WIDTH
);
  logic [WIDTH-1:0] value;
  modport producer(output value),
          consumer(input value);
endinterface : conformance_bus

macromodule conformance_source #(
  parameter int WIDTH = conformance_config::WIDTH
) (
  output logic [WIDTH-1:0] value
);
  initial value = WIDTH;
endmodule : conformance_source

module conformance_sink(conformance_bus.consumer bus);
  logic observed;
  assign observed = bus.value[0];
endmodule : conformance_sink

module conformance_top;
  import conformance_config::*;
  logic [WIDTH-1:0] source_value;
  conformance_bus #(.WIDTH(WIDTH)) link();
  conformance_source #(.WIDTH(WIDTH)) source(.value(source_value));
  conformance_sink sink(.bus(link));
  for (genvar lane = 0; lane < 2; lane++) begin : lanes
    conformance_source #(.WIDTH(lane + 1)) child();
  end
endmodule : conformance_top
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(parsed.ok());
  const auto elaborated = fsim::elaboration::elaborate(
      parsed.design, "sv:work.conformance_top");
  require_elaboration(elaborated, "public SystemVerilog conformance");

  const auto link = elaborated.design->find_signal(
      "conformance_top.link.value");
  const auto generated_one = elaborated.design->find_signal(
      "conformance_top.lanes[0].child.value");
  const auto generated_two = elaborated.design->find_signal(
      "conformance_top.lanes[1].child.value");
  assert(link && generated_one && generated_two);
  assert(elaborated.design->signals().at(*link).width == 3);
  assert(elaborated.design->signals().at(*generated_one).width == 1);
  assert(elaborated.design->signals().at(*generated_two).width == 2);
  for (const auto& [path, width] :
       std::array{
           std::pair{
               std::string_view{"conformance_top.lanes[0].child"},
               std::string_view{"1"}},
           std::pair{
               std::string_view{"conformance_top.lanes[1].child"},
               std::string_view{"2"}}}) {
    const auto specialization = std::ranges::find_if(
        elaborated.design->specializations(),
        [&](const auto& candidate) {
          return candidate.instance == path;
        });
    assert(specialization != elaborated.design->specializations().end());
    assert(std::ranges::any_of(
        specialization->parameter_values,
        [&](const auto& parameter) {
          return parameter.first == "WIDTH" && parameter.second == width;
        }));
  }

  // FSIM-CONFORMANCE CF-SV-GEN-N01 source=SRC-SV-TESTS expectation=reject
  const auto dynamic_generate = fsim::frontend::parse_text(
      "dynamic-generate-conformance.sv",
      R"(module dynamic_generate(input logic select);
  if (select) begin : selected
    logic value;
  end
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(dynamic_generate.ok());
  const auto dynamic_generate_result = fsim::elaboration::elaborate(
      dynamic_generate.design, "sv:work.dynamic_generate");
  assert(
      !dynamic_generate_result.ok()
          && has_diagnostic(
              dynamic_generate_result, "FSIM-ELAB-GEN-001"));

  // FSIM-CONFORMANCE CF-SV-PARAM-N01 source=SRC-SLANG expectation=reject
  const auto missing_type = fsim::frontend::parse_text(
      "missing-type-parameter-conformance.sv",
      R"(module typed_child #(parameter type element_t) ();
endmodule
module missing_type_top;
  typed_child child();
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(missing_type.ok());
  const auto missing_type_result = fsim::elaboration::elaborate(
      missing_type.design, "sv:work.missing_type_top");
  assert(
      !missing_type_result.ok()
          && has_diagnostic(
              missing_type_result, "FSIM-ELAB-SVTYPEPARAM-001"));

  // FSIM-CONFORMANCE CF-SV-PKG-N01 source=SRC-SLANG expectation=reject
  const auto ambiguous_type = fsim::frontend::parse_text(
      "ambiguous-package-type-conformance.sv",
      R"(package first_types;
  typedef logic first_word_t;
endpackage
package second_types;
  typedef logic first_word_t;
endpackage
import first_types::first_word_t, second_types::first_word_t;
module ambiguous_type_top(output first_word_t value);
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(ambiguous_type.ok());
  const auto ambiguous_type_result = fsim::elaboration::elaborate(
      ambiguous_type.design, "sv:work.ambiguous_type_top");
  assert(
      !ambiguous_type_result.ok()
          && has_diagnostic(
              ambiguous_type_result, "FSIM-ELAB-SVTYPE-002"));

  // FSIM-CONFORMANCE CF-SV-IFACE-N02 source=SRC-SURELOG expectation=reject
  const auto wrong_interface = fsim::frontend::parse_text(
      "wrong-interface-conformance.sv",
      R"(interface expected_if;
  logic value;
  modport view(input value);
endinterface
interface other_if;
  logic value;
  modport view(input value);
endinterface
module expects_interface(expected_if.view bus);
endmodule
module wrong_interface_top;
  other_if link();
  expects_interface child(.bus(link));
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(wrong_interface.ok());
  const auto wrong_interface_result = fsim::elaboration::elaborate(
      wrong_interface.design, "sv:work.wrong_interface_top");
  assert(
      !wrong_interface_result.ok()
          && has_diagnostic(
              wrong_interface_result, "FSIM-ELAB-SVIFACE-003"));
}

void test_msvc_debug_elaboration_portability() {
  const std::string bom{"\xef\xbb\xbf"};
  const auto parsed = fsim::frontend::parse(
      fsim::frontend::SourceText{
          R"(C:\work tree\utf8-source\portable-elaboration.sv)",
          bom
              + "module portable_elaboration(output logic [3:0] value);\r\n"
                "  initial value = 4'h9;\r\n"
                "endmodule : portable_elaboration\r\n"},
      fsim::frontend::Language::SystemVerilog2017);
  assert(parsed.ok());
  const auto elaborated = fsim::elaboration::elaborate(
      parsed.design, "sv:work.portable_elaboration");
  require_elaboration(elaborated, "MSVC Debug source portability");
  const auto value =
      elaborated.design->find_signal("portable_elaboration.value");
  assert(value && elaborated.design->signals().at(*value).width == 4U);
  assert(elaborated.design->processes().size() == 1U);
}

}  // namespace fsim::tests::elaboration
