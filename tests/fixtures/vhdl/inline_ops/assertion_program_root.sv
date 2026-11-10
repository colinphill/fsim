// SPDX-License-Identifier: Apache-2.0
//
// A top-level program with a continuous assignment and a concurrent
// assertion with disable iff elaborates: only its elaboration severity
// tasks run at elaboration (IEEE 1800-2023 20.11), and its continuous
// assignments, among them the hidden net of the disable condition (16.12),
// run in the simulation (24.3.1). clk rises at 5 + 10k, when cyc is k, for
// k = 0 to 9; each mask has bit k for the tick at cyc == k.
//
//   w = (cyc < 2) by a continuous assignment, read at each tick: ticks
//     0-1:                                                         3
//   disable iff (cyc < 2) on `0`: the failures are ticks 2-9:    1020
//   default disable iff (cyc < 5 || cyc == 8) on `0` (16.15): the
//     failures are ticks 5-7 and 9:                               736
//
// The program ends when its initial procedure does, after the tick at 95
// (24.3).
program top;
  bit clk;
  int cyc;
  bit w;
  int w_mask, fail_explicit, fail_default;
  assign w = cyc < 2;
  default disable iff (cyc < 5 || cyc == 8);
  assert property (@(posedge clk) disable iff (cyc < 2) 0)
    else fail_explicit |= 1 << cyc;
  assert property (@(posedge clk) 0) else fail_default |= 1 << cyc;
  initial begin
    repeat (10) begin
      #5 clk = 1;
      if (w) w_mask |= 1 << cyc;
      #5 clk = 0;
      cyc++;
    end
    $display("inl root w=%0d explicit=%0d default=%0d",
      w_mask, fail_explicit, fail_default);
  end
endprogram
