// fsim: top=top std=2017
// SPDX-License-Identifier: Apache-2.0
// $sformat and $sformatf with a format known only at run time (IEEE
// 1800-2017 21.3.3), $value$plusargs with a run-time user string and its
// found result (21.6), and an unpacking stream whose dynamic array operand
// has a `with` range (11.4.14.4).
module top;
  string fmt, res, s, query;
  logic signed [7:0] v;
  bit [31:0] w;
  real r;
  int found, value;
  byte q[], out_data[];
  byte packet[$];
  int header, length, crc, out_header, out_length, out_crc;
  bit ok = 1;

  task check(input string got, input string want);
    if (got != want) begin
      $display("FAIL got '%s' want '%s'", got, want);
      ok = 0;
    end
  endtask

  initial begin
    fmt = "%0d"; v = -5;
    $sformat(res, fmt, v);
    check(res, "-5");
    fmt = "%h|%s|%5.2f";
    w = 32'hdead; s = "hey"; r = 2.5;
    check($sformatf(fmt, w, s, r), "0000dead|hey| 2.50");
    check($sformatf({"x=", "%b"}, 3'b101), "x=101");
    fmt = "%d";
    check($sformatf(fmt, 4'd7), " 7");
    fmt = "%e";
    check($sformatf(fmt, 3), "3.000000e+00");

    value = 9;
    query = "NOT_GIVEN_ANYWHERE=%d";
    found = $value$plusargs(query, value);
    if (found != 0 || value != 9) begin
      $display("FAIL plusargs %0d %0d", found, value);
      ok = 0;
    end

    header = 12; length = 3; crc = 42;
    q = new[3];
    q[0] = 8'h11; q[1] = 8'h22; q[2] = 8'h33;
    packet = {<< 8 {header, length, q, crc}};
    {<< 8 {out_header, out_length, out_data with [0 +: out_length],
        out_crc}} = packet;
    if (out_header != 12 || out_length != 3 || out_data.size() != 3
        || out_data[0] != 8'h11 || out_data[2] != 8'h33 || out_crc != 42)
    begin
      $display("FAIL stream %0d %0d %0d %0d", out_header, out_length,
        out_data.size(), out_crc);
      ok = 0;
    end

    if (ok) $display("PASS");
  end
endmodule
