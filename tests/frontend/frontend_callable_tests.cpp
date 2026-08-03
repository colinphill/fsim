// SPDX-License-Identifier: Apache-2.0
#include "frontend_test_support.hpp"
#include "fsim/frontend/frontend.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::tests::frontend {
namespace {

void require(const bool condition, const std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string{message});
  }
}

}  // namespace

void test_systemverilog_callable_closure_declarations() {
  using namespace fsim::frontend;
  const auto parsed = parse_text(
      "callable_closure.sv",
      R"(
module callable_closure;
  logic [7:0] value;
  logic [7:0] copied;
  logic [7:0] result;
  function logic [7:0] classic(source, destination);
    input logic [7:0] source;
    output logic [7:0] destination;
    destination = source;
    classic = source + 1;
  endfunction
  function automatic logic [7:0] adjust(
      ref logic [7:0] target,
      input logic [7:0] amount = 8'd2);
    target = target + amount;
    return target;
  endfunction
  task implicit_task(source, destination);
    input logic [7:0] source;
    output logic [7:0] destination;
    destination = source;
  endtask
  task automatic ref_task(
      ref logic [7:0] target,
      input logic [7:0] amount = 8'd1);
    target = target + amount;
  endtask
  initial begin
    result = classic(.destination(copied), .source(value));
    result = adjust(.target(value));
    implicit_task(.destination(value), .source(8'd4));
    ref_task(.target(value));
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(parsed.ok(), "callable closure forms must parse");
  const auto& unit = parsed.design.units.front();
  require(
      unit.functions.size() == 2
          && !unit.functions[0].automatic
          && !unit.functions[0].lifetime_explicit
          && unit.functions[0].arguments.size() == 2
          && unit.functions[0].arguments[1].direction
              == PortDirection::Output
          && unit.functions[1].automatic
          && unit.functions[1].arguments[0].reference
          && unit.functions[1].arguments[1].default_value,
      "classic, lifetime, writable, ref, and default function HIR");
  require(
      unit.tasks.size() == 2
          && !unit.tasks[0].automatic
          && !unit.tasks[0].lifetime_explicit
          && unit.tasks[0].arguments.size() == 2
          && unit.tasks[1].arguments[0].reference
          && unit.tasks[1].arguments[1].default_value,
      "classic, lifetime, ref, and default task HIR");
  const auto& statements = unit.processes.front().statements;
  require(
      statements[0].value.call_argument_names
          == std::vector<std::string>{"destination", "source"}
          && statements[1].value.call_argument_names
              == std::vector<std::string>{"target"}
          && statements[2].task_argument_names
              == std::vector<std::string>{"destination", "source"}
          && statements[3].task_argument_names
              == std::vector<std::string>{"target"},
      "named callable actuals retain source association order");

  const auto has_code = [](const ParseResult& result, const std::string_view code) {
    return std::ranges::any_of(
        result.diagnostics,
        [code](const Diagnostic& diagnostic) {
          return diagnostic.code == code;
        });
  };
  const auto missing = parse_text(
      "classic_missing.sv",
      "module m; function logic f(a); f = 1'b0; endfunction endmodule",
      Language::SystemVerilog2017);
  require(
      !missing.ok() && has_code(missing, "FSIM-SV-SEM-058"),
      "classic function header arguments require body declarations");
  const auto extra = parse_text(
      "classic_extra.sv",
      "module m; function logic f(a); input logic a; input logic b; "
      "f = a; endfunction endmodule",
      Language::SystemVerilog2017);
  require(
      !extra.ok() && has_code(extra, "FSIM-SV-SEM-058"),
      "classic function body declarations must appear in the header");
  const auto duplicate = parse_text(
      "classic_duplicate.sv",
      "module m; function logic f(a, a); input logic a; "
      "f = a; endfunction endmodule",
      Language::SystemVerilog2017);
  require(
      !duplicate.ok() && has_code(duplicate, "FSIM-SV-SEM-058"),
      "classic function header arguments are unique");
  const auto delimiter = parse_text(
      "classic_delimiter.sv",
      "module m; function logic f(a); input logic a f = a; "
      "endfunction endmodule",
      Language::SystemVerilog2017);
  require(
      !delimiter.ok() && has_code(delimiter, "FSIM-SV-PARSE-197"),
      "classic function body declarations require delimiters");
  const auto end_name = parse_text(
      "classic_end_name.sv",
      "module m; task t(a); input logic a; endtask : other endmodule",
      Language::SystemVerilog2017);
  require(
      !end_name.ok() && has_code(end_name, "FSIM-SV-SEM-068"),
      "classic task end names must match");
}

}  // namespace fsim::tests::frontend
