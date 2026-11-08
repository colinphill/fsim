// SPDX-License-Identifier: Apache-2.0
// Gate, switch, pull and UDP instances for the VPI primitive objects
// (IEEE 1800-2023 28, 29, 37.35, 37.36).
primitive udp_and2 (output y, input a, b);
  table
    0 ? : 0;
    ? 0 : 0;
    1 1 : 1;
  endtable
endprimitive

primitive udp_dff (output reg q, input d, clk);
  initial q = 1'b0;
  table
    0 r : ? : 0;
    1 r : ? : 1;
    ? f : ? : -;
    * ? : ? : -;
  endtable
endprimitive

module top;
  reg d1 = 1'b1;
  reg d2 = 1'b0;
  reg en = 1'b1;
  reg clk = 1'b0;
  wire [1:0] arr_a = 2'b11;
  wire [1:0] arr_b = 2'b01;
  wire w_and, w_unnamed, w_bufif, w_nmos, w_tran_a, w_tran_b, w_pull;
  wire w_gen, u_comb, u_seq;
  wire [1:0] w_arr;

  assign w_tran_a = d1;

  // A named and an unnamed gate.
  and g_and (w_and, d1, d2);
  and (w_unnamed, d1, d1);
  // A tristate gate with a min:typ:max delay.
  bufif1 #(1:2:3) g_buf (w_bufif, d1, en);
  // A MOS switch, a bidirectional switch and a pull.
  nmos g_nmos (w_nmos, d1, en);
  tranif0 g_tran (w_tran_a, w_tran_b, en);
  pullup g_pu (w_pull);
  // A gate array: element ga[1] is the leftmost.
  and ga[1:0] (w_arr, arr_a, arr_b);
  // A combinational and a sequential UDP instance.
  udp_and2 u_comb_i (u_comb, d1, d2);
  udp_dff u_seq_i (u_seq, d1, clk);
  // A gate with a constant terminal (the terminal's vpiExpr is a constant).
  wire w_const; and g_const (w_const, d1, 1'b1);

  generate
    if (1) begin : gen
      or g_or (w_gen, d1, d2);
    end
  endgenerate

  always #5 clk = ~clk;
  initial #40 $finish;
endmodule
