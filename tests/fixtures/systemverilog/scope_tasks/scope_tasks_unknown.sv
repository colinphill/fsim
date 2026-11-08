// SPDX-License-Identifier: Apache-2.0
// A $scope name that matches nothing in the design is a run-time error (IEEE
// 1800-2023 D.11).
module child;
endmodule

module top;
  child u ();
  initial begin
    $scope(u.missing);
    $display("not reached");
  end
endmodule
