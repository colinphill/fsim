// fsim: top=sv_language_details_batch44
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2017 10.3.3 and 28.16: rise and fall delays evaluated at run
// time; 10.3: a continuous assignment of a whole unpacked array; 10.4.2: a
// nonblocking assignment of a whole unpacked array; 9.6.2: disabling a named
// block or task that another process executes; 10.9.2: a structure
// parameter given as a keyed assignment pattern; 10.10: an unpacked array
// declaration initialized by a concatenation.
`timescale 1ns / 1ns
package batch44_pkg;
  typedef struct packed {
    int unsigned width;
    int unsigned depth;
  } cfg_t;
endpackage

module sv_language_details_batch44;
  localparam batch44_pkg::cfg_t CFG = '{width: 6, depth: 3};
  localparam batch44_pkg::cfg_t DEF = '{default: 2};

  logic [CFG.width-1:0] sized;
  wire [7:0] rise_delay = 2;
  reg in;
  wire [1:0] out;
  assign #(rise_delay, 1) out[0] = in;

  logic [3:0] source[2];
  logic [3:0] copy[2];
  logic [3:0] later[2];
  assign copy = source;

  int unsigned values[3] = {5, 6, 7};
  int blocked;
  int tasked;

  task slow_task;
    #5 tasked = 1;
  endtask

  initial begin : worker
    #5 blocked = 1;
  end

  initial begin
    tasked = 0;
    blocked = 0;
    in = 0;
    source[0] = 4'd1;
    source[1] = 4'd2;
    #3;
    in = 1;
    later <= source;
    if (later[0] !== 4'bx) $display("FAIL nonblocking early");
    #1;
    if (out[0] !== 1'b0) $display("FAIL rise delay early");
    #2;
    if (out[0] !== 1'b1) $display("FAIL rise delay");
    source[1] = 4'd9;
    #0;
    fork
      slow_task;
      #1 disable slow_task;
    join
    #6;
    if ($bits(sized) != 6 || CFG.depth != 3 || DEF.width != 2)
      $display("FAIL keyed parameter %0d %0d %0d", $bits(sized), CFG.depth,
        DEF.width);
    else if (copy[0] != 4'd1 || copy[1] != 4'd9)
      $display("FAIL array assign %0d %0d", copy[0], copy[1]);
    else if (later[0] != 4'd1 || later[1] != 4'd2)
      $display("FAIL nonblocking array %0d %0d", later[0], later[1]);
    else if (values[0] != 5 || values[2] != 7)
      $display("FAIL concatenation initializer");
    else if (tasked != 0) $display("FAIL task disable");
    else if (blocked != 0) $display("FAIL block disable");
    else $display("PASS");
  end

  initial #2 disable worker;
endmodule
