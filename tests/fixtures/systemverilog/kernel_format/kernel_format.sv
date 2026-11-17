// SPDX-License-Identifier: Apache-2.0
// String formatting in the v4 kernel's own instructions (KOp::str format):
// $sformatf, $sformat and $swrite arguments of every format specification
// (IEEE 1800-2023 21.2.1, 21.3.3) on narrow and wide, two- and four-state,
// signed and unsigned values, strings, times and reals, with field widths
// and flags, in several instances of one module; and $realtime (20.3.3).
`timescale 1ns/10ps
module kernel_format_cell #(parameter int SEED = 1) (input bit clk);
  logic [7:0] d;
  logic signed [15:0] s;
  logic [99:0] w;
  bit [63:0] two;
  string text;
  string line;
  string more;
  real r;
  shortreal h;
  realtime stamp;
  int count;
  always @(posedge clk) begin
    count = count + 1;
    d = 8'(count * SEED);
    s = -16'(count * SEED);
    w = {4'b1x0z, 32'(count), 64'h0123_4567_89ab_cdef};
    two = 64'(count) << 40;
    r = count * 1.25;
    h = count * 0.5;
    stamp = $realtime;
    text = $sformatf("%0d:%d:%h:%x:%b:%o:%c", d, d, d, d, d[3:0], d, 8'h41 + d[3:0]);
    $sformat(line, "[%5s|%-6d|%06d|%0d|%h|%t|%0t]", text.substr(0, 2), s, s, s, s, $time, $realtime);
    $swrite(more, "%h %0d %b %x", w, two, w[99:90], two);
    more = {more, $sformatf(" %e %f %g %0.3f %10.2f %s", r, r, r, h, r, text)};
    $display("%m %s | %s | %s | %0.4f", text, line, more, stamp);
  end
endmodule

module kernel_format;
  bit clk;
  kernel_format_cell #(.SEED(1)) first(clk);
  kernel_format_cell #(.SEED(3)) second(clk);
  kernel_format_cell #(.SEED(7)) third(clk);
  initial begin
    repeat (12) #1.37 clk = ~clk;
    $finish;
  end
endmodule
