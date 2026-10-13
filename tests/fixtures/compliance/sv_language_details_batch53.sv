// fsim: top=sv_language_details_batch53
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2017 6.20.2: unpacked-array parameters, overridden per instance
// by an array actual; 10.9.2: unpacked arrays of structures initialized by
// positional, member-keyed and default patterns; 8.25: a constant of a
// specialized parameterized class (`C#(7)::v`); 8.4: a member of a
// structure-typed class property read and written through the handle;
// 10.4.2: a nonblocking assignment to a concatenation of variables; 6.16.8:
// string `compare` as a signed result; 11.4.11: a conditional operator with
// an unknown condition merging z operands to x.
`timescale 1ns / 1ns
typedef struct { int a; int b; } b53_pair_t;
typedef struct packed { logic [3:0] hi; logic [3:0] lo; } b53_nibbles_t;

class b53_scaled #(int N = 1);
  localparam int v = N * 3;
endclass

class b53_holder;
  b53_nibbles_t cfg;
endclass

module b53_table #(parameter int weights[3] = '{1, 2, 3});
  localparam int total = weights[0] + weights[1] + weights[2];
  int sum;
  initial sum = weights[0] + weights[1] + weights[2];
endmodule

module sv_language_details_batch53;
  localparam b53_pair_t pairs[3] = '{'{1, 2}, '{a: 3, b: 4}, '{default: 5}};
  localparam int scaled = b53_scaled#(7)::v;

  b53_table defaults ();
  b53_table #(.weights('{10, 20, 30})) overridden ();

  logic [3:0] high_part, low_part;
  logic clk = 1'b0;
  always @(posedge clk) {high_part, low_part} <= 8'ha5;

  initial begin
    b53_holder holder;
    string left_word, right_word;
    logic select_bit;
    logic [1:0] merged;
    int failures;
    failures = 0;
    if (pairs[0].a != 1 || pairs[1].b != 4 || pairs[2].a != 5) failures++;
    if (scaled != 21) failures++;

    holder = new;
    holder.cfg = 8'h00;
    holder.cfg.hi = 4'h9;
    holder.cfg.lo = 4'h3;
    if (holder.cfg !== 8'h93 || holder.cfg.hi !== 4'h9) failures++;

    left_word = "abc";
    right_word = "abd";
    if (left_word.compare(right_word) >= 0) failures++;
    if (right_word.compare(left_word) <= 0) failures++;

    select_bit = 1'bx;
    merged = select_bit ? 2'bzz : 2'bzz;
    if (merged !== 2'bxx) failures++;

    #1 clk = 1'b1;
    #1;
    if (high_part !== 4'ha || low_part !== 4'h5) failures++;
    if (defaults.sum != 6 || overridden.sum != 60) failures++;
    if (defaults.total != 6 || overridden.total != 60) failures++;
    if (failures == 0) $display("PASS");
    else $display("FAIL %0d", failures);
  end
endmodule
