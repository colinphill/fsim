// SPDX-License-Identifier: Apache-2.0
// $scope names a module instance, task, function or named block (IEEE
// 1800-2023 D.11): the variable a is none of them, so the run fails.
module top;
  reg [7:0] a;
  initial begin
    a = 8'd1;
    $scope(a);
    $display("not reached");
  end
endmodule
