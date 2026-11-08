// fsim: top=top std=2017
// SPDX-License-Identifier: Apache-2.0
// A guard on one event of a multi-event control (IEEE 1800-2023 9.4.2.3,
// 16.14.6 example): `@(posedge clk iff en or posedge rst)` wakes at a posedge
// of clk only while en is 1, and at every posedge of rst. The wake tests the
// guard of the event that fired.
//
// Derivation. clk starts 0 and toggles every 5, so its posedges are at 5, 15,
// 25, 35 and 45. en is 1 in [12, 22) and [32, 42) (set at 12, cleared at 22,
// set at 32, cleared at 42). rst pulses high at 26 and 36 and low at 28 and
// 38. The simulation ends at 52.
//  - n1: `posedge clk iff en or posedge rst`: clk at 15 and 35 (en is 1), rst
//    at 26 and 36: 4 wakes.
//  - n2: `posedge clk iff en or posedge rst iff en`: clk at 15 and 35, rst at
//    36 only (en is 0 at 26): 3 wakes.
//  - n3: a statement-level control in a loop, `@(posedge clk iff en or
//    posedge rst iff !en)`: clk at 15 and 35, rst at 26 only (en is 1 at 36):
//    3 wakes.
//  - n4: the same as n1 in an always_ff: 4 wakes.
//  - 16.14.6: in the last procedure `rst` is used in the body, so `posedge
//    rst` cannot be the inferred clock and the clock is `posedge clk iff rst
//    == 0`: the inferred cover counts the same attempts as the one that
//    writes that clock, and there are some (the posedges 5, 15, 25, 35, 45,
//    at none of which rst is 1).
module top;
  logic clk = 0, rst = 0, en = 0, q = 0;
  int n1 = 0, n2 = 0, n3 = 0, n4 = 0, cv_inf = 0, cv_exp = 0;

  always #5 clk = ~clk;
  initial begin
    #12 en = 1;
    #10 en = 0;
    #10 en = 1;
    #10 en = 0;
  end
  initial begin
    #26 rst = 1;
    #2 rst = 0;
    #8 rst = 1;
    #2 rst = 0;
  end

  always @(posedge clk iff en or posedge rst) n1++;
  always @(posedge clk iff en or posedge rst iff en) n2++;
  always begin
    @(posedge clk iff en or posedge rst iff !en);
    n3++;
  end
  always_ff @(posedge clk iff en or posedge rst) n4 <= n4 + 1;

  always @(posedge clk iff rst == 0 or posedge rst) begin
    q <= rst ? 0 : 1;
    cv_inf_p: cover property (1'b1) cv_inf++;
    cv_exp_p: cover property (@(posedge clk iff rst == 0) 1'b1) cv_exp++;
  end

  initial begin
    #52;
    if (n1 != 4 || n2 != 3 || n3 != 3 || n4 != 4) begin
      $display("FAIL n1=%0d n2=%0d n3=%0d n4=%0d", n1, n2, n3, n4);
    end else if (cv_inf != cv_exp || cv_inf == 0) begin
      $display("FAIL cover %0d %0d", cv_inf, cv_exp);
    end else begin
      $display("PASS");
    end
    // The clock runs forever; the check ends the run.
    $finish;
  end
endmodule
