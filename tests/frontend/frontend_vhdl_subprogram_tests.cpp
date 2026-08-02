// SPDX-License-Identifier: Apache-2.0
#include "frontend_test_support.hpp"
#include "fsim/frontend/frontend.hpp"

#include <algorithm>
#include <iostream>
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

} // namespace

void test_vhdl_procedure_declarations() {
  const auto parsed = parse_text(
      "procedures.vhd",
      R"(
package procedure_pkg is
  procedure exchange(
    constant source : in integer := 2;
    variable destination : out integer;
    variable accumulator : inout integer);
end package;

package body procedure_pkg is
  procedure exchange(
    constant source : in integer;
    variable destination : out integer;
    variable accumulator : inout integer) is
    variable temporary : integer := source;
  begin
    destination := temporary;
    accumulator := accumulator + temporary;
    return;
  end procedure exchange;
end package body;

entity procedure_user is
  generic (
    procedure transform parameter (
      source : in integer;
      variable destination : out integer;
      variable accumulator : inout integer) is <>;
    procedure observe(value : integer) is selected);
end entity;

architecture rtl of procedure_user is
  procedure local_transform(
    source : in integer;
    variable destination : out integer;
    variable accumulator : inout integer) is
  begin
    destination := source + 1;
    accumulator := accumulator + destination;
  end procedure;
begin
  process
    variable output_value : integer;
    variable total : integer;
  begin
    local_transform(
      source => 2,
      destination => output_value,
      accumulator => total);
    wait;
  end process;
end architecture;
)",
      Language::Vhdl2008);
  if (!parsed.ok()) {
    for (const auto& diagnostic : parsed.diagnostics) {
      std::cerr << diagnostic.code << ": "
                << diagnostic.message << '\n';
    }
  }
  require(parsed.ok(), "bounded VHDL procedure forms must parse");
  require(
      parsed.design.units.size() == 4,
      "package declaration, body, entity, and architecture procedure units");

  const auto& declaration =
      parsed.design.units.front().procedures.front();
  require(
      declaration.name == "exchange"
          && declaration.language == Language::Vhdl2008
          && !declaration.defined
          && declaration.arguments.size() == 3
          && declaration.arguments[0].object_class
              == InterfaceObjectClass::Constant
          && declaration.arguments[0].default_value
          && declaration.arguments[1].object_class
              == InterfaceObjectClass::Variable
          && declaration.arguments[1].direction
              == PortDirection::Output
          && declaration.arguments[2].direction
              == PortDirection::Inout,
      "package procedure declaration profile HIR");
  const auto& body =
      parsed.design.units[1].procedures.front();
  require(
      body.defined && body.variables.size() == 1
          && body.statements.size() == 3
          && body.statements.back().kind
              == StatementKind::Return,
      "package procedure body HIR");

  const auto& entity = parsed.design.units[2];
  require(
      entity.parameters.size() == 2
          && entity.parameters[0].kind
              == ParameterKind::Procedure
          && entity.parameters[1].kind
              == ParameterKind::Procedure
          && entity.parameters[0].procedure_profile
          && entity.parameters[0].procedure_profile->default_box
          && entity.parameters[0].procedure_profile
                 ->arguments.size() == 3
          && entity.parameters[1].procedure_profile
          && entity.parameters[1].procedure_profile->default_name
          && *entity.parameters[1].procedure_profile->default_name
              == "selected",
      "interface procedure profiles and defaults remain distinct");

  const auto& architecture = parsed.design.units.back();
  require(
      architecture.procedures.size() == 1
          && architecture.processes.size() == 1
          && architecture.processes.front().statements.front().kind
              == StatementKind::ProcedureCall
          && architecture.processes.front().statements.front()
                 .procedure_arguments.size() == 3
          && architecture.processes.front().statements.front()
                 .procedure_arguments.front().formal
          && *architecture.processes.front().statements.front()
                  .procedure_arguments.front().formal
              == "source",
      "local procedure and named call association HIR");

  const auto invalid = parse_text(
      "invalid_procedures.vhd",
      R"(
entity invalid_procedures is
  generic (
    procedure bad_constant(constant value : out integer);
    procedure bad_signal(signal value : in integer);
    procedure bad_default(value : integer := 1));
end entity;
architecture rtl of invalid_procedures is
  procedure writes_constant(value : in integer) is
  begin
    value := 2;
  end procedure;
  procedure timed(value : in integer) is
  begin
    wait for 1 ns;
  end procedure;
begin
  process
  begin
    return 1;
    wait;
  end process;
end architecture;
)",
      Language::Vhdl2008);
  require(
      !invalid.ok(),
      "invalid bounded VHDL procedure forms must be rejected");
  const auto has_code =
      [&](const std::string_view code) {
        return std::ranges::any_of(
            invalid.diagnostics,
            [&](const Diagnostic& diagnostic) {
              return diagnostic.code == code;
            });
      };
  require(
      has_code("FSIM-VHDL-SEM-049")
          && has_code("FSIM-VHDL-UNSUPPORTED-038")
          && has_code("FSIM-VHDL-SEM-056")
          && has_code("FSIM-VHDL-SEM-046")
          && !has_code("FSIM-VHDL-UNSUPPORTED-044"),
      "VHDL procedure class/default and return diagnostics");
}

} // namespace fsim::tests::frontend
