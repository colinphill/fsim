// fsim: top=upward_generated_scope_callable_top std=2017
// SPDX-License-Identifier: Apache-2.0
module upward_generated_scope_callable_child(output logic [3:0] value);
  initial begin
    value = '0;
    a.complete(value);
  end
endmodule

module upward_generated_scope_shadow_child(output logic [3:0] value);
  generate
    begin : a
      task automatic complete(output logic [3:0] result);
        result = 4'h3;
      endtask
    end
  endgenerate

  initial begin
    value = '0;
    a.complete(value);
  end
endmodule

module upward_generated_scope_parent #(parameter logic [3:0] TASK_VALUE = 4'h0) (
  output logic [3:0] parent_value,
  output logic [3:0] shadow_value);
  generate
    begin : a
      task automatic complete(output logic [3:0] result);
        result = TASK_VALUE;
      endtask
    end
  endgenerate

  upward_generated_scope_callable_child child(.value(parent_value));
  upward_generated_scope_shadow_child shadow(.value(shadow_value));
endmodule

module upward_generated_scope_callable_top;
  logic [3:0] left_parent_value;
  logic [3:0] right_parent_value;
  logic [3:0] left_shadow_value;
  logic [3:0] right_shadow_value;

  upward_generated_scope_parent #(.TASK_VALUE(4'hA)) left(
    .parent_value(left_parent_value),
    .shadow_value(left_shadow_value));
  upward_generated_scope_parent #(.TASK_VALUE(4'h5)) right(
    .parent_value(right_parent_value),
    .shadow_value(right_shadow_value));

  initial begin
    #1;
    if (left_parent_value !== 4'hA) begin
      $fatal(1, "left parent specialization supplied the wrong task value");
    end
    if (right_parent_value !== 4'h5) begin
      $fatal(1, "right parent specialization supplied the wrong task value");
    end
    if (left_shadow_value !== 4'h3 || right_shadow_value !== 4'h3) begin
      $fatal(1, "child-local generated scope did not shadow its parent");
    end
    $display("PASS");
    $finish;
  end
endmodule
