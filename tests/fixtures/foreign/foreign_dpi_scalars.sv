// SPDX-License-Identifier: Apache-2.0
// DPI scalar, string and packed vector arguments (IEEE 1800-2023 35.5.5,
// H.7.4, H.10.1): byte, shortint, int and longint, real and shortreal,
// chandle, string, bit and logic, and packed bit and logic vectors as
// svBitVecVal and svLogicVecVal, each as input, output and inout and as the
// result where the standard allows one. These are the arguments a compiled
// run marshals straight from the registers into the C argument buffer.
module top;
  import "DPI-C" function byte dpi_m_byte(
      input byte a, output byte o, inout byte b);
  import "DPI-C" function shortint dpi_m_short(
      input shortint a, output shortint o, inout shortint b);
  import "DPI-C" function int dpi_m_int(
      input int a, output int o, inout int b);
  import "DPI-C" function longint dpi_m_long(
      input longint a, output longint o, inout longint b);
  import "DPI-C" function int dpi_m_unsigned(
      input byte unsigned a, input shortint unsigned b,
      input int unsigned c, input longint unsigned d);
  import "DPI-C" function real dpi_m_real(
      input real a, output real o, inout real b);
  import "DPI-C" function shortreal dpi_m_shortreal(
      input shortreal a, output shortreal o, inout shortreal b);
  import "DPI-C" function chandle dpi_m_chandle(
      input chandle a, output chandle o);
  import "DPI-C" function string dpi_m_string(
      input string a, output string o);
  import "DPI-C" function bit dpi_m_bit(
      input bit a, output bit o, inout bit b);
  import "DPI-C" function logic dpi_m_logic(
      input logic a, output logic o, inout logic b);
  import "DPI-C" function int dpi_m_bits(
      input bit [7:0] a, input bit [32:0] b, input bit [99:0] c,
      output bit [99:0] o, inout bit [12:0] io);
  import "DPI-C" function int dpi_m_logics(
      input logic [7:0] a, input logic [64:0] b, output logic [64:0] o,
      inout logic [3:0] io);
  import "DPI-C" function void dpi_m_void(input int a);

  byte r8, a8 = -5, o8, b8 = 21;
  shortint r16, a16 = -300, o16, b16 = 1000;
  int r32, a32 = -70000, o32, b32 = 100000;
  longint r64, a64 = -64'sd5000000000, o64, b64 = 64'sd6000000000;
  byte unsigned ua = 200;
  shortint unsigned ub = 60000;
  int unsigned uc = 32'd4000000000;
  longint unsigned ud = 64'd18000000000000000000;
  int status;
  real rr, ra = 1.25, ro, rb = -2.5;
  shortreal fr, fa = 0.5, fo, fb = 3.25;
  chandle h0 = null, h1, o0, o1;
  string sr, so;
  bit br, ba = 1'b1, bo, bb = 1'b0;
  logic lr, la = 1'bx, lo, lb = 1'bz;
  bit [7:0] va = 8'hA5;
  bit [32:0] vb = 33'h1_2345_6789;
  bit [99:0] vc = 100'h9876543210FEDCBA987654321;
  bit [99:0] vo;
  bit [12:0] vio = 13'h1ABC;
  logic [7:0] xa = 8'b1x0z_1100;
  logic [64:0] xb = {1'b1, 32'h0, 32'h0000000F};
  logic [64:0] xo;
  logic [3:0] xio = 4'b1x0z;

  initial begin
    r8 = dpi_m_byte(a8, o8, b8);
    $display("#c byte ret=%0d o=%0d b=%0d", r8, o8, b8);
    r16 = dpi_m_short(a16, o16, b16);
    $display("#c short ret=%0d o=%0d b=%0d", r16, o16, b16);
    r32 = dpi_m_int(a32, o32, b32);
    $display("#c int ret=%0d o=%0d b=%0d", r32, o32, b32);
    r64 = dpi_m_long(a64, o64, b64);
    $display("#c long ret=%0d o=%0d b=%0d", r64, o64, b64);
    status = dpi_m_unsigned(ua, ub, uc, ud);
    $display("#c unsigned ret=%0d", status);
    rr = dpi_m_real(ra, ro, rb);
    $display("#c real ret=%0.3f o=%0.3f b=%0.3f", rr, ro, rb);
    fr = dpi_m_shortreal(fa, fo, fb);
    $display("#c shortreal ret=%0.3f o=%0.3f b=%0.3f", fr, fo, fb);
    h1 = dpi_m_chandle(h0, o0);
    $display("#c chandle ret=%0d o=%0d", h1 != null, o0 != null);
    h1 = dpi_m_chandle(h1, o1);
    $display("#c chandle ret=%0d o=%0d same=%0d", h1 != null, o1 != null,
        o1 == h1);
    sr = dpi_m_string("hello", so);
    $display("#c string ret=%s o=%s", sr, so);
    br = dpi_m_bit(ba, bo, bb);
    $display("#c bit ret=%b o=%b b=%b", br, bo, bb);
    lr = dpi_m_logic(la, lo, lb);
    $display("#c logic ret=%b o=%b b=%b", lr, lo, lb);
    status = dpi_m_bits(va, vb, vc, vo, vio);
    $display("#c bits ret=%0d o=%h io=%h", status, vo, vio);
    status = dpi_m_logics(xa, xb, xo, xio);
    $display("#c logics ret=%0d o=%h io=%b", status, xo, xio);
    // A second call of every site: the bound call has no state to leave.
    r8 = dpi_m_byte(a8, o8, b8);
    $display("#c byte again ret=%0d o=%0d b=%0d", r8, o8, b8);
    dpi_m_void(7);
  end
endmodule
