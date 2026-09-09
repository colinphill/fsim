// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/frontend.hpp"
#include "fsim/frontend/systemverilog_scalar_folding.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace fsim::tests::frontend {

using namespace fsim::frontend;

namespace {

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

[[maybe_unused]] std::filesystem::path make_test_directory(
    std::string_view name) {
  const auto suffix =
      std::chrono::steady_clock::now().time_since_epoch().count();
  const auto directory =
      std::filesystem::temp_directory_path()
      / ("fsim-" + std::string{name} + "-"
         + std::to_string(suffix));
  std::filesystem::create_directories(directory);
  return directory;
}

[[maybe_unused]] void write_text(
    const std::filesystem::path& path,
    const std::string_view text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary);
  output << text;
  require(
      output.good(),
      "frontend test fixture must be writable");
}

} // namespace

void test_systemverilog_compiler_directives() {
  const auto directives = parse_text(
      "directives.sv",
      R"(`default_nettype none
module strict;
  assign forbidden = 1'b1;
endmodule
`default_nettype tri1
module implicit_net;
  assign created = 1'b0;
endmodule
`celldefine
module cell_unit;
endmodule
`endcelldefine
module child(input logic value);
endmodule
`unconnected_drive pull1
module driven_parent;
  child child_instance();
endmodule
`nounconnected_drive
`begin_keywords "1800-2009"
module soft;
endmodule
`end_keywords
`begin_keywords "1364-2005"
module logic;
endmodule
`end_keywords
`timescale 10ns/1ns
`default_nettype tri0
`celldefine
`unconnected_drive pull0
`resetall
module reset_unit;
  initial #2 $finish;
endmodule
)",
      Language::SystemVerilog2017);
  require(
      !directives.ok(),
      "`default_nettype none must reject an implicit declaration");
  require(
      std::any_of(
          directives.diagnostics.begin(),
          directives.diagnostics.end(),
          [](const Diagnostic& diagnostic) {
            return diagnostic.code == "FSIM-SV-SEM-015";
          }),
      "forbidden implicit net diagnostic");
  require(
      directives.design.units.size() == 8,
      "all directive-state modules remain represented");

  const auto* implicit_net =
      directives.design.find(UnitKind::VerilogModule, "implicit_net");
  require(
      implicit_net != nullptr
          && implicit_net->default_nettype == "tri1"
          && implicit_net->signals.size() == 1
          && implicit_net->signals.front().name == "created"
          && implicit_net->signals.front().type.spelling == "tri1",
      "`default_nettype creates a typed implicit scalar net");
  const auto* cell =
      directives.design.find(UnitKind::VerilogModule, "cell_unit");
  require(cell != nullptr && cell->is_cell,
          "`celldefine marks following modules");
  const auto* child =
      directives.design.find(UnitKind::VerilogModule, "child");
  require(child != nullptr && !child->is_cell,
          "`endcelldefine restores ordinary module metadata");
  const auto* parent =
      directives.design.find(UnitKind::VerilogModule, "driven_parent");
  require(
      parent != nullptr && parent->instances.size() == 1
          && parent->instances.front().unconnected_drive
              == VerilogUnconnectedDrive::Pull1,
      "`unconnected_drive state is captured by an instance");
  require(
      directives.design.find(UnitKind::VerilogModule, "soft") != nullptr,
      "1800-2009 keyword scope precedes the 1800-2012 soft keyword");
  require(
      directives.design.find(UnitKind::VerilogModule, "logic") != nullptr,
      "legacy begin_keywords permits a later SystemVerilog keyword as a name");
  const auto* reset =
      directives.design.find(UnitKind::VerilogModule, "reset_unit");
  require(
      reset != nullptr && reset->default_nettype == "wire"
          && !reset->is_cell && reset->time_unit.empty()
          && reset->processes.front().statements.front().delay->magnitude == 2
          && reset->processes.front().statements.front().delay->unit.empty(),
      "`resetall restores directive defaults before a following module");

  const auto port_none = parse_text(
      "default-none-port.sv",
      "`default_nettype none\nmodule bad(input value); endmodule\n",
      Language::SystemVerilog2017);
  require(
      !port_none.ok()
          && std::any_of(
              port_none.diagnostics.begin(),
              port_none.diagnostics.end(),
              [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-SEM-016";
              }),
      "`default_nettype none rejects an untyped ANSI port");

  const auto reserved_name = parse_text(
      "reserved-name.sv",
      R"(`begin_keywords "1800-2012"
module soft;
endmodule
`end_keywords
)",
      Language::SystemVerilog2017);
  require(
      !reserved_name.ok()
          && std::any_of(
              reserved_name.diagnostics.begin(),
              reserved_name.diagnostics.end(),
              [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-PARSE-001";
              }),
      "1800-2012 keyword scope rejects soft as an identifier");

  const auto malformed = parse_text(
      "malformed-directives.sv",
      R"(`default_nettype banana
`unconnected_drive highz
`nounconnected_drive
`celldefine
`celldefine
`endcelldefine
`begin_keywords "1800-2099"
`end_keywords
`resetall extra
`begin_keywords "1800-2017"
module valid;
endmodule
)",
      Language::SystemVerilog2017);
  const auto has_code = [&](const std::string_view code) {
    return std::any_of(
        malformed.diagnostics.begin(),
        malformed.diagnostics.end(),
        [&](const Diagnostic& diagnostic) {
          return diagnostic.code == code;
        });
  };
  require(
      !malformed.ok() && has_code("FSIM-SV-PP-032")
          && has_code("FSIM-SV-PP-034")
          && has_code("FSIM-SV-PP-035")
          && has_code("FSIM-SV-PP-036")
          && has_code("FSIM-SV-PP-037")
          && has_code("FSIM-SV-PP-038")
          && has_code("FSIM-SV-PP-047")
          && has_code("FSIM-SV-PP-048"),
      "malformed directive forms have stable targeted diagnostics");
}

void test_systemverilog_function_declarations() {
  const auto parsed = parse_text(
      "functions.sv",
      R"(
package math_pkg;
  function automatic logic [7:0] increment(
      input logic [7:0] value);
    return value + 1;
  endfunction : increment
endpackage

module function_user(input logic select, output logic [7:0] result);
  logic observed;
  function automatic logic [7:0] choose(
      input logic condition,
      input logic [7:0] when_true,
      when_false);
    logic [7:0] temporary;
    if (condition)
      temporary = when_true;
    else
      temporary = when_false;
    choose = temporary;
  endfunction

  function automatic logic observe(input logic value);
    observed = value;
    observe = observed;
  endfunction

  function automatic void record(input logic value);
    static logic retained;
    event changed;
    retained = value;
    -> changed;
    fork
      $display("recorded");
    join_none
    return;
  endfunction

  initial result = choose(select, 8'h2a, 8'h11);
endmodule
)",
      Language::SystemVerilog2017);
  require(parsed.ok(), "automatic SystemVerilog functions must parse");
  require(
      parsed.design.units.size() == 2,
      "package and module function design units");
  const auto& package_function =
      parsed.design.units.front().functions.front();
  require(
      package_function.name == "increment"
          && package_function.automatic
          && package_function.arguments.size() == 1
          && package_function.statements.size() == 1
          && package_function.statements.front().kind
              == StatementKind::Return,
      "package function HIR");
  const auto& module_function =
      parsed.design.units.back().functions.front();
  require(
      module_function.name == "choose"
          && module_function.arguments.size() == 3
          && module_function.variables.size() == 1
          && module_function.statements.size() == 2
          && module_function.statements.back().kind
              == StatementKind::Assignment,
      "module function arguments, locals, and body HIR");
  require(
      parsed.design.units.back().functions.size() == 3
          && parsed.design.units.back().functions[1].name == "observe"
          && parsed.design.units.back().functions[1].statements.size() == 2
          && parsed.design.units.back().functions[1].statements.front()
                 .target.text == "observed",
      "time-free function writes to nonlocal variables remain observable");
  require(
      parsed.design.units.back().functions[2].return_type.spelling == "void"
          && parsed.design.units.back().functions[2].variables.size() == 2
          && parsed.design.units.back().functions[2].statements.size() == 4
          && parsed.design.units.back().functions[2].statements.back().kind
              == StatementKind::Return,
      "void functions accept static/event locals, immediate triggers, nonblocking forks, and value-free returns");

  const auto classic = parse_text(
      "classic_function.v",
      R"(
module classic_function;
  function automatic [7:0] answer;
    answer = 8'h2a;
  endfunction
endmodule
)",
      Language::Verilog2005);
  require(
      classic.ok()
          && classic.design.units.front().functions.size() == 1
          && classic.design.units.front().functions.front()
                 .arguments.empty(),
      "classic no-argument Verilog function");

  const auto qualified_result = parse_text(
      "qualified_function_result.sv",
      R"(
class result_owner;
  extern function int compute();
endclass
function int result_owner::compute();
  compute = 42;
endfunction
)",
      Language::SystemVerilog2017);
  require(
      qualified_result.ok()
          && qualified_result.design
                 .systemverilog_class_method_definitions.size() == 1
          && qualified_result.design
                 .systemverilog_class_method_definitions.front()
                 .statements.front().target.text == "compute",
      "qualified function definitions assign their unqualified result name");

  const auto invalid = parse_text(
      "invalid_functions.sv",
      R"(
module invalid_functions;
  function static logic bad_ref(ref logic argument);
    bad_ref = argument;
  endfunction
  function automatic string bad_output(output string argument);
    bad_output = argument;
  endfunction
  function automatic logic bad_default(output logic argument = 1'b0);
    bad_default = argument;
  endfunction
  function static logic bad(output logic argument);
    #1 bad = argument;
  endfunction
  function automatic logic writes_input(input logic argument);
    argument = 1'b0;
    writes_input = argument;
  endfunction
endmodule
)",
      Language::SystemVerilog2017);
  require(!invalid.ok(), "invalid function forms must be rejected");
  const auto has_code =
      [&](const std::string_view code) {
        return std::ranges::any_of(
            invalid.diagnostics,
            [&](const Diagnostic& diagnostic) {
              return diagnostic.code == code;
            });
      };
  require(
      has_code("FSIM-SV-SEM-095")
          && has_code("FSIM-SV-SEM-096")
          && has_code("FSIM-SV-SEM-064"),
      "function default, ref lifetime, and timing diagnostics");
}

void test_systemverilog_real_time_declarations() {
  const auto parsed = parse_text(
      "real-time-declarations.sv",
      R"(
module real_time_types(
    input shortreal sample,
    output real result,
    input realtime delay_value,
    input time tick_value);
  function automatic real convert(
      input shortreal argument,
      input realtime offset);
    real exact_decimal = 1.2500;
    shortreal exponent_decimal = 6.02e+2;
    realtime physical_delay = 250ps;
    time fractional_tick = 1.5ns;
    convert = argument;
  endfunction
endmodule

class scalar_owner;
  real gain = 2.5;
  shortreal ratio = 5e-1;
  realtime deadline = 10us;
endclass
)",
      Language::SystemVerilog2017);
  require(parsed.ok(), "SystemVerilog real/time declarations must parse");
  const auto& unit = parsed.design.units.front();
  require(
      unit.ports.size() == 4
          && unit.ports[0].type.systemverilog_scalar
              == SystemVerilogScalarKind::ShortReal
          && unit.ports[0].type.width() == 32
          && unit.ports[1].type.systemverilog_scalar
              == SystemVerilogScalarKind::Real
          && unit.ports[1].type.width() == 64
          && unit.ports[2].type.systemverilog_scalar
              == SystemVerilogScalarKind::Realtime
          && unit.ports[3].type.systemverilog_scalar
              == SystemVerilogScalarKind::Time,
      "ports retain exact shortreal/real/realtime/time kinds");
  const auto& function = unit.functions.front();
  require(
      function.return_type.systemverilog_scalar
              == SystemVerilogScalarKind::Real
          && function.arguments.size() == 2
          && function.arguments[0].type.systemverilog_scalar
              == SystemVerilogScalarKind::ShortReal
          && function.arguments[1].type.systemverilog_scalar
              == SystemVerilogScalarKind::Realtime
          && function.variables.size() == 4,
      "callable profiles and locals retain exact scalar kinds");

  const auto& exact = *function.variables[0].initializer;
  const auto& exponent = *function.variables[1].initializer;
  const auto& physical = *function.variables[2].initializer;
  const auto& fractional = *function.variables[3].initializer;
  require(
      exact.systemverilog_decimal_literal
          && exact.systemverilog_decimal_literal->kind
              == SystemVerilogDecimalLiteralKind::Real
          && exact.systemverilog_decimal_literal->digits == "125"
          && exact.systemverilog_decimal_literal->decimal_exponent == -2
          && exponent.systemverilog_decimal_literal
          && exponent.systemverilog_decimal_literal->digits == "602"
          && exponent.systemverilog_decimal_literal->decimal_exponent == 0,
      "real literals retain canonical exact digits and decimal exponents");
  require(
      physical.systemverilog_decimal_literal
          && physical.systemverilog_decimal_literal->kind
              == SystemVerilogDecimalLiteralKind::Time
          && physical.systemverilog_decimal_literal->digits == "25"
          && physical.systemverilog_decimal_literal->decimal_exponent == 1
          && physical.systemverilog_decimal_literal->time_unit == "ps"
          && fractional.systemverilog_decimal_literal
          && fractional.systemverilog_decimal_literal->digits == "15"
          && fractional.systemverilog_decimal_literal->decimal_exponent == -1
          && fractional.systemverilog_decimal_literal->time_unit == "ns"
          && fractional.span.end.offset - fractional.span.begin.offset == 5,
      "time literals retain canonical magnitude, unit, and complete span");

  auto arithmetic = Expression{
      ExpressionKind::Binary, "+", {exact, exponent}, exact.span};
  std::string scalar_error;
  require(
      propagate_systemverilog_scalar_types(arithmetic, {}, scalar_error)
          && arithmetic.systemverilog_scalar_kind
              == SystemVerilogScalarKind::Real,
      "mixed real expression propagation retains the dominant exact kind");
  const auto arithmetic_value = evaluate_systemverilog_scalar_constant(
      arithmetic, {}, {}, scalar_error);
  require(
      arithmetic_value && arithmetic_value->real()
          && *arithmetic_value->real() == 603.25,
      "real constant arithmetic folds to deterministic IEEE-754 bits");

  auto condition = Expression{
      ExpressionKind::Binary, "<", {exact, exponent}, exact.span};
  auto conditional = Expression{
      ExpressionKind::Call, "?:", {condition, exact, exponent}, exact.span};
  require(
      propagate_systemverilog_scalar_types(conditional, {}, scalar_error)
          && conditional.systemverilog_scalar_kind
              == SystemVerilogScalarKind::Real,
      "conditional expressions merge real-family branch types");
  const auto conditional_value = evaluate_systemverilog_scalar_constant(
      conditional, {}, {}, scalar_error);
  require(
      conditional_value && conditional_value->real()
          && *conditional_value->real() == 1.25,
      "real comparisons and conditional truth fold deterministically");

  auto cast = Expression{
      ExpressionKind::Call, "@sv-cast:shortreal", {exact}, exact.span};
  require(
      propagate_systemverilog_scalar_types(cast, {}, scalar_error)
          && cast.systemverilog_scalar_kind
              == SystemVerilogScalarKind::ShortReal,
      "explicit real-family casts propagate their exact result kind");
  const auto cast_value = evaluate_systemverilog_scalar_constant(
      cast, {}, {}, scalar_error);
  require(
      cast_value && cast_value->kind == SystemVerilogScalarKind::ShortReal
          && cast_value->real() && *cast_value->real() == 1.25,
      "real-to-shortreal constant conversion uses binary32 rounding");

  auto time_expression = physical;
  require(
      propagate_systemverilog_scalar_types(time_expression, {}, scalar_error)
          && time_expression.systemverilog_scalar_kind
              == SystemVerilogScalarKind::Realtime,
      "time literals propagate as realtime expressions before integral casts");
  const auto time_value = evaluate_systemverilog_scalar_constant(
      time_expression,
      {},
      SystemVerilogScalarEvaluationContext{1'000, 1'000},
      scalar_error);
  require(
      time_value && time_value->real() && *time_value->real() == 250.0,
      "time literal folding applies the exact evaluation time unit");
  const auto rounded_time = convert_systemverilog_scalar_constant(
      *evaluate_systemverilog_scalar_constant(exact, {}, {}, scalar_error),
      SystemVerilogScalarKind::Time,
      scalar_error);
  require(
      rounded_time && rounded_time->integral()
          && *rounded_time->integral() == 1,
      "real-to-time conversion uses deterministic nearest rounding");

  const auto integer_zero = evaluate_systemverilog_scalar_constant(
      Expression { ExpressionKind::IntegerLiteral, "0", { }, { } },
      {}, {}, scalar_error);
  require(integer_zero.has_value(), "the integral zero literal folds");
  const auto null_chandle = convert_systemverilog_scalar_constant(
      *integer_zero, SystemVerilogScalarKind::Chandle, scalar_error);
  require(
      null_chandle
          && null_chandle->kind == SystemVerilogScalarKind::Chandle
          && null_chandle->bits == 0U,
      "the integral zero literal converts to the null chandle value");
  const auto integer_one = evaluate_systemverilog_scalar_constant(
      Expression { ExpressionKind::IntegerLiteral, "1", { }, { } },
      {}, {}, scalar_error);
  require(integer_one.has_value(), "the integral one literal folds");
  require(
      !convert_systemverilog_scalar_constant(
          *integer_one, SystemVerilogScalarKind::Chandle, scalar_error)
          && scalar_error
              == "chandle casts require a chandle or null operand",
      "nonzero integral values do not convert to chandle values");

  const auto revised_formals = parse_verilog(
      SourceText { "function_ref_static.sv", R"(
module function_ref_static;
  function automatic int observe(
      const ref static int anchor, inherited_anchor,
      ref static int target, inherited_target,
      input int increment = 1);
    target = target + increment;
    return anchor + inherited_anchor + target + inherited_target;
  endfunction
endmodule
)" },
      StandardRevision::SystemVerilog2023);
  require(
      revised_formals.ok()
          && revised_formals.design.units.front().functions.size() == 1,
      "SystemVerilog-2023 ref static function formals must parse");
  const auto& revised_arguments =
      revised_formals.design.units.front().functions.front().arguments;
  require(
      revised_arguments.size() == 5
          && revised_arguments[0].reference
          && revised_arguments[0].const_reference
          && revised_arguments[0].static_reference
          && revised_arguments[0].direction == PortDirection::Input
          && revised_arguments[1].reference
          && revised_arguments[1].const_reference
          && revised_arguments[1].static_reference
          && revised_arguments[2].reference
          && !revised_arguments[2].const_reference
          && revised_arguments[2].static_reference
          && revised_arguments[2].direction == PortDirection::Inout
          && revised_arguments[3].reference
          && revised_arguments[3].static_reference
          && !revised_arguments[4].reference
          && revised_arguments[4].default_value,
      "function formal direction and ref qualifiers inherit as one profile");
  const auto legacy_formals = parse_text(
      "function_ref_static_legacy.sv",
      R"(
module function_ref_static_legacy;
  function automatic int observe(ref static int value);
    return value;
  endfunction
endmodule
)",
      Language::SystemVerilog2017);
  require(
      !legacy_formals.ok()
          && std::ranges::any_of(
              legacy_formals.diagnostics,
              [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-PARSE-368";
              }),
      "ref static function formals do not leak into retained profiles");
  const auto writable_const_function = parse_verilog(
      SourceText { "writable_const_function.sv", R"(
module writable_const_function;
  function automatic int mutate(const ref static int value);
    value = 1;
    return value;
  endfunction
endmodule
)" },
      StandardRevision::SystemVerilog2023);
  require(
      !writable_const_function.ok()
          && std::ranges::any_of(
              writable_const_function.diagnostics,
              [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-SEM-249";
              }),
      "const ref function formals are read-only");

  require(
      parsed.design.systemverilog_classes.size() == 1
          && parsed.design.systemverilog_classes.front().properties.size()
              == 3
          && parsed.design.systemverilog_classes.front().properties[0]
                 .declaration.type.systemverilog_scalar
              == SystemVerilogScalarKind::Real
          && parsed.design.systemverilog_classes.front().properties[1]
                 .declaration.type.systemverilog_scalar
              == SystemVerilogScalarKind::ShortReal
          && parsed.design.systemverilog_classes.front().properties[2]
                 .declaration.type.systemverilog_scalar
              == SystemVerilogScalarKind::Realtime,
      "class properties own exact real-family kinds");

  const auto malformed = parse_text(
      "malformed-real-time.sv",
      R"(
module malformed_real_time;
  real missing_exponent = 1e+;
  realtime invalid_unit = 1.25fortnights;
endmodule
)",
      Language::SystemVerilog2017);
  require(!malformed.ok(), "malformed real/time literals must reject");
  require(
      std::ranges::any_of(
          malformed.diagnostics,
          [](const Diagnostic& diagnostic) {
            return diagnostic.code == "FSIM-SV-PARSE-281";
          })
          && std::ranges::any_of(
              malformed.diagnostics,
              [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-PARSE-282";
              }),
      "malformed exponent and time unit receive stable diagnostics");
}

void test_vhdl_function_declarations() {
  const auto parsed = parse_text(
      "functions.vhd",
      R"(
package math_pkg is
  pure function twice parameter (value : in integer) return integer;
end package;

package body math_pkg is
  pure function twice parameter (value : in integer) return integer is
  begin
    return value + value;
  end function twice;
end package body math_pkg;

entity function_user is
  generic (
    function transform parameter (value : integer) return integer is <>;
    impure function observe(value : integer) return integer is selected);
  port (
    input_value : in integer;
    result : out integer);
end entity;

architecture rtl of function_user is
  pure function increment(
    constant value : in integer := 1) return integer is
    variable temporary : integer := value;
  begin
    temporary := temporary + 1;
    return temporary;
  end function increment;
begin
  result <= increment(input_value);
end architecture;
)",
      Language::Vhdl2008);
  require(parsed.ok(), "bounded VHDL function forms must parse");
  require(
      parsed.design.units.size() == 4,
      "package declaration, body, entity, and architecture function units");

  const auto& package_function =
      parsed.design.units.front().functions.front();
  require(
      package_function.name == "twice"
          && package_function.language == Language::Vhdl2008
          && package_function.pure
          && !package_function.defined
          && package_function.arguments.size() == 1,
      "package function declaration HIR");
  const auto& package_body =
      parsed.design.units[1].functions.front();
  require(
      parsed.design.units[1].primary_name == "math_pkg"
          && package_body.name == "twice"
          && package_body.defined
          && package_body.statements.size() == 1
          && package_body.statements.front().kind
              == StatementKind::Return,
      "package function body HIR");

  const auto& entity = parsed.design.units[2];
  require(
      entity.parameters.size() == 2
          && entity.parameters[0].kind
              == ParameterKind::Function
          && entity.parameters[1].kind
              == ParameterKind::Function,
      "interface functions remain distinct generic formals");
  const auto& boxed =
      *entity.parameters[0].function_profile;
  const auto& named =
      *entity.parameters[1].function_profile;
  require(
      entity.parameters[0].name == "transform"
          && boxed.pure && boxed.default_box
          && !boxed.default_name
          && boxed.arguments.size() == 1
          && boxed.arguments.front().name == "value"
          && boxed.return_type.domain
              == ValueDomain::Integer,
      "boxed interface function profile HIR");
  require(
      entity.parameters[1].name == "observe"
          && !named.pure && !named.default_box
          && named.default_name
          && *named.default_name == "selected",
      "named impure interface function default HIR");

  const auto& body =
      parsed.design.units.back().functions.front();
  require(
      body.name == "increment"
          && body.language == Language::Vhdl2008
          && body.pure && body.defined && body.automatic
          && body.arguments.front().default_value
          && body.variables.size() == 1
          && body.statements.size() == 2
          && body.statements.front().kind
              == StatementKind::Assignment
          && body.statements.back().kind
              == StatementKind::Return
          && body.statements.back().value.kind
              == ExpressionKind::Identifier,
      "VHDL function body, locals, and return HIR");

  const auto invalid = parse_text(
      "invalid_functions.vhd",
      R"(
entity invalid_functions is
  generic (
    function bad(signal value : out integer := 1)
      return integer is "not_a_name";
    procedure bad_procedure(signal value : buffer integer));
end entity;

architecture rtl of invalid_functions is
  function "+"(value : integer) return integer is
  begin
    return;
  end function "+";
  function timed(value : integer) return integer is
  begin
    wait for 1 ns;
    return value;
  end function;
begin
  process
  begin
    return 1;
  end process;
end architecture;
)",
      Language::Vhdl2008);
  require(
      !invalid.ok(),
      "invalid bounded VHDL function forms must be rejected");
  const auto has_code =
      [&](const std::string_view code) {
        return std::ranges::any_of(
            invalid.diagnostics,
            [&](const Diagnostic& diagnostic) {
              return diagnostic.code == code;
            });
      };
  require(
      has_code("FSIM-VHDL-UNSUPPORTED-029")
          && has_code("FSIM-VHDL-UNSUPPORTED-030")
          && has_code("FSIM-VHDL-UNSUPPORTED-038")
          && has_code("FSIM-VHDL-UNSUPPORTED-039")
          && has_code("FSIM-VHDL-UNSUPPORTED-037")
          && has_code("FSIM-VHDL-PARSE-157")
          && has_code("FSIM-VHDL-PARSE-164")
          && has_code("FSIM-VHDL-SEM-046"),
      "VHDL function class, mode, designator, procedure, "
      "return, and placement diagnostics");
}

void test_systemverilog_task_declarations() {
  const auto parsed = parse_text("tasks.sv",
                                 R"(
package transform_pkg;
  timeunit 1ns;
  timeprecision 1ns;
  task automatic exchange(
      input logic [7:0] source,
      output logic [7:0] destination,
      inout logic [7:0] accumulator);
    logic [7:0] temporary;
    temporary = source;
    destination = temporary;
    accumulator = accumulator + temporary;
    return;
  endtask : exchange
endpackage

module task_owner;
  event wake;
  logic ready;
  task automatic clear;
    logic temporary;
    temporary = 1'b0;
    #1;
    @(wake);
    wait (ready);
    -> wake;
  endtask
endmodule
)",
                                 Language::SystemVerilog2017);
  require(parsed.ok(), "automatic SystemVerilog tasks must parse");
  require(parsed.design.units.size() == 2 &&
              parsed.design.units.front().tasks.size() == 1 &&
              parsed.design.units.back().tasks.size() == 1,
          "package and module task design units");
  require(parsed.design.units.front().time_unit == "1ns" &&
              parsed.design.units.front().time_precision == "1ns",
          "package task timing context");
  const auto &task = parsed.design.units.front().tasks.front();
  require(task.name == "exchange" && task.automatic &&
              task.arguments.size() == 3 &&
              task.arguments[0].direction == PortDirection::Input &&
              task.arguments[1].direction == PortDirection::Output &&
              task.arguments[2].direction == PortDirection::Inout &&
              task.variables.size() == 1 && task.statements.size() == 4 &&
              task.statements.back().kind == StatementKind::Return &&
              !task.statements.back().value.valid(),
          "task HIR preserves formals, locals, body, lifetime, and return");
  require(parsed.design.units.back().tasks.front().arguments.empty(),
          "classic no-argument task header");
  const auto &timed_task = parsed.design.units.back().tasks.front();
  require(timed_task.statements.size() == 5 &&
              timed_task.statements[1].kind == StatementKind::Delay &&
              timed_task.statements[2].kind == StatementKind::WaitOn &&
              timed_task.statements[3].kind == StatementKind::WaitUntil &&
              timed_task.statements[4].kind == StatementKind::EventTrigger,
          "task HIR admits bounded timing, event waits, condition waits, "
          "and named-event triggers");

  const auto scheduled = parse_text(
      "scheduled_task_actions.sv",
      R"(
module scheduled_task_actions;
  task run;
    logic value;
    value <= #1 1'b1;
    $stop;
    $finish;
  endtask
endmodule
)",
      Language::SystemVerilog2017);
  require(
      scheduled.ok()
          && scheduled.design.units.front().tasks.front().statements.size()
              == 3,
      "tasks admit nonblocking intra-assignment controls and simulation control tasks");

  const auto invalid = parse_text("invalid_tasks.sv",
                                  R"(
module invalid_tasks;
  task static bad(
      ref logic value,
      output string text,
      output logic value);
    #1 value <= 1'b1;
    return value;
  endtask : mismatched
endmodule
)",
                                  Language::SystemVerilog2017);
  require(!invalid.ok(), "invalid task forms must be rejected");
  const auto has_code = [&](const std::string_view code) {
    return std::ranges::any_of(
        invalid.diagnostics,
        [&](const Diagnostic &diagnostic) { return diagnostic.code == code; });
  };
  require(has_code("FSIM-SV-SEM-098") &&
              has_code("FSIM-SV-SEM-067") && has_code("FSIM-SV-SEM-068") &&
              has_code("FSIM-SV-SEM-071"),
          "task lifetime, ref formal, return, and closing-name "
          "diagnostics");

  const auto revised_formals = parse_verilog(
      SourceText { "task_ref_static.sv", R"(
module task_ref_static;
  task automatic update(
      ref static int target, inherited_target,
      const ref static int anchor, inherited_anchor);
    target = target + anchor;
  endtask
endmodule
)" },
      StandardRevision::SystemVerilog2023);
  require(
      revised_formals.ok()
          && revised_formals.design.units.front().tasks.size() == 1,
      "SystemVerilog-2023 ref static task formals must parse");
  const auto& revised_arguments =
      revised_formals.design.units.front().tasks.front().arguments;
  require(
      revised_arguments.size() == 4
          && revised_arguments[0].reference
          && !revised_arguments[0].const_reference
          && revised_arguments[0].static_reference
          && revised_arguments[0].direction == PortDirection::Inout
          && revised_arguments[1].reference
          && revised_arguments[1].static_reference
          && revised_arguments[2].reference
          && revised_arguments[2].const_reference
          && revised_arguments[2].static_reference
          && revised_arguments[2].direction == PortDirection::Input
          && revised_arguments[3].reference
          && revised_arguments[3].const_reference
          && revised_arguments[3].static_reference,
      "task formal direction and ref qualifiers inherit as one profile");
  const auto writable_const_task = parse_verilog(
      SourceText { "writable_const_task.sv", R"(
module writable_const_task;
  task automatic mutate(const ref static int value);
    value = 1;
  endtask
endmodule
)" },
      StandardRevision::SystemVerilog2023);
  require(
      !writable_const_task.ok()
          && std::ranges::any_of(
              writable_const_task.diagnostics,
              [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-SEM-249";
              }),
      "const ref task formals are read-only");

  const auto duplicate = parse_text("duplicate_tasks.sv",
                                    R"(
package duplicate_tasks;
  task automatic same;
  endtask
  task automatic same;
  endtask
endpackage
)",
                                    Language::SystemVerilog2017);
  require(!duplicate.ok() &&
              std::ranges::any_of(duplicate.diagnostics,
                                  [](const Diagnostic &diagnostic) {
                                    return diagnostic.code == "FSIM-SV-SEM-073";
                                  }),
          "duplicate task declarations are rejected");
}

void test_immediate_assertions() {
  const auto vhdl = parse_text(
      "assertions.vhd",
      R"(
entity assertions is
end entity;
architecture rtl of assertions is
  signal ready : boolean;
begin
  concurrent_check: assert ready
    report "concurrent mismatch" severity warning;
  check: process
  begin
    assert 0 = 1 report "vhdl mismatch" severity failure;
  end process;
end architecture;
)",
      Language::Vhdl2008);
  require(vhdl.ok(), "bounded VHDL assertion syntax");
  const auto* architecture =
      vhdl.design.find(UnitKind::VhdlArchitecture, "rtl");
  require(
      architecture != nullptr
          && architecture->processes.size() == 1
          && architecture->concurrent_statements.size() == 1,
      "VHDL assertion process");
  const auto& vhdl_concurrent_assertion =
      architecture->concurrent_statements.front();
  require(
      vhdl_concurrent_assertion.kind == StatementKind::Assert
          && vhdl_concurrent_assertion.label == "concurrent_check"
          && vhdl_concurrent_assertion.condition.text == "ready"
          && vhdl_concurrent_assertion.assertion_message
              == "concurrent mismatch"
          && vhdl_concurrent_assertion.assertion_severity
              == AssertionSeverity::Warning,
      "labeled concurrent VHDL assertion metadata");
  const auto& vhdl_assertion =
      architecture->processes.front().statements.front();
  require(
      vhdl_assertion.kind == StatementKind::Assert
          && vhdl_assertion.assertion_message == "vhdl mismatch"
          && vhdl_assertion.assertion_severity
              == AssertionSeverity::Failure
          && vhdl_assertion.span.source_name == "assertions.vhd"
          && vhdl_assertion.span.begin.line == 11,
      "VHDL assertion metadata");

  const auto system_verilog = parse_text(
      "assertions.sv",
      R"(
module assertions;
  initial begin
    assert (1'b1) $info("pass"); else $fatal("unexpected");
    assert (1'b0) begin
      $info("unreached");
    end else begin
      $warning;
      $error("sv\nmismatch");
    end
    assert (1'b0);
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(
      system_verilog.ok(),
      "SystemVerilog immediate-assertion action blocks must parse");
  const auto& sv_assertions =
      system_verilog.design.units.front().processes.front().statements;
  require(
      sv_assertions.size() == 3
          && sv_assertions[0].kind == StatementKind::Assert
          && sv_assertions[0].assertion_has_pass_action
          && sv_assertions[0].assertion_has_failure_action
          && sv_assertions[0].statements.size() == 1
          && sv_assertions[0].statements.front().kind
              == StatementKind::Report
          && sv_assertions[0].statements.front().assertion_severity
              == AssertionSeverity::Note
          && sv_assertions[0].else_statements.size() == 1
          && sv_assertions[0].else_statements.front().assertion_severity
              == AssertionSeverity::Failure
          && sv_assertions[1].statements.size() == 1
          && sv_assertions[1].statements.front().kind
              == StatementKind::Block
          && sv_assertions[1].else_statements.size() == 1
          && sv_assertions[1].else_statements.front().kind
              == StatementKind::Block
          && sv_assertions[1].else_statements.front().statements.size()
              == 2
          && sv_assertions[2].assertion_has_pass_action
          && !sv_assertions[2].assertion_has_failure_action
          && sv_assertions[2].span.source_name == "assertions.sv",
      "SystemVerilog pass/failure action metadata");

  const auto contextual_vhdl_severity = parse_text(
      "bad_assertion.vhd",
      R"(
entity bad_assertion is end entity;
architecture rtl of bad_assertion is begin
  check: process begin
    assert 1 severity panic;
  end process;
end architecture;
)",
      Language::Vhdl2008);
  const auto& contextual_assertion =
      contextual_vhdl_severity.design.units.back()
          .processes.front().statements.front();
  require(
      contextual_vhdl_severity.ok()
          && contextual_assertion.vhdl_severity_expression.kind
              == ExpressionKind::Identifier
          && contextual_assertion.vhdl_severity_expression.text
              == "panic",
      "VHDL assertion severity remains a contextual expression");

  const auto expression_sv_report = parse_text(
      "expression_report.sv",
      R"(
module expression_report;
  initial $warning(1'b1, "extra");
endmodule
)",
      Language::SystemVerilog2017);
  require(
      expression_sv_report.ok()
          && expression_sv_report.design.units.front()
                 .processes.front().statements.front().value.text
              == "1'b1"
          && expression_sv_report.design.units.front()
                 .processes.front().statements.front()
                 .task_arguments.size()
              == 1,
      "severity tasks retain expression argument lists");

  const auto missing_assertion_action = parse_text(
      "missing_assertion_action.sv",
      R"(
module missing_assertion_action;
  initial assert (1'b1)
endmodule
)",
      Language::SystemVerilog2017);
  require(
      !missing_assertion_action.ok()
          && std::ranges::any_of(
              missing_assertion_action.diagnostics,
              [](const auto& diagnostic) {
                return diagnostic.code == "FSIM-SV-PARSE-044";
              }),
      "missing immediate-assertion actions need a targeted diagnostic");

  const auto fatal = parse_text(
      "fatal.sv",
      R"(
module fatal_tasks;
  initial begin
    $fatal;
    $fatal("standalone\nfatal");
    $fatal(1, "controlled\tfatal");
    $fatal(1, "formatted=%0d/%0d", 7, 9);
    $fatal("formatted-without-control=%0d", 11);
    assert (1'b0) else $fatal("assertion \"fatal\"");
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(fatal.ok(), "bounded SystemVerilog $fatal forms must parse");
  const auto& fatal_statements =
      fatal.design.units.front().processes.front().statements;
  require(
      fatal_statements.size() == 6
          && fatal_statements[0].kind == StatementKind::Report
          && fatal_statements[1].kind == StatementKind::Report
          && fatal_statements[2].kind == StatementKind::Report
          && fatal_statements[3].kind == StatementKind::Report
          && fatal_statements[0].output_text == "$fatal"
          && fatal_statements[1].output_text == "standalone\nfatal"
          && fatal_statements[2].output_text == "controlled\tfatal"
          && fatal_statements[3].output_text == "formatted=%0d/%0d"
          && fatal_statements[3].task_arguments.size() == 2
          && fatal_statements[3].task_arguments[0].text == "7"
          && fatal_statements[3].task_arguments[1].text == "9"
          && fatal_statements[4].kind == StatementKind::Report
          && fatal_statements[4].output_text
              == "formatted-without-control=%0d"
          && fatal_statements[4].task_arguments.size() == 1
          && fatal_statements[4].task_arguments.front().text == "11"
          && fatal_statements[5].kind == StatementKind::Assert
          && fatal_statements[5].assertion_has_failure_action
          && fatal_statements[5].else_statements.size() == 1
          && fatal_statements[5].else_statements.front().kind
              == StatementKind::Report
          && fatal_statements[5].else_statements.front().output_text
              == "assertion \"fatal\"",
      "standalone, formatted, and assertion-action $fatal metadata");

  const auto severity_tasks = parse_text(
      "severity_tasks.sv",
      R"(
module severity_tasks;
  initial begin
    $info;
    $info();
    $info("note");
    $warning;
    $warning();
    $warning("warning");
    $error;
    $error();
    $error("error");
    $error("beat %0d expected %x got %x", 2, 8'h11, 8'h22);
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(
      severity_tasks.ok(),
      "bounded standalone severity tasks must parse");
  const auto& reports =
      severity_tasks.design.units.front().processes.front().statements;
  require(
      reports.size() == 10
          && std::ranges::all_of(
              reports,
              [](const auto& statement) {
                return statement.kind == StatementKind::Report;
              })
          && reports[0].assertion_severity == AssertionSeverity::Note
          && reports[2].output_text == "note"
          && reports[3].assertion_severity
              == AssertionSeverity::Warning
          && reports[5].output_text == "warning"
          && reports[6].assertion_severity
              == AssertionSeverity::Error
          && reports[8].output_text == "error"
          && reports[9].output_text
              == "beat %0d expected %x got %x"
          && reports[9].task_arguments.size() == 3,
      "standalone severity task forms and metadata");

  const auto verilog_fatal = parse_text(
      "fatal.v",
      "module fatal_v; initial $fatal; endmodule",
      Language::Verilog2005);
  require(
      !verilog_fatal.ok()
          && std::ranges::any_of(
              verilog_fatal.diagnostics,
              [](const auto& diagnostic) {
                return diagnostic.code
                    == "FSIM-VERILOG-SEM-007";
              }),
      "$fatal must remain SystemVerilog-only");

  const auto verilog_report = parse_text(
      "report.v",
      "module report_v; initial $error; endmodule",
      Language::Verilog2005);
  require(
      !verilog_report.ok()
          && std::ranges::any_of(
              verilog_report.diagnostics,
              [](const auto& diagnostic) {
                return diagnostic.code
                    == "FSIM-VERILOG-SEM-009";
              }),
      "severity report tasks must remain SystemVerilog-only");
}

void test_vhdl_literal_report() {
  const auto parsed = parse_text(
      "report.vhd",
      R"(
entity reporter is
end entity;
architecture rtl of reporter is
begin
  process
  begin
    report "vhdl ""quote""" severity note;
    report "" severity warning;
    report "error" severity error;
    wait;
  end process;
end architecture;
)",
      Language::Vhdl2008);
  require(parsed.ok(), "literal VHDL report statements must parse");
  const auto* architecture =
      parsed.design.find(UnitKind::VhdlArchitecture, "rtl");
  require(
      architecture != nullptr
          && architecture->processes.size() == 1
          && architecture->processes.front().statements.size() == 4
          && architecture->processes.front().statements[0].kind
              == StatementKind::Report
          && architecture->processes.front().statements[0].output_text
              == "vhdl \"quote\""
          && architecture->processes.front().statements[0]
                 .assertion_severity == AssertionSeverity::Note
          && architecture->processes.front().statements[1].kind
              == StatementKind::Report
          && architecture->processes.front().statements[1].output_text.empty()
          && architecture->processes.front().statements[1]
                 .assertion_severity == AssertionSeverity::Warning
          && architecture->processes.front().statements[2]
                 .assertion_severity == AssertionSeverity::Error,
      "VHDL report literal HIR, severity, and doubled-quote decoding");

  const auto failure = parse_text(
      "report_failure.vhd",
      R"(
entity reporter is end entity;
architecture rtl of reporter is
begin
  process
  begin
    report "failure" severity failure;
    wait;
  end process;
end architecture;
)",
      Language::Vhdl2008);
  require(
      failure.ok()
          && failure.design
                 .find(UnitKind::VhdlArchitecture, "rtl")
                 ->processes.front()
                 .statements.front()
                 .assertion_severity
              == AssertionSeverity::Failure,
      "failure VHDL report severity HIR");
}

void test_process_variable_declarations() {
  const auto vhdl = parse_text(
      "locals.vhd",
      R"(
entity locals is end entity;
architecture rtl of locals is begin
  worker: process
    variable state : std_logic := '1';
    variable flags : bit_vector(1 downto 0);
  begin
    state := '0';
  end process;
end architecture;
)",
      Language::Vhdl2008);
  require(vhdl.ok(), "bounded VHDL process variables must parse");
  const auto& vhdl_variables =
      vhdl.design.units.back().processes.front().variables;
  require(
      vhdl_variables.size() == 2
          && vhdl_variables[0].name == "state"
          && vhdl_variables[0].type.width() == 1
          && vhdl_variables[0].initializer
          && vhdl_variables[1].name == "flags"
          && vhdl_variables[1].type.width() == 2
          && !vhdl_variables[1].initializer,
      "VHDL process-variable metadata");

  const auto system_verilog = parse_text(
      "locals.sv",
      R"(
module locals;
  initial begin
    logic [3:0] state = 4'b0011;
    bit ready;
    state = 4'b1010;
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(
      system_verilog.ok(),
      "bounded SystemVerilog procedural variables must parse");
  const auto& sv_variables =
      system_verilog.design.units.front().processes.front().variables;
  require(
      sv_variables.size() == 2
          && sv_variables[0].name == "state"
          && sv_variables[0].type.width() == 4
          && sv_variables[0].initializer
          && sv_variables[1].name == "ready"
          && sv_variables[1].type.domain == ValueDomain::Bit2
          && !sv_variables[1].initializer,
      "SystemVerilog procedural-variable metadata");
}

void test_systemverilog_procedural_block_scopes() {
  const auto result = parse_text(
      "block_scopes.sv",
      R"(
module block_scopes;
  logic result;
  initial begin : root_scope
    logic value = 1'b0;
    begin : inner_scope
      logic value = 1'b1;
      result = value;
    end : inner_scope
    begin
      logic anonymous_value;
      result = anonymous_value;
    end
    for (int lane = 0; lane < 2; lane++) begin : iteration
      bit temporary;
      temporary = value;
    end : iteration
    if (result) begin : selected
      logic branch_value;
      result = branch_value;
    end : selected
    else begin : alternate
      logic branch_value;
      result = branch_value;
    end : alternate
  end : root_scope
endmodule
)",
      Language::SystemVerilog2017);
  require(
      result.ok(),
      "named procedural blocks and loop-local declarations must parse");
  const auto& process =
      result.design.units.front().processes.front();
  require(
      process.variables.empty()
          && process.statements.size() == 1
          && process.statements.front().kind
              == StatementKind::Block
          && process.statements.front().label == "root_scope"
          && process.statements.front().declarations.size() == 1,
      "a named process body must remain a lexical HIR block");
  const auto& root = process.statements.front();
  require(
      root.statements.size() == 4
          && root.statements.front().kind
              == StatementKind::Block
          && root.statements.front().label == "inner_scope"
          && root.statements.front().declarations.size() == 1,
      "nested named block declarations and labels");
  require(
      root.statements[1].kind == StatementKind::Block
          && root.statements[1].label.empty()
          && root.statements[1].declarations.size() == 1
          && root.statements[1].declarations.front().name
              == "anonymous_value",
      "an anonymous block with declarations must retain its lexical HIR");
  const auto& loop = root.statements[2];
  require(
      loop.kind == StatementKind::Loop
          && loop.statements.size() == 1
          && loop.statements.front().kind
              == StatementKind::Block
          && loop.statements.front().label == "iteration"
          && loop.statements.front().declarations.size() == 1,
      "a procedural loop must retain its declared lexical block");
  const auto& conditional = root.statements[3];
  require(
      conditional.kind == StatementKind::If
          && conditional.statements.size() == 1
          && conditional.statements.front().kind
              == StatementKind::Block
          && conditional.statements.front().label == "selected"
          && conditional.statements.front().declarations.size() == 1
          && conditional.else_statements.size() == 1
          && conditional.else_statements.front().kind
              == StatementKind::Block
          && conditional.else_statements.front().label == "alternate"
          && conditional.else_statements.front().declarations.size() == 1,
      "conditional branches must retain declared lexical blocks");

  const auto mismatched = parse_text(
      "bad_block_labels.sv",
      R"(
module bad_block_labels;
  initial begin : opening
  end : closing
  initial begin
  end : orphan
endmodule
)",
      Language::SystemVerilog2017);
  require(
      !mismatched.ok()
          && std::ranges::count_if(
                 mismatched.diagnostics,
                 [](const Diagnostic& diagnostic) {
                   return diagnostic.code == "FSIM-SV-SEM-034";
                 })
              == 2,
      "mismatched and orphan procedural end labels need stable diagnostics");
}

void test_procedural_wait_statements() {
  const auto vhdl = parse_text(
      "waits.vhd",
      R"(
entity waits is end entity;
architecture rtl of waits is
  signal trigger : std_logic;
begin
  timer: process
  begin
    wait for 2 ns;
    wait on trigger;
    wait until trigger = '1';
    wait;
    wait on trigger until trigger = '0';
    wait on trigger for 3 ns;
    wait until trigger = '1' for 4 ns;
    wait on trigger until trigger = '0' for 5 ns;
  end process;
end architecture;
)",
      Language::Vhdl2008);
  require(vhdl.ok(), "bounded VHDL wait statements must parse");
  const auto& vhdl_statements =
      vhdl.design.units.back().processes.front().statements;
  require(
      vhdl_statements.size() == 8
          && vhdl_statements[0].kind == StatementKind::Delay
          && vhdl_statements[0].delay
          && vhdl_statements[0].delay->magnitude == 2
          && vhdl_statements[0].delay->unit == "ns"
          && vhdl_statements[1].kind == StatementKind::WaitOn
          && vhdl_statements[1].sensitivities.size() == 1
          && vhdl_statements[1].sensitivities.front().signal
              == "trigger"
          && vhdl_statements[2].kind
              == StatementKind::WaitUntil
          && vhdl_statements[2].condition.kind
              == ExpressionKind::Binary
          && vhdl_statements[2].condition.text == "="
          && vhdl_statements[3].kind
              == StatementKind::WaitUntil
          && vhdl_statements[3].condition.kind
              == ExpressionKind::BooleanLiteral
          && vhdl_statements[3].condition.text == "true"
          && vhdl_statements[4].kind
              == StatementKind::WaitUntil
          && vhdl_statements[4].sensitivities.size() == 1
          && vhdl_statements[4].sensitivities.front().signal
              == "trigger"
          && !vhdl_statements[4].delay
          && vhdl_statements[5].kind
              == StatementKind::WaitOn
          && vhdl_statements[5].delay
          && vhdl_statements[5].delay->magnitude == 3
          && vhdl_statements[6].kind
              == StatementKind::WaitUntil
          && vhdl_statements[6].sensitivities.empty()
          && vhdl_statements[6].delay
          && vhdl_statements[6].delay->magnitude == 4
          && vhdl_statements[7].kind
              == StatementKind::WaitUntil
          && vhdl_statements[7].sensitivities.size() == 1
          && vhdl_statements[7].delay
          && vhdl_statements[7].delay->magnitude == 5,
      "VHDL wait metadata");

  const auto system_verilog = parse_text(
      "events.sv",
      R"(
module events;
  logic trigger;
  logic observed;
  event first_event;
  event second_event;
  initial begin
    @(posedge trigger);
    @(negedge trigger) observed = trigger;
    wait (trigger) observed = 1'b1;
    wait_order (first_event, second_event, first_event)
      observed = 1'b0;
    else
      observed = 1'b1;
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(
      system_verilog.ok(),
      "bounded SystemVerilog procedural event controls must parse");
  const auto& sv_statements =
      system_verilog.design.units.front().processes.front().statements;
  require(
      sv_statements.size() == 4
          && sv_statements[0].kind == StatementKind::WaitOn
          && sv_statements[0].sensitivities.size() == 1
          && sv_statements[0].sensitivities.front().signal == "trigger"
          && sv_statements[0].sensitivities.front().edge
              == EdgeKind::Positive
          && sv_statements[0].statements.empty()
          && sv_statements[1].kind == StatementKind::WaitOn
          && sv_statements[1].sensitivities.front().edge
              == EdgeKind::Negative
          && sv_statements[1].statements.size() == 1
          && sv_statements[1].statements.front().kind
              == StatementKind::Assignment
          && sv_statements[2].kind
              == StatementKind::WaitUntil
          && sv_statements[2].condition.kind
              == ExpressionKind::Identifier
          && sv_statements[2].statements.size() == 1
          && sv_statements[2].statements.front().kind
              == StatementKind::Assignment
          && sv_statements[3].kind
              == StatementKind::WaitOrder
          && sv_statements[3].sensitivities.size() == 3
          && sv_statements[3].sensitivities[0].signal
              == "first_event"
          && sv_statements[3].sensitivities[1].signal
              == "second_event"
          && sv_statements[3].sensitivities[2].signal
              == "first_event"
          && sv_statements[3].statements.size() == 1
          && sv_statements[3].else_statements.size() == 1,
      "SystemVerilog procedural event metadata");

  const auto malformed_wait_order = parse_text(
      "bad_wait_order.sv",
      R"(
module bad_wait_order;
  event first_event;
  event second_event;
  initial wait_order () ;
  initial wait_order (first_event | second_event) ;
endmodule
)",
      Language::SystemVerilog2017);
  require(
      !malformed_wait_order.ok()
          && std::ranges::any_of(
              malformed_wait_order.diagnostics,
              [](const Diagnostic& diagnostic) {
                  return diagnostic.code
                      == "FSIM-SV-PARSE-365";
              })
          && std::ranges::any_of(
              malformed_wait_order.diagnostics,
              [](const Diagnostic& diagnostic) {
                  return diagnostic.code
                      == "FSIM-SV-SEM-243";
              }),
      "malformed wait_order statements receive stable diagnostics");

  const auto malformed_sv_wait = parse_text(
      "bad_wait.sv",
      R"(
module bad_wait;
  logic trigger;
  initial wait trigger;
  initial wait (trigger;
endmodule
)",
      Language::SystemVerilog2017);
  require(
      !malformed_sv_wait.ok()
          && std::ranges::any_of(
              malformed_sv_wait.diagnostics,
              [](const Diagnostic& diagnostic) {
                return diagnostic.code
                    == "FSIM-SV-PARSE-109";
              })
          && std::ranges::any_of(
              malformed_sv_wait.diagnostics,
              [](const Diagnostic& diagnostic) {
                return diagnostic.code
                    == "FSIM-SV-PARSE-110";
              }),
      "malformed condition waits receive stable diagnostics");

  const auto invalid_vhdl = parse_text(
      "bad_wait.vhd",
      R"(
entity bad_wait is end entity;
architecture rtl of bad_wait is
  signal trigger : std_logic;
begin
  worker: process(trigger)
  begin
    wait on trigger;
  end process;
end architecture;
)",
      Language::Vhdl2008);
  require(
      !invalid_vhdl.ok()
          && std::any_of(
              invalid_vhdl.diagnostics.begin(),
              invalid_vhdl.diagnostics.end(),
              [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-VHDL-SEM-012";
              }),
      "VHDL sensitivity-list/wait conflict diagnostic");

  const auto nested_vhdl = parse_text(
      "nested_wait.vhd",
      R"(
entity nested_wait is end entity;
architecture rtl of nested_wait is begin
  worker: process begin
    if 1 = 1 then
      wait for 1 ns;
    end if;
  end process;
end architecture;
)",
      Language::Vhdl2008);
  require(
      nested_vhdl.ok()
          && nested_vhdl.design.units.back()
                 .processes.front().statements.front().statements.front().kind
              == StatementKind::Delay,
      "nested VHDL waits retain their exact statement tree");

  const auto wildcard_sv = parse_text(
      "wildcard_event.sv",
      R"(
module wildcard_event;
  logic trigger;
  logic observed;
  initial @* observed = trigger;
endmodule
)",
      Language::SystemVerilog2017);
  require(
      wildcard_sv.ok(),
      "dynamic wildcard procedural event must parse");
  const auto& wildcard_wait =
      wildcard_sv.design.units.front()
          .processes.front()
          .statements.front();
  require(
      wildcard_wait.kind == StatementKind::WaitOn
          && wildcard_wait.sensitivities.size() == 1
          && wildcard_wait.sensitivities.front().signal == "*"
          && wildcard_wait.statements.size() == 1,
      "dynamic wildcard event metadata");
}

} // namespace fsim::tests::frontend
