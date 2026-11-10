// SPDX-License-Identifier: Apache-2.0
//
// SystemVerilog threads whose effects FSIM_KERNEL_VERIFY's reference replay
// must make as the compiled run made them, so that both are compared on their
// full state (run with +replay_count=7 +replay_name=fsim):
//   frame: an automatic function's unpacked array belongs to its call (IEEE
//      1800-2023 6.21; the frame saves the caller's words and restores them
//      on return): selected = 1 gives {10, 30} = 40; nested_sum adds 1 + 2 +
//      40 = 43; selected = 0 gives {30, 20} = 50.
//   cmos: a cmos switch is an nmos and a pmos on one output (28.9), which the
//      kernel mirrors from the host: n = 1, p = 1 pass in = 0; n = 0, p = 1
//      leave it z; p = 0 passes in = 1.
//   sscanf, fscanf, plusargs: $sscanf and $fscanf (21.3.4.3) and
//      $value$plusargs (21.6) write module variables: 'h2a, 10.0 and -17
//      (3 conversions); 42 read back from the file; 7 and "fsim".
//   nba: nonblocking writes of unpacked-array elements and of part-selects of
//      one element (10.4.2) land at the end of the time step, with the
//      indices of their scheduling; the write through an unknown index is
//      ignored (7.4.5): before 12 and 34, after de and 97. The memory is held
//      as one kernel slot: the run's first nonblocking write is queued as an
//      element write, the later ones as writes of the slot's bits, and the
//      reference replay's element writes must compare as the same writes.
//   cells: one activation of nonblocking writes of every element form into
//      the slot of a memory (10.4.2, in order of scheduling, at = 2): a whole
//      element with x and z bits, cells[2] = xz; a constant part-select,
//      cells[3][7:4] = a: a1; cells[1][2 +: 3] = 101: 33 -> 37;
//      cells[0][6 -: 2] = 01: 44 -> 24; cells[0][6 +: 4] = 0110 writes only
//      its in-range bits 7:6 = 10 (11.5.1): 24 -> a4; a bit-select after the
//      whole element, cells[2][0] = 1: xZ (21.2.1.3). An unknown index
//      (7.4.5), an unknown part-select base and an index past the bounds
//      (11.5.1, 7.4.5) write nothing. The two-state memory states: states[1]
//      = 5a, states[0][2 -: 2] = 11: 06.
`timescale 1ns/1ns
module top;
  int selected = 0;

  function automatic int local_sum();
    int values [0:1];
    values[0] = 10;
    values[1] = 20;
    values[selected] = 30;
    return values[0] + values[1];
  endfunction

  function automatic int nested_sum();
    int cells [0:2];
    cells[0] = 1;
    cells[1] = 2;
    cells[2] = local_sum();
    return cells[0] + cells[1] + cells[2];
  endfunction

  initial begin
    selected = 1;
    $display("frame local=%0d", local_sum());
    $display("frame nested=%0d", nested_sum());
    selected = 0;
    $display("frame again=%0d", local_sum());
  end

  wire cmos_out;
  reg cmos_in = 1'b0, cmos_n = 1'b1, cmos_p = 1'b1;
  cmos (cmos_out, cmos_in, cmos_n, cmos_p);

  initial begin
    #1 $display("cmos on=%b", cmos_out);
    cmos_n = 0;
    #1 $display("cmos off=%b", cmos_out);
    cmos_p = 0;
    cmos_in = 1;
    #1 $display("cmos pmos=%b", cmos_out);
  end

  logic [31:0] scanned;
  real scanned_real;
  int scanned_int;
  int scan_count;
  int file_value;
  int plus_count;
  string plus_name;
  int plus_found;
  integer fd;

  initial begin
    #4;
    scan_count = $sscanf("2a 1e1 -17", "%h %f %d", scanned, scanned_real,
      scanned_int);
    $display("sscanf count=%0d packed=%h real=%0.1f int=%0d", scan_count,
      scanned, scanned_real, scanned_int);
    fd = $fopen("replay_effects.txt", "w");
    $fdisplay(fd, "%0d", 42);
    $fclose(fd);
    fd = $fopen("replay_effects.txt", "r");
    scan_count = $fscanf(fd, "%d", file_value);
    $fclose(fd);
    $display("fscanf count=%0d value=%0d", scan_count, file_value);
    plus_found = $value$plusargs("replay_count=%d", plus_count);
    $display("plusargs found=%0d count=%0d", plus_found, plus_count);
    plus_found = $value$plusargs("replay_name=%s", plus_name);
    $display("plusargs found=%0d name=%s", plus_found, plus_name);
  end

  logic [7:0] matrix [1:0][-1:0];
  logic [1:0] unknown_index = 2'bx1;
  integer row;
  integer column;
  integer part;

  initial begin
    #6;
    matrix[1][-1] = 8'h12;
    matrix[0][0] = 8'h34;
    row = 1;
    column = -1;
    part = 4;
    #1;
    matrix[row][column] <= 8'hde;
    matrix[unknown_index][column] <= 8'hff;
    matrix[0][0][part +: 4] <= 4'h9;
    matrix[0][0][0 +: 4] <= 4'h7;
    row = 0;
    $display("nba before=%h sibling=%h", matrix[1][-1], matrix[0][0]);
    #1;
    $display("nba after=%h sibling=%h", matrix[1][-1], matrix[0][0]);
  end

  logic [7:0] cells [3:0];
  bit [7:0] states [0:1];
  logic [3:0] unknown_base = 4'b1x00;
  integer at;

  initial begin
    #6;
    cells[3] = 8'h11;
    cells[2] = 8'h22;
    cells[1] = 8'h33;
    cells[0] = 8'h44;
    states[0] = 8'h00;
    states[1] = 8'h00;
    at = 2;
    #2;
    cells[at] <= 8'hxz;
    cells[3][7:4] <= 4'ha;
    cells[at - 1][at +: 3] <= 3'b101;
    cells[0][3 * at -: 2] <= 2'b01;
    cells[0][3 * at +: 4] <= 4'b0110;
    cells[unknown_index] <= 8'h00;
    cells[1][unknown_base +: 2] <= 2'b00;
    cells[at + 2] <= 8'hff;
    cells[at][0] <= 1'b1;
    states[at - 1] <= 8'h5a;
    states[0][at -: 2] <= 2'b11;
    at = 0;
    #1;
    $display("cells %h %h %h %h states %h %h", cells[3], cells[2], cells[1],
      cells[0], states[0], states[1]);
  end
endmodule
