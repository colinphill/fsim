// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

namespace fsim::tests::elaboration {

void test_systemverilog_function_lowering() {
  const auto parsed = fsim::frontend::parse_text(
      "function_runtime.sv",
      R"(
module function_runtime #(
    parameter int WIDTH = 8
) (
    input logic select,
    input logic [WIDTH-1:0] value,
    output logic [WIDTH-1:0] result
);
  function automatic logic [WIDTH-1:0] increment(
      input logic [WIDTH-1:0] argument);
    return argument + 1;
  endfunction

  function automatic logic [WIDTH-1:0] choose(
      input logic condition,
      input logic [WIDTH-1:0] argument);
    logic [WIDTH-1:0] temporary;
    if (condition)
      temporary = increment(argument);
    else
      temporary = argument;
    choose = temporary;
  endfunction

  initial result = choose(select, value);
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(parsed.ok());
  const auto elaborated = fsim::elaboration::elaborate(
      parsed.design, "sv:work.function_runtime");
  assert(elaborated.ok());
  const auto select =
      elaborated.design->find_signal("select");
  const auto value =
      elaborated.design->find_signal("value");
  const auto result =
      elaborated.design->find_signal("result");
  assert(select && value && result);
  const auto& operations =
      elaborated.design->processes().front().operations;
  assert(std::ranges::any_of(
      operations,
      [](const auto& operation) {
        return std::holds_alternative<
            fsim::runtime::simir::Call>(operation);
      }));
  assert(std::ranges::any_of(
      operations,
      [](const auto& operation) {
        return std::holds_alternative<
            fsim::runtime::simir::Return>(operation);
      }));

  auto interpreter =
      elaborated.design->create_interpreter();
  interpreter->deposit_signal(
      *select,
      fsim::runtime::PackedLogic4::from_msb_string("1"));
  interpreter->deposit_signal(
      *value,
      fsim::runtime::PackedLogic4::from_msb_string("00101001"));
  const auto run = interpreter->run();
  assert(run.status == fsim::runtime::RunStatus::completed);
  assert(
      interpreter->signal_value(*result).to_msb_string()
      == "00101010");

  const auto recursive = fsim::frontend::parse_text(
      "recursive_function.sv",
      R"(
module recursive_function(
    input logic value,
    output logic result);
  function automatic logic recurse(input logic argument);
    recurse = argument ? recurse(argument) : 1'b0;
  endfunction
  initial result = recurse(value);
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(recursive.ok());
  const auto rejected = fsim::elaboration::elaborate(
      recursive.design, "sv:work.recursive_function");
  assert(
      !rejected.ok()
      && has_diagnostic(rejected, "FSIM-ELAB-SVFUNC-006"));

  const auto wrong_arity = fsim::frontend::parse_text(
      "function_arity.sv",
      R"(
module function_arity(output logic result);
  function automatic logic identity(input logic argument);
    identity = argument;
  endfunction
  initial result = identity();
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(wrong_arity.ok());
  const auto rejected_arity = fsim::elaboration::elaborate(
      wrong_arity.design, "sv:work.function_arity");
  assert(
      !rejected_arity.ok()
      && has_diagnostic(
          rejected_arity, "FSIM-ELAB-SVFUNC-003"));

  const auto packages = fsim::frontend::parse_text(
      "package_functions.sv",
      R"(
package math_pkg;
  function automatic logic [7:0] add_two(
      input logic [7:0] argument);
    return argument + 2;
  endfunction
endpackage

module imported_function(output logic [7:0] result);
  import math_pkg::*;
  initial result = add_two(8'd40);
endmodule

module qualified_function(output logic [7:0] result);
  initial result = math_pkg::add_two(8'd40);
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(packages.ok());
  for (const auto top :
       {"sv:work.imported_function",
        "sv:work.qualified_function"}) {
    const auto package_elaborated =
        fsim::elaboration::elaborate(packages.design, top);
    assert(package_elaborated.ok());
    const auto package_result =
        package_elaborated.design->find_signal("result");
    assert(package_result);
    auto package_interpreter =
        package_elaborated.design->create_interpreter();
    assert(
        package_interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    assert(
        package_interpreter
            ->signal_value(*package_result)
            .to_msb_string()
        == "00101010");
  }

  const auto constant = fsim::frontend::parse_text(
      "constant_function.sv",
      R"(
module constant_function(output logic [3:0] result);
  function automatic int width_for(input int argument);
    if (argument > 3)
      return 4;
    return 2;
  endfunction
  localparam int WIDTH = width_for(5);
  logic [WIDTH-1:0] internal;
  if (width_for(5) == 4) begin : selected
    initial begin
      internal = 4'b1010;
      result = internal;
    end
  end
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(constant.ok());
  const auto constant_elaborated =
      fsim::elaboration::elaborate(
          constant.design, "sv:work.constant_function");
  assert(constant_elaborated.ok());
  const auto internal =
      constant_elaborated.design->find_signal("internal");
  const auto constant_result =
      constant_elaborated.design->find_signal("result");
  assert(internal && constant_result);
  assert(
      constant_elaborated.design->signals().at(*internal).width
      == 4);
  auto constant_interpreter =
      constant_elaborated.design->create_interpreter();
  assert(
      constant_interpreter->run().status
      == fsim::runtime::RunStatus::completed);
  assert(
      constant_interpreter
          ->signal_value(*constant_result)
          .to_msb_string()
      == "1010");
}

} // namespace fsim::tests::elaboration
