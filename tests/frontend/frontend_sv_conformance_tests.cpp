// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/frontend.hpp"

#include <algorithm>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>

namespace fsim::tests::frontend {

using namespace fsim::frontend;

namespace {

void require(const bool condition, const std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string{message});
  }
}

bool has_code(
    const ParseResult& result,
    const std::string_view code) {
  return std::ranges::any_of(
      result.diagnostics,
      [&](const Diagnostic& diagnostic) {
        return diagnostic.code == code;
      });
}

}  // namespace

void test_systemverilog_public_conformance_frontend() {
  // FSIM-CONFORMANCE CF-SV-PP-001 source=SRC-SV-TESTS expectation=accept
  auto preprocessed = preprocess_verilog(
      SourceText{
          "conformance-macros.sv",
          R"(`define JOIN(left,right) left``right
`define WIDTH(value=4) value
module `JOIN(macro_,unit) #(
  parameter int COUNT = `WIDTH()
) ();
endmodule : macro_unit
)"},
      Language::SystemVerilog2017);
  auto macro_result =
      parse_verilog(std::move(preprocessed.lexed), true);
  require(
      macro_result.ok()
          && macro_result.design.units.size() == 1
          && macro_result.design.units.front().name == "macro_unit"
          && macro_result.design.units.front().parameters.size() == 1
          && macro_result.design.units.front()
                 .parameters.front().default_value.text
              == "4",
      "CF-SV-PP-001 defaulted macro and token-paste expansion");

  // The declaration is independently authored from chapter-indexed
  // SRC-SV-TESTS and semantic boundary cases in SRC-SLANG/SRC-SURELOG.
  // FSIM-CONFORMANCE CF-SV-DECL-001 source=SRC-SV-TESTS expectation=accept
  // FSIM-CONFORMANCE CF-SV-TYPE-001 source=SRC-SLANG expectation=accept
  // FSIM-CONFORMANCE CF-SV-PARAM-001 source=SRC-SV-TESTS expectation=accept
  // FSIM-CONFORMANCE CF-SV-PKG-001 source=SRC-SURELOG expectation=accept
  // FSIM-CONFORMANCE CF-SV-IFACE-001 source=SRC-SURELOG expectation=accept
  // FSIM-CONFORMANCE CF-SV-GEN-001 source=SRC-SV-TESTS expectation=accept
  const auto parsed = parse_text(
      "public-conformance.sv",
      R"(package conformance_types;
  parameter int WIDTH = 4;
  typedef logic signed [WIDTH-1:0] word_t;
  typedef enum logic [1:0] {
    idle_state,
    active_state = 2
  } state_t;
endpackage : conformance_types

interface conformance_bus #(
  parameter int WIDTH = conformance_types::WIDTH
);
  logic [WIDTH-1:0] payload;
  modport producer(output payload),
          consumer(input payload);
endinterface : conformance_bus

macromodule conformance_leaf #(
  parameter type element_t = conformance_types::word_t,
  parameter int WIDTH = conformance_types::WIDTH
) (
  input logic enable,
  output element_t result
);
  if (WIDTH > 1) begin : selected_width
    initial result = enable;
  end else begin : scalar_width
    initial result = '0;
  end
endmodule : conformance_leaf

module \module  (output logic \wire  );
  import conformance_types::*;
  conformance_bus #(.WIDTH(WIDTH)) bus();
  conformance_leaf #(
    .element_t(word_t),
    .WIDTH(WIDTH)
  ) child(
    .enable(1'b1),
    .result(bus.payload)
  );
  assign \wire  = bus.payload[0];
endmodule : \module
)",
      Language::SystemVerilog2017);
  require(parsed.ok(), "public SystemVerilog frontend corpus must parse");
  require(
      parsed.design.units.size() == 4,
      "public corpus retains package, interface, macro module, and module");

  const auto* package = parsed.design.find(
      UnitKind::SystemVerilogPackage, "conformance_types");
  const auto* interface = parsed.design.find(
      UnitKind::SystemVerilogInterface, "conformance_bus");
  const auto* leaf = parsed.design.find(
      UnitKind::VerilogModule, "conformance_leaf");
  const auto* escaped = parsed.design.find(
      UnitKind::VerilogModule, "\\module");
  require(
      package != nullptr
          && package->parameters.size() == 3
          && package->type_aliases.size() == 2
          && package->type_aliases[0].type.is_signed
          && package->type_aliases[1].enum_literals.size() == 2,
      "CF-SV-TYPE/PARAM/PKG typed package declarations");
  require(
      interface != nullptr
          && interface->parameters.size() == 1
          && interface->signals.size() == 1
          && interface->systemverilog_modports.size() == 2
          && interface->systemverilog_modports[0]
                 .members.front().direction
              == PortDirection::Output
          && interface->systemverilog_modports[1]
                 .members.front().direction
              == PortDirection::Input,
      "CF-SV-IFACE-001 parameterized interface and views");
  require(
      leaf != nullptr
          && leaf->parameters.size() == 2
          && leaf->parameters[0].kind == ParameterKind::Type
          && leaf->ports.size() == 2
          && leaf->generate_regions.size() == 1
          && leaf->generate_regions.front().then_scope
              == "selected_width"
          && leaf->generate_regions.front().else_scope
              == "scalar_width",
      "CF-SV-DECL/PARAM/GEN macromodule typed specialization HIR");
  require(
      escaped != nullptr
          && escaped->ports.size() == 1
          && escaped->ports.front().name == "\\wire"
          && escaped->systemverilog_imports.size() == 1
          && escaped->instances.size() == 2,
      "CF-SV-DECL-001 escaped keyword identifiers retain exact spelling");

  // FSIM-CONFORMANCE CF-SV-PP-N01 source=SRC-SV-TESTS expectation=reject
  const auto bad_macro = preprocess_verilog(
      SourceText{
          "bad-conformance-macro.sv",
          R"(`define SELECT(first,second) first
`SELECT(only_one)
)"},
      Language::SystemVerilog2017);
  require(
      !bad_macro.ok()
          && std::ranges::any_of(
              bad_macro.lexed.diagnostics,
              [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-PP-030";
              }),
      "CF-SV-PP-N01 wrong macro arity is rejected exactly");

  // FSIM-CONFORMANCE CF-SV-DECL-N01 source=SRC-SV-TESTS expectation=reject
  const auto mismatched_module = parse_text(
      "mismatched-module-label.sv",
      "module opening; endmodule : closing\n",
      Language::SystemVerilog2017);
  require(
      !mismatched_module.ok()
          && has_code(mismatched_module, "FSIM-SV-SEM-129"),
      "CF-SV-DECL-N01 mismatched module end name is rejected");

  // FSIM-CONFORMANCE CF-SV-IFACE-N01 source=SRC-SLANG expectation=reject
  const auto mismatched_interface = parse_text(
      "mismatched-interface-label.sv",
      "interface opening; endinterface : closing\n",
      Language::SystemVerilog2017);
  require(
      !mismatched_interface.ok()
          && has_code(mismatched_interface, "FSIM-SV-SEM-129"),
      "CF-SV-IFACE-N01 mismatched interface end name is rejected");

  // FSIM-CONFORMANCE CF-SV-DECL-N02 source=SRC-SV-TESTS expectation=reject
  const auto reserved_identifier = parse_text(
      "reserved-module-name.sv",
      "module module; endmodule\n",
      Language::SystemVerilog2017);
  require(
      !reserved_identifier.ok()
          && has_code(reserved_identifier, "FSIM-SV-PARSE-001"),
      "CF-SV-DECL-N02 unescaped reserved identifier is rejected");
}

}  // namespace fsim::tests::frontend
