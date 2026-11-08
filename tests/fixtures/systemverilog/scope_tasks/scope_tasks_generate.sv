// SPDX-License-Identifier: Apache-2.0
// A generate block is not a module instance, task, function or named block,
// so $scope refuses it (IEEE 1800-2023 D.11) and the run stops.
module leaf;
endmodule

module top;
  if (1) begin : g
    leaf gl ();
  end
  initial begin
    $scope(g);
    $display("not reached");
  end
endmodule
