// SPDX-License-Identifier: Apache-2.0
// A select of a user function call's result (IEEE 1800-2023 37.59 details 9
// and 10): its vpiParent is the call, and under `simulate --foreign-probes`
// its value is the selected bits of what the call returned at its last
// evaluation (37.42, 38.15).
module top;
  logic [7:0] a = 8'hC5;
  logic [3:0] y;
  logic z;
  int k = 4;
  function automatic logic [7:0] twice(input logic [7:0] v);
    return v + v;
  endfunction
  initial begin : main
    y = twice(a)[3:0];
    z = twice(a)[2];
    y = twice(a)[7-:4];
    y = twice(a)[k+:3];
    #1;
  end
endmodule
