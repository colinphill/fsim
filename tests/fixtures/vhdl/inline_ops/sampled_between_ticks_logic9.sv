// SPDX-License-Identifier: Apache-2.0
//
// Value change functions with an explicit clocking event on nets a VHDL top
// level drives with std_logic (sampled_between_ticks_logic9_vhdl.vhd), called
// in time steps that are not ticks of that clock (IEEE 1800-2023 16.9.3: the
// sampled value of the time step against the sample of the most recent
// strictly prior tick; 21.2.1.3 maps 'H' to 1 and 'L' to 0, 'Z' reads as z).
// sb is one std_logic, sv a 70 element vector (a ring of several chunks).
// A SystemVerilog value change function compares the values SystemVerilog
// reads (the mixed contract's 21.2.1.3 / UG900 mapping; 'H' reads as 1 with
// weak strength, and strength is not part of the value 16.9.3 compares), so
// 'H' -> '1', 'L' -> '0' and 'Z' -> 'Z' are no change, at a tick and between
// ticks.
//
// clk has its posedges at 15, 25, ..., 85, clk2 at 23 and 33. sb is '0'
// from 2, 'H' from 22, 'L' from 32, 'H' from 52, '1' from 58, 'L' from 68 and
// '0' from 78. sv is all '0' from 2; element 65 is 'Z' from 22, 'H' from 32
// and 'Z' from 52 (assigned 'Z' again at 58); element 3 is 'H' from 52, '1'
// from 58, 'L' from 68 and '0' from 78. The samples at the posedges of clk:
//
//   tick   15  25  35  45  55  65  75  85
//   sb     '0' 'H' 'L' 'L' 'H' '1' 'L' '0'
//   sv(65) '0' 'Z' 'H' 'H' 'Z' 'Z' 'Z' 'Z'
//   sv(3)  '0' '0' '0' '0' 'H' '1' 'L' '0'
//
// Procedural reads:
//
//   t     sb now vs prior tick   rose fell stable  sv changed  sv stable
//   23    'H' vs 15: '0'         1    0    0       1           0
//   26    'H' vs 25: 'H'         0    0    1       0           1
//   33    'L' vs 25: 'H'         0    1    0       1           0
//   36    'L' vs 35: 'L'         0    0    1       0           1
//   59    '1' vs 55: 'H'         0    0    1       0           1
//         (sv(3) '1' vs 'H', sv(65) 'Z' vs 'Z')
//   65    '1' vs 55: 'H' (a tick, compared with 55)
//                                0    0    1       0           1
//   66    '1' vs 65: '1'         0    0    1       0           1
//   69    'L' vs 65: '1'         0    1    0       1           0
//   79    '0' vs 75: 'L'         0    0    1       0           1
//   85    '0' vs 75: 'L' (a tick)
//                                0    0    1       0           1
//
// Assertions clocked by clk2 (each cover adds the time of its successes):
// $rose(sb) at 23 (sum 23), $fell(sb) at 33 (33), $changed(sv) at 23 and 33
// (56).
//
// Assertions clocked by clk itself, at its ticks after 50 (each cover adds
// the time of its successes): $rose(sb) at 55 ('H' vs 'L'; sum 55), $fell(sb)
// at 75 ('L' vs '1'; 75), $stable(sb) at 65 ('1' vs 'H') and 85 ('0' vs
// 'L'; 150), $changed(sv) at 55 and 75 (130; not at 65 or 85).
`timescale 1ns/1ns
module svm(input wire sb, input wire [69:0] sv);
  logic clk = 0, clk2 = 0;
  int rose_sum = 0, fell_sum = 0, changed_sum = 0;
  int tick_rose = 0, tick_fell = 0, tick_stable = 0, tick_changed = 0;
  initial begin
    #15;
    repeat (8) begin
      clk = 1; #2 clk = 0; #8;
    end
  end
  initial begin
    #23;
    repeat (2) begin
      clk2 = 1; #2 clk2 = 0; #8;
    end
  end
  task automatic probe;
    $display("inl l9 t=%0d rose=%0b fell=%0b stable=%0b svchg=%0b svstable=%0b",
      $time, $rose(sb, @(posedge clk)), $fell(sb, @(posedge clk)),
      $stable(sb, @(posedge clk)), $changed(sv, @(posedge clk)),
      $stable(sv, @(posedge clk)));
  endtask
  initial begin
    #23 probe();        // 23
    #3 probe();         // 26
    #7 probe();         // 33
    #3 probe();         // 36
    #14;
    $display("inl l9 clk2 rose=%0d fell=%0d changed=%0d",
      rose_sum, fell_sum, changed_sum);
    #9 probe();         // 59
    #6 probe();         // 65
    #1 probe();         // 66
    #3 probe();         // 69
    #10 probe();        // 79
    #6 probe();         // 85
    #10;
    $display("inl l9 ticks rose=%0d fell=%0d stable=%0d changed=%0d",
      tick_rose, tick_fell, tick_stable, tick_changed);
    $finish;
  end
  cover property (@(posedge clk2) $rose(sb, @(posedge clk))) rose_sum += $time;
  cover property (@(posedge clk2) $fell(sb, @(posedge clk))) fell_sum += $time;
  cover property (@(posedge clk2) $changed(sv, @(posedge clk))) changed_sum += $time;
  cover property (@(posedge clk) $rose(sb)) if ($time > 50) tick_rose += $time;
  cover property (@(posedge clk) $fell(sb)) if ($time > 50) tick_fell += $time;
  cover property (@(posedge clk) $stable(sb)) if ($time > 50) tick_stable += $time;
  cover property (@(posedge clk) $changed(sv)) if ($time > 50) tick_changed += $time;
endmodule
