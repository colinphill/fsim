// SPDX-License-Identifier: Apache-2.0
//
// Whole unpacked arrays of a module (IEEE 1800-2023 7.6) written as a whole
// by the v4 kernel's generated code: nonblocking writes (10.4.2) that read
// their sources now and update in the NBA region (a swap of a and b, and
// 40-bit elements across words), a continuous assignment of an array
// (10.3; c follows a, its element c[1] waking a process each change), and
// a blocking whole-array write.
module top;
  logic [3:0] a [3];
  logic [3:0] b [3];
  logic [39:0] wa [2];
  logic [39:0] wb [2];
  logic [3:0] c [3];
  assign c = a;
  initial begin
    a[0] = 1; a[1] = 2; a[2] = 3;
    b[0] = 7; b[1] = 8; b[2] = 9;
    wa[0] = 40'h12_3456_789a; wa[1] = 40'hff_0000_0001;
    wb[0] = 0; wb[1] = 1;
    a <= b; b <= a;
    wb <= wa;
    $display("inl now %0d %0d %0d %0d", a[0], b[0], c[0], wb[0]);
    #0 $display("inl delta %0d %0d", a[0], b[0]);
    #1 $display("inl after %0d %0d %0d / %0d %0d %0d c=%0d %0d %0d", a[0], a[1], a[2], b[0], b[1], b[2], c[0], c[1], c[2]);
    $display("inl wide %h %h", wb[0], wb[1]);
    a = b;
    #1 $display("inl blocking %0d %0d %0d c=%0d", a[0], a[1], a[2], c[2]);
  end
  always @(c[1]) $display("inl c1 %0d at %0t", c[1], $time);
endmodule
