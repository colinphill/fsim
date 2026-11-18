// SPDX-License-Identifier: Apache-2.0
//
// Two-dimensional fixed-size arrays of an automatic task, held by the v4
// kernel as its own elements: element writes and reads and a whole-array
// assignment (IEEE 1800-2023 7.4, 7.6) run in compiled member code, the
// copy as generated code.
//   grid[r][c] = 16*r + c: grid[1][2] = 18, grid[0][1] = 1;
//   grid[1][3] is out of range: logic reads x (7.4.6);
//   copy = grid copies every element: copy[1][0] = 16; a later write of
//   grid[1][0] leaves copy unchanged.
module top;
  task automatic run();
    logic [7:0] grid[1:0][0:2];
    logic [7:0] copy[1:0][0:2];
    for (int r = 0; r < 2; r++)
      for (int c = 0; c < 3; c++) grid[r][c] = 8'(r * 16 + c);
    $display("inl grid %0d %0d %b", grid[1][2], grid[0][1], grid[1][3]);
    copy = grid;
    grid[1][0] = 8'd99;
    $display("inl copy %0d %0d %0d", copy[1][0], copy[0][2], grid[1][0]);
  endtask
  initial run();
endmodule
