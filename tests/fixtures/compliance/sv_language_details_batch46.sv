// fsim: top=sv_language_details_batch46
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2017 23.3.3.5: multidimensional interface instance arrays;
// 25.5: a non-ANSI interface port; 25.10: a parameter read through an
// interface port in a constant expression; 6.20.5: a specparam used as a
// constant; 7.10.1: queue slices and string queue methods; 9.7: process
// status; 10.9.1: an index-keyed array pattern; 13.4.1: a static void
// function called as a statement; 17.9.1 of IEEE 1364-2005 and Annex N:
// $random with a seed.
`timescale 1ns / 1ns
interface batch46_if;
  localparam int WIDTH = 6;
  logic [3:0] value;
endinterface

module batch46_reader(port);
  batch46_if port;
  localparam int DOUBLE = port.WIDTH * 2;
  logic [DOUBLE-1:0] wide;
  assign wide = {DOUBLE{1'b1}};
endmodule

class batch46_counter;
  static int total;
  static function void bump(int by);
    total += by;
  endfunction
endclass

module sv_language_details_batch46;
  specparam DELAY = 3;
  batch46_if grid[2][3]();
  batch46_if single();
  batch46_reader reader(single);
  int values[$];
  int slice[$];
  string words[$];
  string first_word;
  int keyed[4];
  integer seed;
  integer drawn;
  process handle;

  initial begin
    grid[1][2].value = 4'd9;
    grid[0][1].value = 4'd3;
    values = '{1, 2, 3, 4, 5};
    slice = values[1:3];
    words.push_back("alpha");
    words.push_back("beta");
    first_word = words.pop_front();
    keyed = '{2: 7, default: 1};
    batch46_counter::bump(4);
    batch46_counter::bump(5);
    seed = 1;
    drawn = $random(seed);
    handle = process::self();
    #DELAY;
    if (grid[1][2].value != 4'd9 || grid[0][1].value != 4'd3)
      $display("FAIL interface array");
    else if ($bits(reader.wide) != 12 || reader.wide != 12'hfff)
      $display("FAIL interface port parameter");
    else if ($time != 3) $display("FAIL specparam delay");
    else if (slice.size() != 3 || slice[0] != 2 || slice[2] != 4)
      $display("FAIL queue slice");
    else if (first_word != "alpha" || words.size() != 1 || words[0] != "beta")
      $display("FAIL string queue");
    else if (keyed[2] != 7 || keyed[0] != 1 || keyed[3] != 1)
      $display("FAIL keyed pattern");
    else if (batch46_counter::total != 9) $display("FAIL static function");
    else if (seed != 32'h00010dce || drawn != 32'h80010e00)
      $display("FAIL seeded random %h %h", seed, drawn);
    else if (handle.status() != process::RUNNING)
      $display("FAIL process status");
    else $display("PASS");
  end
endmodule
