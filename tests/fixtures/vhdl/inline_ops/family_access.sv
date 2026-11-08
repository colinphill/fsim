// SPDX-License-Identifier: Apache-2.0
//
// A net array whose elements are driven by continuous assignments (a family
// of kernel-owned leaf nets, 128 bits wide in all) read as a whole by a
// combinational block. Reads and writes of the whole array are accesses of its
// elements (IEEE 1800-2023 6.5, 10.10), compiled like those of the
// elements, not run through the operation helper.
//   src = {1, 2, 3, 4}, so copy = {1, 2, 3, 4}            (first report)
//   src[2] = 9 at 1 ns, so copy = {1, 2, 9, 4}            (second report)
module top;
  reg [31:0] src [0:3];
  wire [31:0] n [0:3];
  reg [31:0] copy [0:3];
  genvar g;
  generate for (g = 0; g < 4; g = g + 1) begin : gen
    assign n[g] = src[g];
  end endgenerate
  always @* copy = n;
  initial begin
    src[0] = 1; src[1] = 2; src[2] = 3; src[3] = 4;
    #1;
    $display("inl fam %0d %0d %0d %0d", copy[0], copy[1], copy[2], copy[3]);
    src[2] = 9;
    #1;
    $display("inl fam %0d %0d %0d %0d", copy[0], copy[1], copy[2], copy[3]);
  end
endmodule
