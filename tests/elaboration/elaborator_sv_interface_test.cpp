// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

#include <algorithm>

namespace fsim::tests::elaboration {

void test_systemverilog_interfaces() {
  auto invalid = fsim::frontend::parse_text(
      "invalid-interfaces.sv",
      R"(
interface a_if;
  logic value;
  function automatic logic sample(); return value; endfunction
  modport reader(input value);
  modport provider(export function sample);
endinterface
interface b_if;
  logic value;
  modport reader(input value);
endinterface
module wrong_type(a_if.reader bus); endmodule
module wrong_view(a_if.missing bus); endmodule
module input_writer(a_if.reader bus);
  initial bus.value = 1'b1;
endmodule
module missing_export(a_if.provider bus); endmodule
module invalid_interfaces_top;
  a_if a();
  b_if b();
  wrong_type type_error(.bus(b));
  wrong_view view_error(.bus(a));
  input_writer write_error(.bus(a));
  missing_export export_error(.bus(a));
  wrong_type expression_error(.bus(a.value + 1'b1));
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(invalid.ok());
  const auto result = fsim::elaboration::elaborate(
      invalid.design, "sv:work.invalid_interfaces_top");
  assert(!result.ok());
  for (const auto code : {
           "FSIM-ELAB-SVIFACE-001",
           "FSIM-ELAB-SVIFACE-003",
           "FSIM-ELAB-SVIFACE-004",
           "FSIM-ELAB-SVIFACE-006",
           "FSIM-ELAB-SVIFACE-009"}) {
    assert(has_diagnostic(result, code));
  }

  auto malformed = fsim::frontend::parse_text(
      "malformed-interface-hir.sv",
      R"(
interface malformed_if;
  logic value;
  function automatic logic sample(); return value; endfunction
  modport view(input value, import function sample);
endinterface
module malformed_user(malformed_if.view bus); endmodule
module malformed_interface_top;
  malformed_if link();
  malformed_user child(.bus(link));
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(malformed.ok());
  const auto interface = std::ranges::find_if(
      malformed.design.units,
      [](const auto& unit) {
        return unit.kind
            == fsim::frontend::UnitKind::SystemVerilogInterface;
      });
  assert(interface != malformed.design.units.end());
  auto& members = interface->systemverilog_modports.front().members;
  auto missing_signal = members.front();
  missing_signal.name = "missing_signal";
  members.push_back(std::move(missing_signal));
  auto missing_callable = members[1];
  missing_callable.name = "missing_callable";
  members.push_back(std::move(missing_callable));
  members.push_back(members[1]);
  const auto malformed_result = fsim::elaboration::elaborate(
      malformed.design, "sv:work.malformed_interface_top");
  assert(!malformed_result.ok());
  for (const auto code : {
           "FSIM-ELAB-SVIFACE-005",
           "FSIM-ELAB-SVIFACE-007",
           "FSIM-ELAB-SVIFACE-008"}) {
    assert(has_diagnostic(malformed_result, code));
  }
}

}  // namespace fsim::tests::elaboration
