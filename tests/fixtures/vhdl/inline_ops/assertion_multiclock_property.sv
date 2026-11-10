// SPDX-License-Identifier: Apache-2.0
//
// Multiclocked properties whose root is not a sequence (IEEE 1800-2023
// 16.13.2, 16.13.3, 16.16, Annex F.5) as generated code: implications and
// followed-by across clocks, property operators over differently clocked
// operands, if-else, nexttime, always, eventually, until, the abort operators
// and disable iff, over two and three clocks. Each is asserted; s<i> counts
// the successes (the pass statement runs for vacuous ones too, 16.14.1) and
// f<i> the failures.
//
// Clocks: c1 ticks at 10 j + 5 (j = 0..29) and leads every property (16.16);
// c2 at 20 i + 8, never with c1; c3 at 30 m + 15, always with a tick of c1.
// rows[j] holds {a, b, c, d, rst} for 10 j <= t < 10 j + 10, so every tick
// in that interval samples rows[j] (16.5.1). An evaluation at a point waits
// for the nearest tick of its clock at or after the point (`|->`, a clocked
// operand, the else branch of if-else) or strictly after it (`|=>`, `##1`,
// `#=#`); nexttime, always, eventually, until and if count ticks of the clock
// that flows into them (16.13.3). disable iff (rst) disables an attempt when
// rst holds at an evaluation (a tick of c1 or c2) from its start to its end;
// accept_on (d) samples d at every evaluation from its start point,
// sync_reject_on (d) at the ticks of c1 only. An attempt not decided when the
// counts are shown (t = 300) reports nothing (16.12). The counts come from an
// independent evaluation of these rules over the table:
//
//   k1   a |-> @(c2) b                         fail  9, success 21 (14 vacuous)
//   k2   a |-> @(c3) b                         fail 13, success 17 (14 vacuous)
//   k3   a |=> @(c3) b                         fail 12, success 17 (14 vacuous)
//   k4   a ##1 @(c2) b |=> @(c3) c             fail  3, success 27 (23 vacuous)
//   k5   a ##1 @(c2) b |-> c ##1 d             fail  5, success 25 (23 vacuous)
//   k6   a #-# @(c2) b                         fail 23, success  7 ( 0 vacuous)
//   k7   a #=# @(c3) b                         fail 26, success  3 ( 0 vacuous)
//   k8   a |-> (@(c2) b) and (@(c3) c)         fail 11, success 19 (14 vacuous)
//   k9   a |-> (@(c2) b) or (@(c3) c)          fail  5, success 25 (14 vacuous)
//   k10  a |-> not (@(c2) b ##1 c)             fail  4, success 26 (14 vacuous)
//   k11  a |-> ((@(c2) b) implies (@(c3) c))   fail  2, success 28 (19 vacuous)
//   k12  a |-> ((@(c2) b) iff (@(c3) c))       fail  6, success 24 (14 vacuous)
//   k13  (a ##1 b) or (b ##1 @(c2) c)          fail 16, success 14 ( 0 vacuous)
//   k14  if (a) (@(c2) b) else (@(c3) c)       fail 16, success 13 ( 0 vacuous)
//   k15  a |=> if (b) (@(c3) c)                fail  5, success 25 (22 vacuous)
//   k16  a |-> nexttime (@(c2) b)              fail  6, success 23 (14 vacuous)
//   k17  a |-> always [1:2] (@(c3) b)          fail 14, success 15 (14 vacuous)
//   k18  a |-> eventually [1:2] (@(c2) b)      fail  3, success 25 (14 vacuous)
//   k19  a |-> (@(c2) b) until c               fail  6, success 24 (14 vacuous)
//   k20  a |-> accept_on (d) (@(c2) b ##1 c)   fail  7, success 23 (20 vacuous)
//   k21  a |=> sync_reject_on (d) (@(c2) b ##1 c) fail 12, success 18 (14 vacuous)
//   k22  disable iff (rst) a |-> @(c2) b ##1 c fail 12, success 12 (11 vacuous)
//   k23  a ##[1:2] b |=> @(c2) c               fail  8, success 21 (17 vacuous)
module top;
  logic a, b, c, d, rst;
  bit c1, c2, c3;
  always #5 c1 = ~c1;
  initial begin
    #8;
    forever begin
      c2 = 1; #10; c2 = 0; #10;
    end
  end
  initial begin
    #15;
    forever begin
      c3 = 1; #15; c3 = 0; #15;
    end
  end
  reg [4:0] rows [0:29];
  initial begin
    rows[0] = 5'b01100;
    rows[1] = 5'b01000;
    rows[2] = 5'b10000;
    rows[3] = 5'b11100;
    rows[4] = 5'b01000;
    rows[5] = 5'b01100;
    rows[6] = 5'b10010;
    rows[7] = 5'b00000;
    rows[8] = 5'b11010;
    rows[9] = 5'b11000;
    rows[10] = 5'b10100;
    rows[11] = 5'b10101;
    rows[12] = 5'b01001;
    rows[13] = 5'b10100;
    rows[14] = 5'b11100;
    rows[15] = 5'b01000;
    rows[16] = 5'b10010;
    rows[17] = 5'b11000;
    rows[18] = 5'b10100;
    rows[19] = 5'b00000;
    rows[20] = 5'b01000;
    rows[21] = 5'b11100;
    rows[22] = 5'b01100;
    rows[23] = 5'b00101;
    rows[24] = 5'b11101;
    rows[25] = 5'b00101;
    rows[26] = 5'b01100;
    rows[27] = 5'b10100;
    rows[28] = 5'b10100;
    rows[29] = 5'b00000;
  end
  initial begin
    {a, b, c, d, rst} = 5'b01100;
    for (int k = 1; k <= 29; k++) begin
      #10;
      {a, b, c, d, rst} = rows[k];
    end
  end
  int f1, s1, f2, s2, f3, s3, f4, s4, f5, s5, f6, s6, f7, s7, f8, s8;
  int f9, s9, f10, s10, f11, s11, f12, s12, f13, s13, f14, s14, f15, s15;
  int f16, s16, f17, s17, f18, s18, f19, s19, f20, s20, f21, s21, f22, s22;
  int f23, s23;
  assert property (@(posedge c1) a |-> @(posedge c2) b) s1++; else f1++;
  assert property (@(posedge c1) a |-> @(posedge c3) b) s2++; else f2++;
  assert property (@(posedge c1) a |=> @(posedge c3) b) s3++; else f3++;
  assert property (@(posedge c1) a ##1 @(posedge c2) b |=> @(posedge c3) c) s4++; else f4++;
  assert property (@(posedge c1) a ##1 @(posedge c2) b |-> c ##1 d) s5++; else f5++;
  assert property (@(posedge c1) a #-# @(posedge c2) b) s6++; else f6++;
  assert property (@(posedge c1) a #=# @(posedge c3) b) s7++; else f7++;
  assert property (@(posedge c1) a |-> (@(posedge c2) b) and (@(posedge c3) c)) s8++; else f8++;
  assert property (@(posedge c1) a |-> (@(posedge c2) b) or (@(posedge c3) c)) s9++; else f9++;
  assert property (@(posedge c1) a |-> not (@(posedge c2) b ##1 c)) s10++; else f10++;
  assert property (@(posedge c1) a |-> ((@(posedge c2) b) implies (@(posedge c3) c)))
    s11++; else f11++;
  assert property (@(posedge c1) a |-> ((@(posedge c2) b) iff (@(posedge c3) c)))
    s12++; else f12++;
  assert property (@(posedge c1) (a ##1 b) or (b ##1 @(posedge c2) c)) s13++; else f13++;
  assert property (@(posedge c1) if (a) (@(posedge c2) b) else (@(posedge c3) c))
    s14++; else f14++;
  assert property (@(posedge c1) a |=> if (b) (@(posedge c3) c)) s15++; else f15++;
  assert property (@(posedge c1) a |-> nexttime (@(posedge c2) b)) s16++; else f16++;
  assert property (@(posedge c1) a |-> always [1:2] (@(posedge c3) b)) s17++; else f17++;
  assert property (@(posedge c1) a |-> eventually [1:2] (@(posedge c2) b)) s18++; else f18++;
  assert property (@(posedge c1) a |-> (@(posedge c2) b) until c) s19++; else f19++;
  assert property (@(posedge c1) a |-> accept_on (d) (@(posedge c2) b ##1 c))
    s20++; else f20++;
  assert property (@(posedge c1) a |=> sync_reject_on (d) (@(posedge c2) b ##1 c))
    s21++; else f21++;
  assert property (@(posedge c1) disable iff (rst) a |-> @(posedge c2) b ##1 c)
    s22++; else f22++;
  assert property (@(posedge c1) a ##[1:2] b |=> @(posedge c2) c) s23++; else f23++;
  initial begin
    #300;
    $display("inl mcp k1 f=%0d s=%0d", f1, s1);
    $display("inl mcp k2 f=%0d s=%0d", f2, s2);
    $display("inl mcp k3 f=%0d s=%0d", f3, s3);
    $display("inl mcp k4 f=%0d s=%0d", f4, s4);
    $display("inl mcp k5 f=%0d s=%0d", f5, s5);
    $display("inl mcp k6 f=%0d s=%0d", f6, s6);
    $display("inl mcp k7 f=%0d s=%0d", f7, s7);
    $display("inl mcp k8 f=%0d s=%0d", f8, s8);
    $display("inl mcp k9 f=%0d s=%0d", f9, s9);
    $display("inl mcp k10 f=%0d s=%0d", f10, s10);
    $display("inl mcp k11 f=%0d s=%0d", f11, s11);
    $display("inl mcp k12 f=%0d s=%0d", f12, s12);
    $display("inl mcp k13 f=%0d s=%0d", f13, s13);
    $display("inl mcp k14 f=%0d s=%0d", f14, s14);
    $display("inl mcp k15 f=%0d s=%0d", f15, s15);
    $display("inl mcp k16 f=%0d s=%0d", f16, s16);
    $display("inl mcp k17 f=%0d s=%0d", f17, s17);
    $display("inl mcp k18 f=%0d s=%0d", f18, s18);
    $display("inl mcp k19 f=%0d s=%0d", f19, s19);
    $display("inl mcp k20 f=%0d s=%0d", f20, s20);
    $display("inl mcp k21 f=%0d s=%0d", f21, s21);
    $display("inl mcp k22 f=%0d s=%0d", f22, s22);
    $display("inl mcp k23 f=%0d s=%0d", f23, s23);
    $finish;
  end
endmodule
