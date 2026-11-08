// SPDX-License-Identifier: Apache-2.0
// Concurrent assertions in generate blocks are assertion objects (IEEE
// 1800-2023 27.3, 37.49): ga sits in a conditional generate block, gl in the
// two blocks of a loop generate, and gc is a cover of the block. Each is
// found by its hierarchical name and has the type of its directive, its full
// name and its clocking event. cs is a `cover sequence` written inline: its
// clocking event and `disable iff` condition belong to the directive (16.14.3,
// 37.50), no property declaration holds them.
module top;
  logic clk = 0, a = 0, b = 0;
  always #5 clk = ~clk;

  if (1) begin : g
    ga: assert property (@(posedge clk) a |-> b);
    gc: cover property (@(posedge clk) a && b);
  end

  for (genvar i = 0; i < 2; i++) begin : l
    gl: assert property (@(posedge clk) a |-> b);
  end

  cs: cover sequence (@(negedge clk) disable iff (b) a ##1 b);

  initial #20 $finish;
endmodule
