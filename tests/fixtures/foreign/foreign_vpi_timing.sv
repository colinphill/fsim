// SPDX-License-Identifier: Apache-2.0
// A module with every kind of module path and four timing checks with
// notifiers (IEEE 1800-2023 37.39, 37.40); the stimulus violates $setup and
// $width once each.
`timescale 1ns / 1ns
module dut (
    input  wire clk,
    input  wire d,
    input  wire a,
    input  wire b,
    input  wire en,
    output wire q_full,
    output wire q_par,
    output wire q_none,
    output wire q_edge
);
  reg ntf_setup = 1'b0;
  reg ntf_hold = 1'b0;
  reg ntf_sh = 1'b0;
  reg ntf_width = 1'b0;
  assign q_full = a & b;
  assign q_par = a;
  assign q_none = a ^ b;
  assign q_edge = d;
  specify
    (a, b *> q_full) = 3;
    (a => q_par) = 2;
    if (en) (a => q_none) = 4;
    ifnone (a => q_none) = 5;
    (posedge clk => (q_edge +: d)) = 6;
    $setup(d, posedge clk, 4, ntf_setup);
    $hold(posedge clk, d, 3, ntf_hold);
    $setuphold(posedge clk, d, 2, 1, ntf_sh);
    $width(posedge clk, 5, 0, ntf_width);
    (en => q_par) = (1:2:3);  // delay object: a min:typ:max triple
    (b => q_par) = (2, 3);  // a rise/fall pair: a vpiListOp
    $hold(posedge clk &&& en, d &&& a, 2);  // &&& conditions of both terms
  endspecify
endmodule

module top;
  reg clk, d, a, b, en;
  wire q_full, q_par, q_none, q_edge;
  dut u (
      .clk(clk),
      .d(d),
      .a(a),
      .b(b),
      .en(en),
      .q_full(q_full),
      .q_par(q_par),
      .q_none(q_none),
      .q_edge(q_edge)
  );
  initial begin
    clk = 0;
    d = 0;
    a = 0;
    b = 0;
    en = 1;
    #10 clk = 1;
    #10 clk = 0;
    #10 d = 1;
    // d changes 3 ns before the edge: $setup (4) is violated, the setup
    // limit of $setuphold (2) is not.
    #3 clk = 1;
    // $width (posedge clk) measures the high pulse, from posedge to negedge
    // (IEEE 1800-2023 31.4.4): this 3 ns pulse violates it at 36, and the
    // following 2 ns low pulse is not checked.
    #3 clk = 0;
    #2 clk = 1;
    #22 clk = 0;
    #10 d = 0;
    #10 $finish;
  end
endmodule
