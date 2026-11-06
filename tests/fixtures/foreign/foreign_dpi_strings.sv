// SPDX-License-Identifier: Apache-2.0
// DPI arrays of strings (IEEE 1800-2023 35.5.6, 35.5.6.1, H.7.4, H.12): a
// sized input and output array, open arrays of one and of two dimensions
// with ascending and descending bounds, and open and sized inout and
// output arrays whose elements the C code replaces, and a dynamic array and
// a queue of strings as the actuals of open-array formals, in input, inout
// and output.
module top;
  import "DPI-C" function int dpi_s_sized(
      input string s [3], output string o [2]);
  import "DPI-C" function int dpi_s_open(
      input string d [], input string m [][]);
  import "DPI-C" function void dpi_s_swap(
      inout string x [], inout string y [2]);
  import "DPI-C" function void dpi_s_fill(output string r []);
  import "DPI-C" function int dpi_s_dyn(
      input string q [], inout string io [], output string o []);

  string a [3];
  string o [2];
  string d [4:2];
  string m [1:2][0:1];
  string x [0:1];
  string y [2];
  string r [2:3];
  string dq [];
  string dio [$];
  string dout [];
  int status;

  initial begin
    a[0] = "alpha";
    a[1] = "beta";
    a[2] = "gamma";
    status = dpi_s_sized(a, o);
    $display("#c sized ret=%0d o=%s,%s", status, o[0], o[1]);
    d[4] = "four";
    d[3] = "three";
    d[2] = "two";
    m[1][0] = "a";
    m[1][1] = "b";
    m[2][0] = "c";
    m[2][1] = "d";
    status = dpi_s_open(d, m);
    $display("#c open ret=%0d", status);
    x[0] = "x0";
    x[1] = "x1";
    y[0] = "y0";
    y[1] = "y1";
    dpi_s_swap(x, y);
    $display("#c swap x=%s,%s y=%s,%s", x[0], x[1], y[0], y[1]);
    r[2] = "stale";
    dpi_s_fill(r);
    $display("#c fill r=%s,%s", r[2], r[3]);
    dq = new[3];
    dq[0] = "p";
    dq[1] = "q";
    dq[2] = "r";
    dio = { "i0", "i1" };
    dout = new[2];
    status = dpi_s_dyn(dq, dio, dout);
    $display("#c dyn ret=%0d io=%s,%s o=%s,%s", status, dio[0], dio[1],
        dout[0], dout[1]);
  end
endmodule
