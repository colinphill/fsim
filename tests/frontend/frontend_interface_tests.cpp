// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/frontend.hpp"

#include <algorithm>
#include <cstdint>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::tests::frontend {

using namespace fsim::frontend;

namespace {

void require(const bool condition, const std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string{message});
  }
}

}  // namespace

void test_systemverilog_interfaces() {
  const auto parsed = parse_text(
      "interfaces.sv",
      R"(interface bus_if #(parameter int WIDTH = 8);
  logic [WIDTH-1:0] data;
  logic valid;
  logic ready;
  logic cfg;
  function automatic logic [WIDTH-1:0] sample();
    return data;
  endfunction
  task automatic clear();
    data = '0;
  endtask
  modport initiator(output data, valid, input ready,
                    import function sample, import task clear,
                    ref cfg),
          target(input data, valid, output ready),
          service(export function sample, export task clear);
endinterface : bus_if

module interface_user(bus_if.initiator bus, interface monitor);
  bus_if #(.WIDTH(16)) link[1:0]();
  assign bus.data = '0;
endmodule
)",
      Language::SystemVerilog2017);
  require(parsed.ok(), "bounded interface and modport HIR must parse");
  require(
      parsed.design.units.size() == 2
          && parsed.design.units[0].kind
              == UnitKind::SystemVerilogInterface
          && parsed.design.units[1].kind
              == UnitKind::VerilogModule,
      "interfaces retain a distinct design-unit kind");
  const auto& interface_unit = parsed.design.units[0];
  require(
      interface_unit.parameters.size() == 1
          && interface_unit.signals.size() == 4
          && interface_unit.systemverilog_modports.size() == 3
          && interface_unit.systemverilog_modports[0].name
              == "initiator"
          && interface_unit.systemverilog_modports[0].members.size() == 6
          && interface_unit.systemverilog_modports[0].members[0].name
              == "data"
          && interface_unit.systemverilog_modports[0].members[0].direction
              == PortDirection::Output
          && interface_unit.systemverilog_modports[0].members[2].direction
              == PortDirection::Input
          && interface_unit.systemverilog_modports[0].members[3].kind
              == SystemVerilogModportMemberKind::FunctionImport
          && interface_unit.systemverilog_modports[0].members[4].kind
              == SystemVerilogModportMemberKind::TaskImport
          && interface_unit.systemverilog_modports[0].members[5].direction
              == PortDirection::Ref
          && interface_unit.systemverilog_modports[2].members[0].kind
              == SystemVerilogModportMemberKind::FunctionExport,
      "modports retain signal directions and callable access entries");
  const auto& module = parsed.design.units[1];
  require(
      module.ports.size() == 2
          && module.ports[0].interface_type == "bus_if"
          && module.ports[0].modport == "initiator"
          && module.ports[0].type.spelling == "interface"
          && module.ports[1].interface_type.empty()
          && module.ports[1].modport.empty()
          && module.signals.empty()
          && module.instances.size() == 1
          && module.instances.front().unit_name == "bus_if"
          && module.instances.front().array_indices
              == std::vector<std::int64_t>({1, 0})
          && module.instances.front().parameter_overrides.size() == 1,
      "interface ports and parameterized interface instances retain HIR");

  const auto invalid = parse_text(
      "invalid_interfaces.sv",
      R"(interface invalid_if;
  logic value;
  function automatic logic sample(); return value; endfunction
  modport broken(value),
          unknown(input missing),
          duplicate(input value, value),
          duplicate(output value),
          callable(import missing),
          wrong(import task sample),
          clocked(clocking value);
endinterface
module invalid_module;
  modport illegal(input missing);
endmodule
)",
      Language::SystemVerilog2017);
  const auto has_code = [&](const std::string_view code) {
    return std::ranges::any_of(
        invalid.diagnostics,
        [&](const Diagnostic& diagnostic) {
          return diagnostic.code == code;
        });
  };
  require(
      !invalid.ok()
          && has_code("FSIM-SV-SEM-115")
          && has_code("FSIM-SV-SEM-116")
          && has_code("FSIM-SV-SEM-117")
          && has_code("FSIM-SV-SEM-118")
          && has_code("FSIM-SV-SEM-119")
          && has_code("FSIM-SV-SEM-122")
          && has_code("FSIM-SV-SEM-123")
          && has_code("FSIM-SV-UNSUPPORTED-044"),
      "invalid interface and modport forms have stable diagnostics");
}

}  // namespace fsim::tests::frontend
