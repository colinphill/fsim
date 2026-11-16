// fsim: top=sv_kernel_real_math std=2017
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 6.12, 11.3.1, 20.5, 20.8: real, shortreal, realtime and
// time arithmetic, comparisons and conversions, and the math and
// conversion functions, compiled into the kernel: rounding to nearest away
// from zero on conversion to an integer, truncation by $rtoi, wrap of a
// huge real to 64 bits, infinities and NaN from division and $sqrt/$ln,
// unknown bits of an integer read as zero by $itor, shortreal rounding,
// time comparisons with reals, in a combinational always block and
// across a suspension.
module sv_kernel_real_math;
  int failures;
  real a, b, c;
  shortreal s, u;
  time tm;
  realtime rt;
  int i;
  longint li;
  bit [63:0] bits;
  logic [7:0] lx;

  task automatic check(input string what, input bit ok);
    if (!ok) begin
      $display("FAIL %s", what);
      failures++;
    end
  endtask

  function automatic bit near(input real got, input real want);
    return got - want < 1e-9 && want - got < 1e-9;
  endfunction

  always @(a) c = a * 1.5 + b / 4.0;

  initial begin
    a = 2.25;
    b = -7.0;
    #1;
    check("always block", near(c, 1.625));
    check("add", a + b == -4.75);
    check("subtract", a - b == 9.25);
    check("multiply", a * b == -15.75);
    check("divide", near(a / b, -0.32142857142857145));
    check("divide by zero", a / 0.0 > 1e308);
    check("negative infinity", -a / 0.0 < -1e308);
    check("compare", !(a == b) && a != b && !(a < b) && !(a <= b) && a > b && a >= b);
    s = 1.1;
    u = 2.2;
    check("shortreal add", $shortrealtobits(s + u) == 32'h40533334);
    check("shortreal compare", s < u);
    i = a;
    check("to int", i == 2);
    i = -2.5;
    check("round away from zero, negative", i == -3);
    i = 2.5;
    check("round away from zero", i == 3);
    li = b * 1e12;
    check("to longint", li == -64'sd7000000000000);
    tm = a * 3;
    check("to time", tm == 7);
    tm = 10;
    rt = tm * 0.5;
    check("time times real", rt == 5.0);
    check("time compare", tm > 3 && tm + 5 == 15);
    check("rtoi", $rtoi(-3.99) == -3 && $rtoi(1e3) == 1000);
    check("itor", $itor(-5) == -5.0);
    bits = $realtobits(a);
    check("realtobits", bits == 64'h4002000000000000);
    check("bitstoreal", $bitstoreal(bits) == 2.25);
    check("shortrealtobits", $shortrealtobits(s) == 32'h3f8ccccd);
    check("bitstoshortreal", $bitstoshortreal(32'h3f800000) == 1.0);
    check("ln", near($ln(a), 0.8109302162163288));
    check("log10", near($log10(100.0), 2.0));
    check("exp", near($exp(1.0), 2.718281828459045));
    check("sqrt", $sqrt(a) == 1.5);
    check("pow", near($pow(2.0, 10.0), 1024.0));
    check("floor and ceil", $floor(-a) == -3.0 && $ceil(a) == 3.0);
    check("trig", near($sin(0.0), 0.0) && near($cos(0.0), 1.0) && near($atan2(1.0, 1.0), 0.7853981633974483));
    check("hypot", $hypot(3.0, 4.0) == 5.0);
    check("hyperbolic", near($tanh(0.0), 0.0) && near($asinh(0.0), 0.0) && near($cosh(0.0), 1.0));
    check("nan", $sqrt(-1.0) != $sqrt(-1.0));
    check("ln of zero", $ln(0.0) < -1e308);
    lx = 8'b1x01_0011;
    check("unknown bits read as zero", $itor(lx) == 147.0);
    li = 64'h7fff_ffff_ffff_ffff;
    check("int to real rounding", $itor(li) == 9223372036854775808.0);
    a = 1e30;
    li = a;
    check("wrap to 64 bits", li == 64'sd5076964154930102272);
    check("integer plus real", 7 + a == 1e30);
    #1;
    check("after a wait", a * b == -7e30);
    if (failures == 0) $display("PASS");
    $finish;
  end
endmodule
