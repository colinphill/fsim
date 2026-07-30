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

void require(
    const bool condition,
    const std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string{message});
  }
}

bool has_code(
    const ParseResult& parsed,
    const std::string_view code) {
  return std::ranges::any_of(
      parsed.diagnostics,
      [&](const Diagnostic& diagnostic) {
        return diagnostic.code == code;
      });
}

} // namespace

void test_vhdl_generic_subprogram_declarations() {
  const auto parsed = parse_text(
      "generic_subprograms.vhd",
      R"(
package generic_subprogram_pkg is
  generic (
    type item_t;
    bias : integer := 1;
    function adjust(value : integer) return integer is <>;
    procedure observe(value : integer) is <>)
  function apply(value : integer) return integer;

  function apply_two is new apply
    generic map (integer, 2, bump, note);
  function apply_defaults is new apply
    generic map (
      item_t => integer,
      bias => <>,
      adjust => <>,
      observe => <>);
end package;

package body generic_subprogram_pkg is
  generic (
    type item_t;
    bias : integer := 1;
    function adjust(value : integer) return integer is <>;
    procedure observe(value : integer) is <>)
  function apply(value : integer) return integer is
  begin
    return adjust(value) + bias;
  end function;
end package body;

entity generic_subprogram_user is
end entity;

architecture rtl of generic_subprogram_user is
  function bump(value : integer) return integer is
  begin
    return value + 1;
  end function;

  procedure note(value : integer) is
  begin
    null;
  end procedure;

  generic (
    type item_t;
    delta : integer := 1;
    function adjust(value : integer) return integer is <>;
    procedure observe(value : integer) is <>)
  procedure update(variable value : inout integer) is
  begin
    value := adjust(value) + delta;
    observe(value);
  end procedure;

  procedure update_three is new update
    generic map (
      item_t => integer,
      delta => 3,
      adjust => bump,
      observe => note);
begin
end architecture;
)",
      Language::Vhdl2008);
  if (!parsed.ok()) {
    for (const auto& diagnostic : parsed.diagnostics) {
      std::cerr << diagnostic.code << ": "
                << diagnostic.message << '\n';
    }
  }
  require(
      parsed.ok(),
      "bounded VHDL generic subprogram forms must parse");
  require(
      parsed.design.units.size() == 4,
      "generic subprogram package, body, entity, and architecture");

  const auto& declaration = parsed.design.units[0];
  require(
      declaration.generic_function_templates.size() == 1
          && declaration.generic_function_instances.size() == 2,
      "package generic function template and instances");
  const auto& function_template =
      declaration.generic_function_templates.front();
  require(
      function_template.generic_parameters.size() == 4
          && function_template.generic_parameters[0].kind
              == ParameterKind::Type
          && function_template.generic_parameters[1].kind
              == ParameterKind::Value
          && function_template.generic_parameters[2].kind
              == ParameterKind::Function
          && function_template.generic_parameters[3].kind
              == ParameterKind::Procedure
          && !function_template.function.defined,
      "generic function template retains all generic families");
  require(
      declaration.generic_function_instances[0].generic_map.size() == 4
          && !declaration.generic_function_instances[0]
                   .generic_map_box
          && declaration.generic_function_instances[1]
                 .generic_map[1].default_box
          && declaration.generic_function_instances[1]
                 .generic_map[2].default_box
          && declaration.generic_function_instances[1]
                 .generic_map[3].default_box,
      "explicit and individual-box function instance maps");

  const auto& body = parsed.design.units[1];
  require(
      body.generic_function_templates.size() == 1
          && body.generic_function_templates.front()
                 .function.defined,
      "package generic function body remains a template");

  const auto& architecture = parsed.design.units[3];
  require(
      architecture.generic_procedure_templates.size() == 1
          && architecture.generic_procedure_instances.size() == 1
          && architecture.generic_procedure_templates.front()
                 .generic_parameters.size() == 4
          && architecture.generic_procedure_templates.front()
                 .procedure.defined
          && architecture.generic_procedure_instances.front()
                 .template_name == "update",
      "local generic procedure template and instance HIR");

  const auto invalid = parse_text(
      "invalid_generic_subprograms.vhd",
      R"(
entity invalid_generic_subprograms is
  generic ()
  function empty(value : integer) return integer;

  generic (
    package nested is new work.template generic map (<>))
  function nested_package(value : integer) return integer;
end entity;

architecture rtl of invalid_generic_subprograms is
  function first is new empty
    generic map (amount => 1, 2);
  function second is new empty
    generic map (amount => 1, amount => 2);
  function malformed is new empty
    generic (<);
begin
end architecture;
)",
      Language::Vhdl2008);
  require(
      !invalid.ok(),
      "invalid generic subprogram forms must be rejected");
  require(
      has_code(invalid, "FSIM-VHDL-SEM-063")
          && has_code(
              invalid, "FSIM-VHDL-UNSUPPORTED-045")
          && has_code(invalid, "FSIM-VHDL-SEM-061")
          && has_code(invalid, "FSIM-VHDL-SEM-060")
          && has_code(invalid, "FSIM-VHDL-PARSE-196"),
      "generic subprogram syntax and association diagnostics");
}

} // namespace fsim::tests::frontend
