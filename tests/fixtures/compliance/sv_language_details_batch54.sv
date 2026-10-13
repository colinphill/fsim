// fsim: top=sv_language_details_batch54
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2017 11.6.1: operands of a constant expression take the width of
// the whole expression before their operators apply; 6.20.2: a parameter
// without a type has the width of its value; 11.2.1, 20.8.2: real math
// functions in constant expressions; 11.4.2: constant division by zero is x;
// 9.4.2.2: `@*` with no readable signals never runs; 20.6.2: `$bits` of a
// type parameter and of another instance's typedef; 23.6: a member of a
// packed structure signal and a generate-block local parameter named
// hierarchically; 10.10: unpacked array concatenations assigned to a fixed
// array and to a slice; 10.3, 10.4.2: continuous and nonblocking slice
// copies; 7.5.1: `new[n](q)` with a queue initializer; 11.5.1: an output
// port connected to a partly out-of-range select.
`timescale 1ns / 1ns
typedef struct packed { logic [3:0] hi; logic [3:0] lo; } b54_pair_t;

interface b54_types #(parameter type T = logic) ();
  typedef logic [6:0] word_t;
  localparam int TW = $bits(T);
endinterface

module b54_holder;
  b54_pair_t pair = 8'h5a;
endmodule

module b54_pass(input [1:0] i, output [1:0] o);
  assign o = i;
endmodule

module sv_language_details_batch54;
  localparam int b54_wide = 3'd4 * 3'd4 * 2 + 3'd3;
  localparam b54_two = 2'b11;
  localparam b54_sum = b54_two + b54_two;
  localparam real b54_root = $sqrt(16.0);
  localparam real b54_power = $pow(2.0, 10.0);
  localparam [7:0] b54_quotient = 8'd7 / 8'd0;
  localparam real b54_infinite = 1.0 / 0.0;

  if (1) begin : b54_block
    localparam int LOCAL = b54_wide + 1;
  end

  b54_types #(.T(int)) types ();
  b54_holder holder ();

  logic [3:0] b54_count = 0;
  always @* b54_count = b54_count + 1;

  reg [1:0] drive = 2'b10;
  wire [7:0] spill;
  b54_pass clipped (.i(drive), .o(spill[8:7]));

  int fixed[0:3];
  int part[0:5];
  int moved[0:3];
  wire [7:0] lanes[0:2];
  logic [7:0] lane_value = 8'h11;
  assign lanes[1:2] = {lane_value, 8'h22};
  logic clk = 0;
  always @(posedge clk) moved[1:3] <= moved[0:2];

  initial begin
    int queue[$];
    int dynamic[];
    int failures;
    failures = 0;
    if (b54_wide != 35 || b54_sum != 2 || $bits(b54_sum) != 2) failures++;
    if (b54_root != 4.0 || b54_power != 1024.0) failures++;
    if (b54_quotient !== 8'hxx || b54_infinite <= 1.0e300) failures++;
    if (b54_block.LOCAL != 36) failures++;
    if (types.TW != 32 || $bits(types.word_t) != 7) failures++;
    if (holder.pair.hi != 4'h5 || $bits(holder.pair.lo) != 4) failures++;

    fixed = {1, 2, 3, 4};
    part = '{default: 0};
    part[1:4] = {fixed[2:3], 7, 8};
    if (fixed[3] != 4 || part[1] != 3 || part[4] != 8 || part[5] != 0)
      failures++;

    queue = '{5, 6};
    dynamic = new[2](queue);
    if (dynamic.size() != 2 || dynamic[1] != 6) failures++;

    moved = '{1, 2, 3, 4};
    #1 clk = 1;
    #1;
    if (moved[0] != 1 || moved[1] != 1 || moved[3] != 3) failures++;
    if (lanes[1] !== 8'h11 || lanes[2] !== 8'h22) failures++;
    if (spill[7] !== 1'b0 || b54_count !== 4'd0) failures++;
    if (failures == 0) $display("PASS");
    else $display("FAIL %0d", failures);
  end
endmodule
