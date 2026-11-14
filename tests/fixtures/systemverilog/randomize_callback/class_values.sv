// SPDX-License-Identifier: Apache-2.0
// Values in post_randomize, which randomize() calls, are the
// LRM's: real and shortreal literals, operations
// and comparisons, conversions on assignment and initialization, rounding
// away from zero (IEEE 1800-2023 6.12, 11.3.1), the conversion and math
// functions (20.5, 20.8.2); $bits of a string and of an unpacked array
// (20.6.2); streaming concatenations as sources and as targets (11.4.14);
// member and element selects of packed structures and arrays (7.2.1,
// 7.4.1).
class item;
  rand bit [1:0] x;
  int e;
  int k;
  real r;
  shortreal sr;
  real rr;
  logic [63:0] w;
  logic [15:0] nb;
  logic [3:0] b = 4'b0011;
  logic [3:0] lo, hi;
  string s;
  int arr[4];
  struct packed { logic [3:0] a; logic [3:0] b; } ps = 8'h5A;
  logic [1:0][7:0] m = 16'hC3F0;
  function void post_randomize();
    r = 1.5;
    r = r * 2.0 + 1;
    e = r;
    sr = r / 4;
    $display("real %f %0d %f %g %e", r, e, sr, r - 0.25, -r);
    e = 2.5;
    k = -2.5;
    $display("round %0d %0d %0d", e, k, $rtoi(-2.7));
    r = $itor(3) / 2;
    w = $realtobits(1.0);
    rr = $bitstoreal(64'h4004000000000000);
    $display("conv %f %h %f %h", r, w, rr, $shortrealtobits(sr));
    $display("compare %b %b %b %b", r > 1, r == 1.5, sr < r, r != rr);
    begin
      real lr = 3;
      int li = 7.6;
      $display("local %f %0d %f", lr, li, lr * li);
    end
    $display("math %f %f %f %0d", $sqrt(16.0), $pow(2.0, 10), 2.0 ** 3, $floor(2.7) == 2.0);
    $display("bits %0d %0d", $bits(s), $bits(arr));
    s = "abcd";
    $display("bits string %0d", $bits(s));
    w = {>>{b}};
    $display("stream %h", w);
    w = {<<{b}};
    $display("stream %h", w);
    nb = {<<4{16'h1234}};
    $display("stream %h", nb);
    nb = {<<3{8'b10110011}};
    $display("stream %b", nb);
    {>>{lo, hi}} = 8'hA5;
    $display("unpack %h %h", lo, hi);
    {<<4{lo, hi}} = 8'h3C;
    $display("unpack %h %h", lo, hi);
    $display("read %h %h %h %h %b", ps.a, ps.b[2:1], m[1], m[0][5:2], m[1][7]);
  endfunction
endclass

module top;
  item it = new;
  initial $display("result=%0d", it.randomize());
endmodule
