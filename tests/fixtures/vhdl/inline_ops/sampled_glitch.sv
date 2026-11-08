// SPDX-License-Identifier: Apache-2.0
//
// A clock with two rising edges in one time step (a glitch: 0->1->0->1 in
// deltas), and a that changes between the two edges. IEEE 1800-2023 16.5:
// a clock tick is "a time step when a clocking event ... occurs", and "if the
// clocking event transitions more than once during a time step, the
// resulting behavior is undefined". fsim ticks each assertion once per time
// step on both engines (one fail or cover line per step below), so this
// fixture pins engine parity on that undefined case, not an LRM result: the
// compiled kernel's inline sampled reads ($past, $sampled, $rose, $stable,
// $changed; 16.5.1, 16.9.3; task #186 stage 2) give the interpreter's values.
module top;
  logic clk = 0;
  logic [7:0] a = 0;
  initial begin
    repeat (6) begin
      #10 clk = 1; #0 a = a + 1; #0 clk = 0; #0 clk = 1; #0 clk = 0;
    end
    #10 $finish;
  end
  p_past: assert property (@(posedge clk) $past(a) == 8'hff)
    else $display("inl glitch t=%0t fail a=%0d past=%0d past2=%0d rose=%0b stable=%0b changed=%0b sampled=%0d",
                  $time, a, $past(a), $past(a, 2), $rose(a[0]), $stable(a), $changed(a), $sampled(a));
  c_past: cover property (@(posedge clk) $stable(a))
    $display("inl glitch t=%0t cover stable a=%0d past=%0d", $time, a, $past(a));
endmodule
