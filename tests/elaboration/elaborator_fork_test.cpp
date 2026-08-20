// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

namespace fsim::tests::elaboration {

void test_systemverilog_fork_lowering()
{
    using namespace fsim::runtime::simir;
    const auto parsed = fsim::frontend::parse_text(
        "forks.sv",
        R"(
module forks;
  logic [7:0] result;
  initial begin
    result = 0;
    fork : all_children
      result[0] = 1;
      #2 result[1] = 1;
    join : all_children
    result[2] = 1;
    fork
      #1 result[3] = 1;
      #3 result[4] = 1;
    join_any
    result[5] = 1;
    wait fork;
    fork
      #5 result[6] = 1;
    join_none
    result[7] = 1;
    disable fork;
    #6;
    $finish;
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(parsed.ok());
    const auto elaborated = fsim::elaboration::elaborate(parsed.design, "forks");
    if (!elaborated.ok()) {
        for (const auto& diagnostic : elaborated.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(elaborated.ok());
    assert(elaborated.design->processes().size() == 1);
    const auto& operations = elaborated.design->processes().front().operations;
    assert(
        std::count_if(
            operations.begin(), operations.end(),
            [](const auto& operation) {
                return fsim::runtime::simir::operation_holds<Fork>(operation);
            })
        == 3);
    assert(
        std::count_if(
            operations.begin(), operations.end(),
            [](const auto& operation) {
                return fsim::runtime::simir::operation_holds<ForkEnd>(operation);
            })
        == 5);
    assert(std::any_of(
        operations.begin(), operations.end(),
        [](const auto& operation) {
            return fsim::runtime::simir::operation_holds<WaitFork>(operation);
        }));
    assert(std::any_of(
        operations.begin(), operations.end(),
        [](const auto& operation) {
            return fsim::runtime::simir::operation_holds<DisableFork>(operation);
        }));

    const auto result = elaborated.design->find_signal("result");
    assert(result);
    auto interpreter = elaborated.design->create_interpreter();
    const auto run = interpreter->run();
    assert(
        run.status == fsim::runtime::RunStatus::stopped
        && run.time == 11
        && interpreter->signal_value(*result).to_msb_string()
            == "10111111");

    const auto named_disable = fsim::frontend::parse_text(
        "named_disable.v",
        R"(
module named_disable;
  reg [2:0] result;
  initial begin
    result = 3'b001;
    fork : stopped_children
      disable stopped_children;
      #2 result = 3'b000;
    join : stopped_children
    begin : outer
      begin : inner
        result[1] = 1'b1;
        disable outer;
        result = 3'b000;
      end
      result = 3'b000;
    end
    begin : parallel_outer
      fork
        disable parallel_outer;
        result[0] = 1'b0;
      join
      result = 3'b000;
    end
    result[2] = 1'b1;
  end
  initial #1 $finish;
endmodule
)",
        fsim::frontend::Language::Verilog2005);
    assert(named_disable.ok());
    const auto named_disable_elaborated = fsim::elaboration::elaborate(
        named_disable.design, "named_disable");
    if (!named_disable_elaborated.ok()) {
        for (const auto& diagnostic : named_disable_elaborated.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(named_disable_elaborated.ok());
    const auto named_result = named_disable_elaborated.design->find_signal("result");
    assert(named_result);
    auto named_interpreter = named_disable_elaborated.design->create_interpreter();
    const auto named_run = named_interpreter->run();
    assert(
        named_run.status == fsim::runtime::RunStatus::stopped
        && named_run.time == 1
        && named_interpreter->signal_value(*named_result)
                .to_msb_string()
            == "111");

    const auto unknown_disable = fsim::frontend::parse_text(
        "unknown_disable.v",
        R"(
module unknown_disable;
  initial disable missing;
endmodule
)",
        fsim::frontend::Language::Verilog2005);
    assert(unknown_disable.ok());
    const auto rejected_disable = fsim::elaboration::elaborate(
        unknown_disable.design, "unknown_disable");
    assert(!rejected_disable.ok());
    assert(has_diagnostic(
        rejected_disable, "FSIM-ELAB-SVDISABLE-001"));

    const auto handles = fsim::frontend::parse_text(
        "process_handles.sv",
        R"(
module process_handles;
  process handle;
  int status_value;
  bit completed;
  string random_state;
  initial begin
    fork
      begin
        handle = process::self();
        #1;
      end
    join_none
    #0;
    status_value = handle.status();
    completed = handle.completed();
    handle.await();
    handle.kill();
    handle.suspend();
    handle.resume();
    random_state = handle.get_randstate();
    handle.set_randstate(random_state);
    handle.srandom(32'h1234);
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(handles.ok());
    assert(
        handles.design.units.front().signals.front().type.spelling
        == "process");
    const auto handles_elaborated = fsim::elaboration::elaborate(handles.design, "process_handles");
    if (!handles_elaborated.ok()) {
        for (const auto& diagnostic : handles_elaborated.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(handles_elaborated.ok());
    const auto& handle_operations = handles_elaborated.design->processes().front().operations;
    const auto has_operation = [&](const auto* tag) {
        using OperationType = std::remove_cv_t<
            std::remove_pointer_t<decltype(tag)>>;
        return std::ranges::any_of(
            handle_operations,
            [](const auto& operation) {
                return fsim::runtime::simir::operation_holds<
                    OperationType>(operation);
            });
    };
    assert(has_operation(static_cast<ProcessSelf*>(nullptr)));
    assert(has_operation(static_cast<ProcessStatusQuery*>(nullptr)));
    assert(has_operation(static_cast<ProcessCompleted*>(nullptr)));
    assert(has_operation(static_cast<ProcessAwait*>(nullptr)));
    assert(has_operation(static_cast<ProcessKill*>(nullptr)));
    assert(has_operation(static_cast<ProcessSuspend*>(nullptr)));
    assert(has_operation(static_cast<ProcessResume*>(nullptr)));
    assert(has_operation(static_cast<ProcessGetRandState*>(nullptr)));
    assert(has_operation(static_cast<ProcessSetRandState*>(nullptr)));
    assert(has_operation(static_cast<ProcessSrandom*>(nullptr)));

    const auto bounded_task = fsim::frontend::parse_text(
        "bounded-task-fork.sv",
        R"(
module bounded_task_fork;
  logic [2:0] result;
  task automatic update_result;
    fork
      #1 result[0] = 1'b1;
      #2 result[1] = 1'b1;
    join
    result[2] = 1'b1;
  endtask
  initial begin
    result = '0;
    update_result();
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(bounded_task.ok());
    const auto bounded_task_elaborated = fsim::elaboration::elaborate(
        bounded_task.design, "bounded_task_fork");
    assert(bounded_task_elaborated.ok());
    auto bounded_task_interpreter =
        bounded_task_elaborated.design->create_interpreter();
    const auto bounded_task_result = bounded_task_interpreter->run();
    assert(
        bounded_task_result.status == fsim::runtime::RunStatus::completed
        && bounded_task_result.time == 2);
    const auto bounded_task_value =
        bounded_task_elaborated.design->find_signal("result");
    assert(bounded_task_value);
    assert(
        bounded_task_interpreter->signal_value(*bounded_task_value).low_word().aval
        == 7);

    const auto bounded_task_frame = fsim::frontend::parse_text(
        "bounded-task-fork-frame.sv",
        R"(
module bounded_task_fork_frame;
  reg [7:0] memory[0:3];
  reg [7:0] observed;
  task automatic capture;
    integer index;
    begin
      for (index = 0; index < 4; index = index + 1)
        memory[index] = index + 8'h41;
      fork
        begin
          index = 0;
          observed = memory[index];
        end
        #1;
      join
    end
  endtask
  initial begin
    capture();
    assert (observed == 8'h41);
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(bounded_task_frame.ok());
    const auto bounded_task_frame_elaborated =
        fsim::elaboration::elaborate(
            bounded_task_frame.design,
            "bounded_task_fork_frame");
    assert(bounded_task_frame_elaborated.ok());
    auto bounded_task_frame_interpreter =
        bounded_task_frame_elaborated.design->create_interpreter();
    const auto bounded_task_frame_result =
        bounded_task_frame_interpreter->run();
    assert(
        bounded_task_frame_result.status
            == fsim::runtime::RunStatus::completed
        && bounded_task_frame_result.time == 1);

    const auto callable = fsim::frontend::parse_text(
        "callable_fork.sv",
        R"(
module callable_fork(output logic value);
  function automatic logic compute();
    fork
      return 1'b1;
    join
  endfunction
  assign value = compute();
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(!callable.ok());
    assert(std::any_of(
        callable.diagnostics.begin(), callable.diagnostics.end(),
        [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-SV-SEM-064";
        }));

    auto malformed = fsim::frontend::parse_text(
        "malformed_callable_fork.sv",
        R"(
module callable_fork(output logic value);
  function automatic logic compute();
    return 1'b1;
  endfunction
  assign value = compute();
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(malformed.ok());
    auto fork = fsim::frontend::Statement { };
    fork.kind = fsim::frontend::StatementKind::Fork;
    fork.span = malformed.design.units.front().functions.front().span;
    auto child = fsim::frontend::Statement { };
    child.kind = fsim::frontend::StatementKind::Null;
    child.span = fork.span;
    fork.statements.push_back(std::move(child));
    malformed.design.units.front().functions.front().statements = {
        std::move(fork)
    };
    const auto rejected = fsim::elaboration::elaborate(
        malformed.design, "callable_fork");
    assert(!rejected.ok());
    assert(has_diagnostic(rejected, "FSIM-ELAB-107"));

    const auto invalid_strobe = fsim::frontend::parse_text(
        "invalid_strobe.sv",
        R"(
module invalid_strobe(input logic lhs, rhs);
  initial $strobe("%b", lhs & rhs);
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(invalid_strobe.ok());
    const auto rejected_strobe = fsim::elaboration::elaborate(
        invalid_strobe.design, "invalid_strobe");
    assert(!rejected_strobe.ok());
    assert(has_diagnostic(rejected_strobe, "FSIM-ELAB-108"));
}

} // namespace fsim::tests::elaboration
