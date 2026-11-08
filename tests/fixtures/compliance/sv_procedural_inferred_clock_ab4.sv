// fsim: top=top std=2017
// SPDX-License-Identifier: Apache-2.0
// The clock inferred for a procedural concurrent assertion (IEEE 1800-2023
// 16.14.6). Each procedure holds a cover with the inferred clock and one that
// writes the clock the rule gives out, so by 16.14.6 they count the same
// attempts:
//  - a) the event control lists posedge clk and negedge rst; rst is used in
//    the body, so posedge rst cannot be the clock (c, 2) and the one event
//    expression left, posedge clk, is.
//  - b) `posedge clk iff en`: the clock is the whole event expression, so the
//    attempts begin at the posedges of clk at which en is 1; they are
//    fewer than the posedges of clk.
//  - c) an initial procedure with one event control, @(posedge clk), infers
//    that edge.
module top;
  logic clk = 0, rst = 1, en = 0, q = 0;
  int a_inf = 0, a_exp = 0, b_inf = 0, b_exp = 0, c_inf = 0, c_exp = 0;
  int edges = 0;

  always #5 clk = ~clk;
  always @(posedge clk) edges++;
  // en is 1 in the cycles that start at 15 and 35 only.
  initial begin
    #12 en = 1;
    #10 en = 0;
    #10 en = 1;
    #10 en = 0;
  end

  always @(posedge clk or negedge rst) begin
    if (!rst) q <= 0;
    ca_inf: cover property (1'b1) a_inf++;
    ca_exp: cover property (@(posedge clk) 1'b1) a_exp++;
  end

  always @(posedge clk iff en) begin
    cb_inf: cover property (1'b1) b_inf++;
    cb_exp: cover property (@(posedge clk iff en) 1'b1) b_exp++;
  end

  initial begin
    @(posedge clk);
    cc_inf: cover property (1'b1) c_inf++;
    cc_exp: cover property (@(posedge clk) 1'b1) c_exp++;
  end

  initial begin
    #100;
    if (a_inf != a_exp || a_inf == 0) begin
      $display("FAIL a %0d %0d", a_inf, a_exp);
    end else if (b_inf != b_exp || b_inf == 0 || b_inf >= edges) begin
      $display("FAIL b %0d %0d edges %0d", b_inf, b_exp, edges);
    end else if (c_inf != c_exp || c_inf == 0) begin
      $display("FAIL c %0d %0d", c_inf, c_exp);
    end else begin
      $display("PASS");
    end
    // The clock runs forever; the check ends the run.
    $finish;
  end
endmodule
