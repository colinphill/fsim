// SPDX-License-Identifier: Apache-2.0
// Startup restrictions, callback and iterator handle lifetimes, multiple
// indexes and scoped names (IEEE 1800-2023 36.10.2, 38.8, 38.20, 38.21, 38.23,
// 38.38).
module child;
  reg w;
endmodule

module top;
  reg [7:0] mem [0:1][0:2];
  reg [3:0][7:0] pk;
  reg [7:0] v = 8'h00;
  child c ();
  initial begin
    #6;
  end
endmodule
