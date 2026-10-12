// fsim: top=sv_language_details_batch48
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2017 7.4: elements of nested unpacked, dynamic and associative
// arrays; 26.3: package arrays read with run-time indices and package
// structure members; 11.5.1: constant selects outside the declared range;
// 20.6, 20.7: $bits and $size of typedefs, strings and array elements, also
// as parameter overrides; 8.15: super; 6.19.5: chained enumeration methods;
// 12.7.1: a for-loop variable of an enumeration type; 13.4: a function
// output argument written in an if condition and a real task output
// converted to an integral actual; 13.4.1: a constant function reading its
// own name; 23.3.3.5: instance arrays connected to slices of wide actuals.
`timescale 1ns / 1ns
package batch48_pkg;
  int counts[3] = '{4, 5, 6};
  struct packed { logic [3:0] x; } flags = 4'b0101;
endpackage

module batch48_and(output out, input a, b);
  and gate(out, a, b);
endmodule

module batch48_width #(parameter int W = 1) (input logic [W-1:0] value);
endmodule

class batch48_base;
  int value = 1;
endclass

class batch48_derived extends batch48_base;
  byte value = 2;
  function int base_value();
    return super.value;
  endfunction
endclass

module sv_language_details_batch48;
  typedef enum logic [1:0] { RED, GREEN, BLUE } colour_t;
  typedef logic [4:0] five_t;
  localparam int DOUBLED = double_until(33);
  byte rows[][2];
  int table2[int][int];
  reg [7:0] narrow;
  bit [3:0] converted;
  bit written;
  string text;
  string names;
  wire [3:0] anded;
  reg [3:0] left_in, right_in;
  colour_t colour;
  five_t five;
  batch48_and gates [0:3] (anded, left_in, right_in);
  batch48_width #(.W($bits(colour_t))) colour_width (colour);
  batch48_width #(.W($bits(five_t))) five_width (five);

  function automatic int double_until(input int limit);
    double_until = 1;
    while (double_until < limit) double_until = double_until * 2;
  endfunction

  function automatic logic set_flag(output bit flag);
    flag = 1'b1;
    return 1'b1;
  endfunction

  task automatic copy_real(output real target, input real source);
    target = source;
  endtask

  initial begin
    batch48_derived derived;
    int index, failures;
    real minus_seven;
    derived = new;
    rows = new[2];
    rows[1] = '{8'd7, 8'd9};
    table2[3][4] = 12;
    index = 2;
    narrow = 8'h0f;
    text = "hello";
    left_in = 4'b1100;
    right_in = 4'b1010;
    colour = GREEN;
    minus_seven = -7.0;
    copy_real(converted, minus_seven);
    if (set_flag(written)) names = "";
    for (colour_t c = RED; c != BLUE; c = c.next) names = {names, c.name};
    #1;
    failures = 0;
    if (rows[1][0] != 7 || rows[1][1] != 9) failures++;
    if (table2[3][4] != 12 || table2[0][0] != 0) failures++;
    if (batch48_pkg::counts[index] != 6) failures++;
    if (batch48_pkg::flags.x[index] !== 1'b1) failures++;
    if (narrow[9] !== 1'bx || narrow[1:-2] !== 4'b11xx) failures++;
    if ($size(text) != 5 || $size(rows[0]) != 2) failures++;
    if (derived.base_value() != 1) failures++;
    if (colour.next(1).next(1) != RED) failures++;
    if (!written || names != "REDGREEN") failures++;
    if (DOUBLED != 64) failures++;
    if (converted != 4'h9) failures++;
    if (anded != 4'b1000) failures++;
    if ($bits(colour_width.value) != 2 || $bits(five_width.value) != 5)
      failures++;
    if (failures == 0) $display("PASS");
    else $display("FAIL %0d", failures);
  end
endmodule
