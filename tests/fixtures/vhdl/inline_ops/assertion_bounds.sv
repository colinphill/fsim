// SPDX-License-Identifier: Apache-2.0
//
// Delay and repetition bounds that differ per instance (IEEE 1800-2023
// 16.7, 16.9.2): module chk takes the delay D and the repetition count R as
// parameters, so its assertion plans are one text and the bounds are
// per-instance constants, which the compiled kernel keeps in the attempt
// block (not in generated code). Sample n (the posedge at 10 n + 5) sees
// rows[n - 1] and 000 for n = 0 (16.5.1); a is bit 2 and b bit 1 of a row.
// The counts are derived from the table by an independent evaluation of the
// sequence semantics over the sampled values:
//
//   D=1 R=2  a |-> ##D b fails 8, a ##1 b[*R] succeeds 7
//   D=2 R=1  a |-> ##D b fails 7, a ##1 b[*R] succeeds 12
//   D=3 R=3  a |-> ##D b fails 7, a ##1 b[*R] succeeds 2
module chk #(parameter int D = 1, parameter int R = 2)(input bit clk, input logic a, b,
    output int fails, output int hits);
  initial begin fails = 0; hits = 0; end
  assert property (@(posedge clk) a |-> ##D b) else fails++;
  cover property (@(posedge clk) a ##1 b[*R]) hits++;
endmodule

module top;
  bit clk = 0;
  always #5 clk = ~clk;
  logic a = 0, b = 0, c = 0;
  int cyc = 0;
  int f0, h0, f1, h1, f2, h2;
  logic [2:0] rows [0:47] = '{3'b111, 3'b010, 3'b011, 3'b111, 3'b001, 3'b111, 3'b010, 3'b011, 3'b100, 3'b000, 3'b100, 3'b110, 3'b101, 3'b111, 3'b011, 3'b000, 3'b110, 3'b011, 3'b101, 3'b111, 3'b000, 3'b000, 3'b010, 3'b010, 3'b101, 3'b011, 3'b110, 3'b011, 3'b111, 3'b000, 3'b010, 3'b110, 3'b010, 3'b110, 3'b101, 3'b011, 3'b111, 3'b000, 3'b011, 3'b111, 3'b100, 3'b010, 3'b001, 3'b011, 3'b011, 3'b011, 3'b101, 3'b111};
  always @(negedge clk) begin
    {a, b, c} = rows[cyc];
    cyc <= cyc + 1;
  end
  chk #(.D(1), .R(2)) u0(clk, a, b, f0, h0);
  chk #(.D(2), .R(1)) u1(clk, a, b, f1, h1);
  chk #(.D(3), .R(3)) u2(clk, a, b, f2, h2);
  initial begin
    #480;
    $display("inl bounds f=%0d,%0d,%0d h=%0d,%0d,%0d", f0, f1, f2, h0, h1, h2);
    $finish;
  end
endmodule
