// SPDX-License-Identifier: Apache-2.0
//
// A program's continuous assignments run (IEEE 1800-2023 24.3.1, 24.3.2),
// and its concurrent assertions have the scheduling of design code (24.3.1):
// the program p and the module m hold the same assertions and record the
// same ticks. clk rises at 5 + 10k, when cyc is k (cyc counts at the falling
// edges), for k = 0 to 9; each mask has bit k for the tick at cyc == k.
//
//   default disable iff (cyc < 4) on `0` (16.15): ticks 0-3 are disabled,
//     the failures are ticks 4-9:                               1008
//   disable iff (cyc >= 7) overrides the default: failures 0-6:  127
//   disable iff (rst), where rst = (cyc >= 3 && cyc < 6) is assigned in
//     the Active region of each tick by design code: the disable condition
//     reads current values in the Observed region where the attempt starts
//     (16.12), rst is already the new value there, so ticks 3-5 are
//     disabled and the failures are 0-2 and 6-9 (the values of the previous
//     tick would give 0-3 and 7-9, 911):                         967
//   reject_on(pulse) 1 ##1 1 with disable iff (1'b0): pulse is 1 from 7 to
//     9, between the ticks at 5 and 15; asynchronously checked with sampled
//     values at every time step of an attempt (16.12.14), the attempt from
//     tick 0 is rejected at 9, where pulse samples 1 (cyc is still 0):  1
//
// In the program, w = (cyc < 4) by a continuous assignment, read by the
// program at each tick (ticks 0-3: 15), and its output port ow = !w drives
// the module's net dw, read by the module at each tick (ticks 4-9: 1008).
module top;
  bit clk;
  int cyc;
  bit rst;
  bit pulse;
  wire dw;
  int p_default, p_explicit, p_rst, p_reject, p_w;
  int m_default, m_explicit, m_rst, m_reject;
  int dw_mask;
  always #5 clk = !clk;
  always @(negedge clk) cyc++;
  always @(posedge clk) rst = cyc >= 3 && cyc < 6;
  always @(posedge clk) if (dw === 1'b1) dw_mask |= 1 << cyc;
  initial begin
    #7 pulse = 1;
    #2 pulse = 0;
  end
  p u_p (.clk(clk), .cyc(cyc), .rst(rst), .pulse(pulse), .ow(dw),
    .fail_default(p_default), .fail_explicit(p_explicit),
    .fail_rst(p_rst), .fail_reject(p_reject), .w_mask(p_w));
  m u_m (.clk(clk), .cyc(cyc), .rst(rst), .pulse(pulse),
    .fail_default(m_default), .fail_explicit(m_explicit),
    .fail_rst(m_rst), .fail_reject(m_reject));
  initial begin
    #100;
    $display("inl program default=%0d explicit=%0d rst=%0d reject=%0d",
      p_default, p_explicit, p_rst, p_reject);
    $display("inl module default=%0d explicit=%0d rst=%0d reject=%0d",
      m_default, m_explicit, m_rst, m_reject);
    $display("inl assign w=%0d dw=%0d", p_w, dw_mask);
    $finish;
  end
endmodule

program p(input bit clk, input int cyc, input bit rst, input bit pulse,
    output wire ow, output int fail_default, output int fail_explicit,
    output int fail_rst, output int fail_reject, output int w_mask);
  bit w;
  assign w = cyc < 4;
  assign ow = !w;
  default disable iff (cyc < 4);
  assert property (@(posedge clk) 0) else fail_default |= 1 << cyc;
  assert property (@(posedge clk) disable iff (cyc >= 7) 0)
    else fail_explicit |= 1 << cyc;
  assert property (@(posedge clk) disable iff (rst) 0)
    else fail_rst |= 1 << cyc;
  assert property (@(posedge clk) disable iff (1'b0)
      reject_on(pulse) 1'b1 ##1 1'b1)
    else fail_reject |= 1 << cyc;
  initial forever @(posedge clk) if (w) w_mask |= 1 << cyc;
endprogram

module m(input bit clk, input int cyc, input bit rst, input bit pulse,
    output int fail_default, output int fail_explicit, output int fail_rst,
    output int fail_reject);
  default disable iff (cyc < 4);
  assert property (@(posedge clk) 0) else fail_default |= 1 << cyc;
  assert property (@(posedge clk) disable iff (cyc >= 7) 0)
    else fail_explicit |= 1 << cyc;
  assert property (@(posedge clk) disable iff (rst) 0)
    else fail_rst |= 1 << cyc;
  assert property (@(posedge clk) disable iff (1'b0)
      reject_on(pulse) 1'b1 ##1 1'b1)
    else fail_reject |= 1 << cyc;
endmodule
