// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"
#include "fsim/runtime/simir_interpreter.hpp"
#include "fsim/frontend/parser.hpp"
#include "fsim/frontend/token.hpp"
#include "fsim/runtime/scheduler.hpp"
#include "fsim/runtime/simir.hpp"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <iostream>

namespace fsim::tests::elaboration {

void test_systemverilog_task_lowering()
{
    const auto parsed = fsim::frontend::parse_text(
        "task_runtime.sv",
        R"(
package task_pkg;
  task automatic add_two(
      input logic [7:0] value,
      output logic [7:0] result);
    result = value + 2;
  endtask
endpackage

module task_runtime #(
    parameter int WIDTH = 8
) (
    output logic [WIDTH-1:0] result,
    output logic [WIDTH-1:0] accumulator
);
  import task_pkg::*;

  function automatic logic [WIDTH-1:0] increment(
      input logic [WIDTH-1:0] value);
    return value + 1;
  endfunction

  task automatic accumulate(
      input logic [WIDTH-1:0] value,
      output logic [WIDTH-1:0] transformed,
      inout logic [WIDTH-1:0] total);
    logic [WIDTH-1:0] temporary;
    add_two(increment(value), temporary);
    transformed = temporary;
    total = total + temporary;
    return;
    total = 0;
  endtask

  initial begin
    accumulator = 8'd1;
    accumulate(8'd39, result, accumulator);
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(parsed.ok());
    const auto elaborated = compile_and_elaborate(
        parsed.design, "sv:work.task_runtime");
    assert(elaborated.ok());
    const auto result = elaborated.design->find_signal("result");
    const auto accumulator = elaborated.design->find_signal("accumulator");
    assert(result && accumulator);
    const auto& operations = elaborated.design->processes().front().operations;
    assert(std::ranges::count_if(
               operations,
               [](const auto& operation) {
                   return fsim::runtime::simir::operation_holds<
                       fsim::runtime::simir::Call>(operation);
               })
        >= 3);
    assert(std::ranges::count_if(
               operations,
               [](const auto& operation) {
                   return fsim::runtime::simir::operation_holds<
                       fsim::runtime::simir::Return>(operation);
               })
        >= 3);

    auto interpreter = elaborated.design->create_interpreter();
    assert(
        interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    assert(
        interpreter->signal_value(*result).to_msb_string()
        == "00101010");
    assert(
        interpreter->signal_value(*accumulator).to_msb_string()
        == "00101011");

    const auto association_error = fsim::frontend::parse_text(
        "task_association_error.sv",
        R"(
module task_association_error(output logic result);
  task automatic selected(
      input logic left = 1'b0,
      output logic right);
    right = left;
  endtask
  initial selected(.missing(1'b1), .right(result));
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(association_error.ok());
    const auto rejected_association = compile_and_elaborate(
        association_error.design,
        "sv:work.task_association_error");
    assert(
        !rejected_association.ok()
        && has_diagnostic(
            rejected_association, "FSIM-ELAB-SVTASK-012"));

    const auto ref_signal = fsim::frontend::parse_text(
        "task_ref_signal.sv",
        R"(
module task_ref_signal(output logic result);
  logic value;
  task automatic mutate(ref logic target);
    target = 1'b1;
  endtask
  initial begin
    mutate(value);
    result = value;
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(ref_signal.ok());
    const auto elaborated_ref_signal = compile_and_elaborate(
        ref_signal.design, "sv:work.task_ref_signal");
    assert(elaborated_ref_signal.ok());
    auto ref_signal_interpreter = elaborated_ref_signal.design->create_interpreter();
    const auto ref_signal_value = elaborated_ref_signal.design->find_signal("value");
    const auto ref_signal_result = elaborated_ref_signal.design->find_signal("result");
    assert(ref_signal_value && ref_signal_result);
    assert(
        ref_signal_interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    assert(
        ref_signal_interpreter
                ->signal_value(*ref_signal_value)
                .to_msb_string()
            == "1"
        && ref_signal_interpreter
                ->signal_value(*ref_signal_result)
                .to_msb_string()
            == "1");

    const auto ref_literal = fsim::frontend::parse_text(
        "task_ref_literal.sv",
        R"(
module task_ref_literal;
  task automatic mutate(ref logic target);
    target = 1'b1;
  endtask
  initial mutate(1'b0);
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(ref_literal.ok());
    const auto rejected_ref_literal = compile_and_elaborate(
        ref_literal.design, "sv:work.task_ref_literal");
    assert(
        !rejected_ref_literal.ok()
        && has_diagnostic(
            rejected_ref_literal, "FSIM-ELAB-SVTASK-013"));

    // IEEE 1800-2023 16.4: an automatic variable cannot be the actual of a
    // ref formal of a deferred assertion action.
    const auto deferred_automatic_ref = fsim::frontend::parse_text(
        "deferred_automatic_ref.sv",
        R"(
module deferred_automatic_ref;
  task automatic note(ref int target);
    $display("%0d", target);
  endtask
  initial begin
    automatic int local_value = 3;
    assert #0 (1'b0) else note(local_value);
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(deferred_automatic_ref.ok());
    const auto rejected_deferred_automatic_ref = compile_and_elaborate(
        deferred_automatic_ref.design, "sv:work.deferred_automatic_ref");
    assert(
        !rejected_deferred_automatic_ref.ok()
        && has_diagnostic(
            rejected_deferred_automatic_ref, "FSIM-ELAB-SVASSERT-003"));

    // A static variable is a legal ref actual of an observed action.
    const auto deferred_static_ref = fsim::frontend::parse_text(
        "deferred_static_ref.sv",
        R"(
module deferred_static_ref;
  int shared_value = 3;
  task automatic note(ref int target);
    $display("%0d", target);
  endtask
  initial assert #0 (1'b0) else note(shared_value);
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(deferred_static_ref.ok());
    const auto accepted_deferred_static_ref = compile_and_elaborate(
        deferred_static_ref.design, "sv:work.deferred_static_ref");
    assert(
        accepted_deferred_static_ref.ok()
        && !has_diagnostic(
            accepted_deferred_static_ref, "FSIM-ELAB-SVASSERT-003"));

    // 4.4.2.9: a final deferred action runs in the Postponed region, where a
    // task that writes a variable cannot be called.
    const auto final_writing_task = fsim::frontend::parse_text(
        "final_writing_task.sv",
        R"(
module final_writing_task;
  int counter;
  task automatic bump;
    counter = counter + 1;
  endtask
  initial assert final (1'b1) bump();
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(final_writing_task.ok());
    const auto rejected_final_writing_task = compile_and_elaborate(
        final_writing_task.design, "sv:work.final_writing_task");
    assert(
        !rejected_final_writing_task.ok()
        && has_diagnostic(
            rejected_final_writing_task, "FSIM-ELAB-SVASSERT-004"));

    const auto final_reading_task = fsim::frontend::parse_text(
        "final_reading_task.sv",
        R"(
module final_reading_task;
  int counter = 2;
  task automatic show;
    int copy;
    copy = counter;
    $display("%0d", copy);
  endtask
  initial assert final (1'b1) show();
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(final_reading_task.ok());
    const auto accepted_final_reading_task = compile_and_elaborate(
        final_reading_task.design, "sv:work.final_reading_task");
    assert(
        accepted_final_reading_task.ok()
        && !has_diagnostic(
            accepted_final_reading_task, "FSIM-ELAB-SVASSERT-004"));

    const auto static_suspension = fsim::frontend::parse_text(
        "static_task_suspension.sv",
        R"(
module static_task_suspension;
  task retained;
    #1;
  endtask
  initial retained();
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(static_suspension.ok());
    const auto elaborated_static_suspension = compile_and_elaborate(
        static_suspension.design,
        "sv:work.static_task_suspension");
    assert(elaborated_static_suspension.ok());
    auto static_suspension_interpreter = elaborated_static_suspension.design->create_interpreter();
    const auto static_suspension_run = static_suspension_interpreter->run();
    assert(
        static_suspension_run.status
            == fsim::runtime::RunStatus::completed
        && static_suspension_run.time == 1);

    const auto static_container = fsim::frontend::parse_text(
        "static_task_container.sv",
        R"(
module static_task_container;
  task retained;
    logic values[1:0];
    values[0] = 1'b1;
  endtask
  initial retained();
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(static_container.ok());
    const auto elaborated_static_container = compile_and_elaborate(
        static_container.design,
        "sv:work.static_task_container");
    assert(elaborated_static_container.ok());
    auto static_container_interpreter = elaborated_static_container.design->create_interpreter();
    assert(
        static_container_interpreter->run().status
        == fsim::runtime::RunStatus::completed);

    const auto qualified = fsim::frontend::parse_text(
        "qualified_task.sv",
        R"(
package qualified_pkg;
  task automatic assign_value(
      output logic [7:0] result);
    result = 8'd42;
  endtask
endpackage
module qualified_task(output logic [7:0] result);
  initial qualified_pkg::assign_value(result);
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(qualified.ok());
    const auto qualified_elaborated = compile_and_elaborate(
        qualified.design, "sv:work.qualified_task");
    assert(qualified_elaborated.ok());
    auto qualified_interpreter = qualified_elaborated.design->create_interpreter();
    const auto qualified_result = qualified_elaborated.design->find_signal("result");
    assert(qualified_result);
    assert(
        qualified_interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    assert(
        qualified_interpreter
            ->signal_value(*qualified_result)
            .to_msb_string()
        == "00101010");

    const auto suspending = fsim::frontend::parse_text(
        "suspending_task.sv",
        R"(
`timescale 1ns/1ns
module suspending_task(
    output logic [7:0] result,
    output logic [7:0] total,
    output logic [7:0] early);
  event kick;
  logic ready;

  task automatic inner(
      input logic [7:0] value,
      output logic [7:0] transformed,
      inout logic [7:0] accumulator);
    logic [7:0] temporary;
    temporary = value;
    #2;
    transformed = temporary + 2;
    @(kick);
    wait (ready);
    accumulator = accumulator + transformed;
  endtask

  task automatic outer(
      input logic [7:0] value,
      output logic [7:0] transformed,
      inout logic [7:0] accumulator);
    #1;
    inner(value, transformed, accumulator);
  endtask

  initial begin
    result = 0;
    total = 1;
    outer(40, result, total);
  end

  initial begin
    #4;
    -> kick;
    early = result;
    #1;
    ready = 1;
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(suspending.ok());
    const auto elaborated_suspending = compile_and_elaborate(
        suspending.design, "sv:work.suspending_task");
    if (!elaborated_suspending.ok()) {
        for (const auto& diagnostic :
            elaborated_suspending.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(elaborated_suspending.ok());
    std::size_t calls = 0;
    std::size_t returns = 0;
    std::size_t waits = 0;
    for (const auto& process :
        elaborated_suspending.design->processes()) {
        calls += static_cast<std::size_t>(std::ranges::count_if(
            process.operations,
            [](const auto& operation) {
                return fsim::runtime::simir::operation_holds<
                    fsim::runtime::simir::Call>(operation);
            }));
        returns += static_cast<std::size_t>(std::ranges::count_if(
            process.operations,
            [](const auto& operation) {
                return fsim::runtime::simir::operation_holds<
                    fsim::runtime::simir::Return>(operation);
            }));
        waits += static_cast<std::size_t>(std::ranges::count_if(
            process.operations,
            [](const auto& operation) {
                return fsim::runtime::simir::operation_holds<
                           fsim::runtime::simir::WaitFor>(operation)
                    || fsim::runtime::simir::operation_holds<
                        fsim::runtime::simir::WaitOn>(operation);
            }));
    }
    assert(calls >= 2 && returns >= 2 && waits >= 5);
    auto suspending_interpreter = elaborated_suspending.design->create_interpreter();
    assert(
        suspending_interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    const auto suspended_result = elaborated_suspending.design->find_signal("result");
    const auto suspended_total = elaborated_suspending.design->find_signal("total");
    const auto early_result = elaborated_suspending.design->find_signal("early");
    assert(suspended_result && suspended_total && early_result);
    assert(
        suspending_interpreter
            ->signal_value(*suspended_result)
            .to_msb_string()
        == "00101010");
    assert(
        suspending_interpreter
            ->signal_value(*suspended_total)
            .to_msb_string()
        == "00101011");
    assert(
        suspending_interpreter
            ->signal_value(*early_result)
            .to_msb_string()
        == "00000000");

    const auto forbidden_suspension = fsim::frontend::parse_text(
        "forbidden_suspending_task.sv",
        R"(
module forbidden_suspending_task(
    input logic source,
    output logic sink);
  task automatic delayed;
    #1;
  endtask
  task automatic wrapper;
    delayed();
  endtask
  final wrapper();
  always_comb begin
    sink = source;
    wrapper();
  end
  always_latch begin
    sink = source;
    wrapper();
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(forbidden_suspension.ok());
    const auto rejected_suspension = compile_and_elaborate(
        forbidden_suspension.design,
        "sv:work.forbidden_suspending_task");
    assert(
        !rejected_suspension.ok()
        && has_diagnostic(
            rejected_suspension, "FSIM-ELAB-SVTASK-010"));

    const auto event_process = fsim::frontend::parse_text(
        "event_process_task.sv",
        R"(
module event_process_task(
    input logic clock,
    output logic value);
  task automatic delayed;
    #1;
    value = 1;
  endtask
  always @(posedge clock) delayed();
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(event_process.ok());
    assert(
        compile_and_elaborate(
            event_process.design, "sv:work.event_process_task")
            .ok());

    const auto recursive = fsim::frontend::parse_text(
        "recursive_task.sv",
        R"(
module recursive_task;
  task automatic recurse;
    recurse();
  endtask
  initial recurse();
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(recursive.ok());
    const auto recursive_result = compile_and_elaborate(
        recursive.design, "sv:work.recursive_task");
    assert(recursive_result.ok());
    const auto& recursive_operations = recursive_result.design->processes().front().operations;
    assert(std::ranges::any_of(
        recursive_operations,
        [](const auto& operation) {
            return fsim::runtime::simir::operation_holds<
                fsim::runtime::simir::CallableFramePush>(operation);
        }));

    const auto wrong_arity = fsim::frontend::parse_text(
        "task_arity.sv",
        R"(
module task_arity;
  task automatic consume(input logic value);
  endtask
  initial consume();
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(wrong_arity.ok());
    const auto rejected_arity = compile_and_elaborate(
        wrong_arity.design, "sv:work.task_arity");
    assert(
        !rejected_arity.ok()
        && has_diagnostic(
            rejected_arity, "FSIM-ELAB-SVTASK-005"));
}

} // namespace fsim::tests::elaboration
