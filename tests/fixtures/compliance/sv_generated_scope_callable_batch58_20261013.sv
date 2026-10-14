// fsim: top=generated_scope_callable_top std=2017
// SPDX-License-Identifier: Apache-2.0
module generated_scope_callable_leaf #(
  parameter bit USE_LEFT = 1'b0
)(
  output logic [3:0] function_value,
  output logic [3:0] branch_result_value,
  output logic [3:0] case_result_value,
  output logic [3:0] task_value,
  output logic [3:0] loop_task_value,
  output logic [3:0] mixed_then_value,
  output logic [3:0] mixed_else_value
);
  generate
    for (genvar lane = 0; lane < 1; lane = lane + 1) begin : loop_scope
      task automatic write_loop_value(output logic [3:0] value);
        value = 4'h9;
      endtask

      initial loop_scope.write_loop_value(loop_task_value);
    end

    // Different genvar iterations select different conditional arms. The
    // selected-generate set is unit-wide, so both arm IDs can be present.
    for (genvar mixed_lane = 0; mixed_lane < 2; mixed_lane = mixed_lane + 1) begin : mixed_loop
      if (mixed_lane == 0) begin : mixed_route
        task automatic write_then(output logic [3:0] value);
          value = 4'h6;
        endtask

        initial mixed_route.write_then(mixed_then_value);
      end else begin : mixed_route
        task automatic write_else(output logic [3:0] value);
          value = 4'h9;
        endtask

        initial mixed_route.write_else(mixed_else_value);
      end
    end

    if (USE_LEFT) begin : route
      function automatic logic [3:0] compute(input logic [3:0] value);
        return value + 4'h1;
      endfunction

      function automatic logic [3:0] branch_result(input logic unused);
        branch_result = 4'h3;
      endfunction

      task automatic write_value(output logic [3:0] value);
        value = 4'h5;
      endtask
    end else begin : route
      function automatic logic [3:0] compute(input logic [3:0] value);
        return value + 4'h2;
      endfunction

      function automatic logic [3:0] branch_result(input logic unused);
        branch_result = 4'h4;
      endfunction

      task automatic write_value(output logic [3:0] value);
        value = 4'hc;
      endtask
    end
  endgenerate

  generate
    case (USE_LEFT)
      1'b1: begin : case_route
        function automatic logic [3:0] case_result(input logic unused);
          case_result = 4'h6;
        endfunction
      end
      default: begin : case_route
        function automatic logic [3:0] case_result(input logic unused);
          case_result = 4'h7;
        endfunction
      end
    endcase
  endgenerate

  initial begin
    function_value = route.compute(4'ha);
    branch_result_value = route.branch_result(1'b0);
    case_result_value = case_route.case_result(1'b0);
    route.write_value(task_value);
  end
endmodule

module generated_scope_callable_top;
  logic [3:0] left_function;
  logic [3:0] left_branch_result;
  logic [3:0] left_case_result;
  logic [3:0] left_task;
  logic [3:0] left_loop_task;
  logic [3:0] left_mixed_then;
  logic [3:0] left_mixed_else;
  logic [3:0] right_function;
  logic [3:0] right_branch_result;
  logic [3:0] right_case_result;
  logic [3:0] right_task;
  logic [3:0] right_loop_task;
  logic [3:0] right_mixed_then;
  logic [3:0] right_mixed_else;

  generated_scope_callable_leaf #(.USE_LEFT(1'b1)) left (
    .function_value(left_function),
    .branch_result_value(left_branch_result),
    .case_result_value(left_case_result),
    .task_value(left_task),
    .loop_task_value(left_loop_task),
    .mixed_then_value(left_mixed_then),
    .mixed_else_value(left_mixed_else)
  );
  generated_scope_callable_leaf #(.USE_LEFT(1'b0)) right (
    .function_value(right_function),
    .branch_result_value(right_branch_result),
    .case_result_value(right_case_result),
    .task_value(right_task),
    .loop_task_value(right_loop_task),
    .mixed_then_value(right_mixed_then),
    .mixed_else_value(right_mixed_else)
  );

  initial begin
    #1;
    if (left_function !== 4'hb || left_branch_result !== 4'h3
        || left_case_result !== 4'h6 || left_task !== 4'h5
        || right_function !== 4'hc || right_branch_result !== 4'h4
        || right_case_result !== 4'h7 || right_task !== 4'hc
        || left_loop_task !== 4'h9 || right_loop_task !== 4'h9
        || left_mixed_then !== 4'h6 || right_mixed_then !== 4'h6
        || left_mixed_else !== 4'h9 || right_mixed_else !== 4'h9) begin
      $fatal(1, "generated callable selected the wrong branch");
    end
    $display("PASS");
    $finish;
  end
endmodule
