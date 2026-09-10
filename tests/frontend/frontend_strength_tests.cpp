// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/frontend.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace fsim::tests::frontend {
using namespace fsim::frontend;

namespace {
void require(const bool condition, const std::string_view message) {
  if (!condition) throw std::runtime_error(std::string{message});
}
}  // namespace

void test_verilog_strength_and_switch_primitives() {
  const auto strength_forms = parse_text(
      "strength_forms.v",
      R"(
module strength_forms;
  wire source;
  wire (strong0, weak1) initialized = source;
  wire result;
  trireg (large) retained;
  assign (pull0, weak1) #2 result = source;
endmodule
)", Language::Verilog2005);
  require(strength_forms.ok(), "Verilog strength forms must parse");
  const auto& unit = strength_forms.design.units.front();
  const auto initialized = std::ranges::find_if(
      unit.signals, [](const SignalDeclaration& declaration) {
        return declaration.name == "initialized";
      });
  const auto retained = std::ranges::find_if(
      unit.signals, [](const SignalDeclaration& declaration) {
        return declaration.name == "retained";
      });
  require(
      initialized != unit.signals.end() && initialized->drive_strength
          && initialized->drive_strength->zero == VerilogStrength::Strong
          && initialized->drive_strength->one == VerilogStrength::Weak
          && retained != unit.signals.end() && retained->charge_strength
          && retained->charge_strength->rank == VerilogStrength::Large
          && unit.concurrent_statements.size() == 2
          && unit.concurrent_statements[0].verilog_drive_strength
          && unit.concurrent_statements[1].verilog_drive_strength
          && unit.concurrent_statements[1].verilog_drive_strength->zero
              == VerilogStrength::Pull
          && unit.concurrent_statements[1].verilog_drive_strength->one
              == VerilogStrength::Weak,
      "drive and charge strengths retain canonical HIR ranks");

  const auto switches = parse_text(
      "switches.v",
      R"(
module switches;
  wire a, b, y, ncontrol, pcontrol;
  wire [1:0] va, vb;
  pullup (weak1) up(y);
  pulldown (strong0) down(y);
  nmos n(y, a, ncontrol);
  pmos p(y, a, pcontrol);
  rnmos rn(y, a, ncontrol);
  rpmos rp(y, a, pcontrol);
  cmos c(y, a, ncontrol, pcontrol);
  rcmos rc(y, a, ncontrol, pcontrol);
  tran t(a, b);
  rtran rt(a, b);
  tranif0 t0(a, b, ncontrol);
  tranif1 t1(a, b, ncontrol);
  rtranif0 rt0(a, b, ncontrol);
  rtranif1 rt1(a, b, ncontrol);
  rtran arrayed[1:0](va, vb);
endmodule
)", Language::Verilog2005);
  require(switches.ok(), "Verilog switch primitives must parse");
  const auto& statements =
      switches.design.units.front().concurrent_statements;
  require(
      statements.size() == 24 && statements[0].verilog_drive_strength
          && statements[0].verilog_drive_strength->one
              == VerilogStrength::Weak
          && statements[1].verilog_drive_strength->zero
              == VerilogStrength::Strong
          && statements[4].verilog_drive_strength
          && statements[4].verilog_drive_strength->zero
              == VerilogStrength::Pull
          && statements[8].label == "t$left"
          && statements[8].verilog_switch_driver
          && statements[8].verilog_switch_source.text == "b"
          && statements[9].label == "t$right"
          && statements[10].verilog_switch_resistive
          && statements[12].value.kind == ExpressionKind::Call
          && statements[12].verilog_switch_bidirectional
          && statements[12].verilog_switch_control.text == "ncontrol"
          && !statements[12].verilog_switch_active_high
          && statements[14].verilog_switch_active_high
          && statements[20].label == "arrayed[1]$left"
          && statements[21].label == "arrayed[1]$right"
          && statements[22].label == "arrayed[0]$left"
          && statements[23].label == "arrayed[0]$right"
          && statements[20].target.kind == ExpressionKind::Index
          && statements[20].target.operands[1].text == "1"
          && statements[22].target.operands[1].text == "0",
      "switch primitives and arrays lower to strength-qualified directional "
      "drivers");

  const auto invalid = parse_text(
      "invalid-strengths.v",
      R"(
module invalid_strengths;
  wire a, b, control;
  wire [1:0] narrow;
  logic (strong0, weak1) variable_strength;
  wire (large) misplaced_charge;
  and (highz0, highz1) invalid_highz(a, b, control);
  nmos (weak1, strong0) invalid_mos(a, b, control);
  pullup (weak0) invalid_pull(a);
  tran symbolic[control:0](a, b);
  tran enormous[2147483647:0](a, b);
  tran mismatched[3:0](narrow, b);
  tran [1:0](a, b);
endmodule
)", Language::SystemVerilog2017);
  const auto has_code = [&](const std::string_view code) {
    return std::ranges::any_of(
        invalid.diagnostics, [&](const auto& diagnostic) {
          return diagnostic.code == code;
        });
  };
  require(
      !invalid.ok() && has_code("FSIM-SV-SEM-150")
          && has_code("FSIM-SV-SEM-151")
          && has_code("FSIM-SV-SEM-152")
          && has_code("FSIM-SV-SEM-154")
          && has_code("FSIM-SV-SEM-155")
          && has_code("FSIM-SV-SEM-156")
          && has_code("FSIM-SV-SEM-157")
          && has_code("FSIM-SV-SEM-158")
          && has_code("FSIM-SV-SEM-389"),
      "invalid strength, charge, pull, switch, and array forms are targeted");
}

}  // namespace fsim::tests::frontend
