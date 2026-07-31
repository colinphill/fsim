// SPDX-License-Identifier: Apache-2.0
#include "frontend_test_support.hpp"
#include "fsim/frontend/frontend.hpp"

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
}

}  // namespace fsim::tests::frontend
