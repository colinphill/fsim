// SPDX-License-Identifier: Apache-2.0
//
// Delayed updates in the v4 kernel's generated code: intra-assignment
// delays of a whole variable, a constant bit, a run-time bit and a run-time
// part (IEEE 1800-2023 9.4.5, 10.4.2), a continuous assignment with rise and
// fall delays and a gate delay (28.16), and an inertial delay that swallows
// a pulse shorter than itself (10.3.3).
//   y rises 2 after a, falls 3 after it; g follows a 1 later; the 1-wide
//   pulse on p never reaches q (delay 2).
module top;
  timeunit 1ns; timeprecision 1ns;
  logic a, p;
  logic [7:0] v, w;
  int i;
  wire y, g, q;
  assign #(2, 3) y = a;
  and #1 (g, a, 1'b1);
  assign #2 q = p;
  initial begin
    a = 0; p = 0; v = 0; w = 0; i = 3;
    #10 a = 1;
    v <= #2 8'h5a;
    w[0] <= #1 1'b1;
    w[i] <= #3 1'b1;
    w[i + 1 +: 2] <= #4 2'b11;
    #1 $display("inl t=%0t y=%b g=%b v=%h w=%b", $time, y, g, v, w);
    #1 $display("inl t=%0t y=%b g=%b v=%h w=%b", $time, y, g, v, w);
    #2 $display("inl t=%0t y=%b g=%b v=%h w=%b", $time, y, g, v, w);
    a = 0;
    p = 1;
    #1 p = 0;
    #1 $display("inl t=%0t y=%b g=%b q=%b", $time, y, g, q);
    #3 $display("inl t=%0t y=%b g=%b q=%b", $time, y, g, q);
  end
endmodule
