// fsim: top=top std=2017
// SPDX-License-Identifier: Apache-2.0
// Gate-instance arrays (IEEE 1800-2017 28.3.5, 28.3.6) whose bounds are
// parameter expressions, with vector terminals split across the instances,
// a scalar terminal shared by all, and concatenations giving each instance
// one operand; array query functions of a fixed-size net are constant
// (20.7).
module gates #(parameter W = 4) (input wire [W-1:0] a, input wire [W-1:0] e,
    input wire en, output wire [W-1:0] y, output wire [W-1:0] z,
    output wire [W-1:0] q);
  localparam int L = $left(y);
  bufif1 ba[W-1:0] (y, a, e);
  bufif0 bb[W-1:0] (y, ~a, e);
  and ga[L:0] (z, a, en);
  not gn[0:3] ({q[0], q[1], q[2], q[3]}, {a[3], a[2], a[1], a[0]});
endmodule

module top;
  reg [5:0] a, e;
  reg en;
  wire [5:0] y, z, q;
  gates #(.W(6)) g (.a(a), .e(e), .en(en), .y(y), .z(z), .q(q));
  bit ok = 1;
  initial begin
    a = 6'b101100; e = 6'b111000; en = 1;
    #1;
    if (y !== 6'b101011 || z !== 6'b101100 || q[3:0] !== 4'b1100) begin
      $display("FAIL %b %b %b", y, z, q);
      ok = 0;
    end
    en = 0; e = 6'b000111;
    #1;
    if (y !== 6'b010100 || z !== 6'b000000) begin
      $display("FAIL %b %b", y, z);
      ok = 0;
    end
    if (ok) $display("PASS");
  end
endmodule
