// SPDX-License-Identifier: Apache-2.0
//
// Procedural concurrent assertions with asynchronous aborts (IEEE 1800-2023
// 16.14.6, 16.12.14, 16.13): multiclocked properties, whose clocks report
// their ticks through tick bits of their own, and a singly clocked one that
// evaluates between its clock's ticks too. An attempt starts only at a tick
// of the property's leading clock after its procedure reached the statement
// (16.14.6.2), and the procedure's enable is consumed at that tick: the
// ticks after it start nothing.
//
// Clocks: c1 rises at 10, 30, 50, 70, 90, 110; c2 at 15, 35, 55, 75, 95,
// 115. The procedure runs at each c1 tick and reaches the statements only
// at 30 (a is 1 from 25 to 35). The multiclocked properties lead with c2:
// their attempts start at 35, where b is sampled 1 (b is 1 from 34 to 44),
// and end at the c1 tick 50, where b is sampled 0.
//   p1  accept_on(d) (@(posedge c2) b ##1 @(posedge c1) b): would fail at
//       50; d is 1 from 42 to 43, so the attempt is accepted at 43.
//   p2  reject_on(r) (@(posedge c2) b ##1 @(posedge c1) !b): would pass at
//       50; r is 1 from 46 to 47, so the attempt is rejected at 47.
//   p3  accept_on(e) (@(posedge c2) b ##1 @(posedge c1) b), e never 1:
//       fails at 50.
//   p4  accept_on(s) (1 ##2 b), clocked by the procedure's posedge c1: the
//       attempt starts at 30 and would fail at 70; s is 1 from 62 to 63, so
//       it is accepted at 63.
module top;
  bit c1, c2;
  logic a = 0, b = 0, d = 0, r = 0, e = 0, s = 0;
  always #10 c1 = ~c1;
  initial begin
    #15;
    forever begin
      c2 = 1; #10; c2 = 0; #10;
    end
  end
  initial begin
    #25 a = 1;
    #10 a = 0;  // 35
  end
  initial begin
    #34 b = 1;
    #8 d = 1;   // 42
    #1 d = 0;   // 43
    #1 b = 0;   // 44
    #2 r = 1;   // 46
    #1 r = 0;   // 47
    #15 s = 1;  // 62
    #1 s = 0;   // 63
  end
  always @(posedge c1) begin
    if (a) p1: assert property (accept_on(d)
        (@(posedge c2) b ##1 @(posedge c1) b))
      $display("inl p1 pass t=%0t", $time);
    else $display("inl p1 fail t=%0t", $time);
    if (a) p2: assert property (reject_on(r)
        (@(posedge c2) b ##1 @(posedge c1) !b))
      $display("inl p2 pass t=%0t", $time);
    else $display("inl p2 fail t=%0t", $time);
    if (a) p3: assert property (accept_on(e)
        (@(posedge c2) b ##1 @(posedge c1) b))
      $display("inl p3 pass t=%0t", $time);
    else $display("inl p3 fail t=%0t", $time);
    if (a) p4: assert property (accept_on(s) (1 ##2 b))
      $display("inl p4 pass t=%0t", $time);
    else $display("inl p4 fail t=%0t", $time);
  end
  initial #120 $finish;
endmodule
