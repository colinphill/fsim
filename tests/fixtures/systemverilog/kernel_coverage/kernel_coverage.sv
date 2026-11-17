// SPDX-License-Identifier: Apache-2.0
// Code coverage counted by the v4 kernel's own instructions: statement and
// branch hits of a module instantiated three times (one native template,
// each instance its own counters), and toggles of 1, 8, 48 and 100 bit
// values, some starting unknown (a transition from or to x counts in
// neither direction).
module kc_leaf(input logic clk, input logic [7:0] d, output logic [7:0] q);
  logic [7:0] r;
  always @(posedge clk) begin
    if (d[0]) r <= d + 8'd1;
    else r <= d ^ 8'h5a;
    q <= r;
  end
endmodule

module kernel_coverage;
  logic clk = 0;
  logic [7:0] d = 0;
  logic [7:0] q0, q1, q2;
  logic [47:0] mid;
  logic [99:0] wide;
  kc_leaf u0(.clk, .d, .q(q0));
  kc_leaf u1(.clk, .d(d + 8'd3), .q(q1));
  kc_leaf u2(.clk, .d(~d), .q(q2));
  always #5 clk = ~clk;
  always @(posedge clk) begin
    d <= d * 8'd13 + 8'd7;
    mid <= {mid[46:0], d[3]};
    wide <= {wide[98:0], d[5]};
  end
  initial begin
    #2000;
    $display("q %h %h %h %h %h", q0, q1, q2, mid, wide);
    $finish;
  end
endmodule
