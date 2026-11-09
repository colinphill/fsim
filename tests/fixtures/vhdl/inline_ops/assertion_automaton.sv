// SPDX-License-Identifier: Apache-2.0
//
// The attempt automaton of a concurrent assertion directive as generated code
// (IEEE 1800-2023 16.7-16.14; task #186): sequences with delays, ranges,
// consecutive, goto and nonconsecutive repetition, `not`, the overlapped and
// non-overlapped implications and followed-by (`#-#`, `#=#`), `disable iff`
// and the sampled value functions, each asserted (the failures are counted by
// the else action) and covered (every success, vacuous ones included: the pass
// statement of `cover property` runs for each successful attempt, 16.14.3) over 48 clock
// ticks, plus `cover sequence`, which counts every match (16.14.3).
//
// Stimulus: rows[k] is applied at the negedge 10 (k + 1); the posedge of
// sample n (t = 10 n + 5) samples the preponed values, which are rows[n - 1]
// for n >= 1 and the initial 000 for n = 0 (16.5.1). dis[k] is the disable
// condition applied the same way. An attempt that has not been decided when
// the run ends reports nothing (weak, 16.12). The counts f<i> / s<i> are
// derived from the table by an independent evaluation of the sequence and
// property semantics over the sampled values (16.9, 16.12.3-16.12.7):
//
//   p1   a ##1 b                      fail 35, success 12 ( 0 vacuous)
//   p2   a ##[1:3] b                  fail 27, success 20 ( 0 vacuous)
//   p3   a ##1 b[*2]                  fail 40, success  7 ( 0 vacuous)
//   p4   a[*2:3] ##1 b                fail 42, success  5 ( 0 vacuous)
//   p5   a ##1 b[->1] ##1 c           fail 34, success 13 ( 0 vacuous)
//   p6   not (a ##1 b)                fail 12, success 35 ( 0 vacuous)
//   p7   a |-> b ##1 c                fail 15, success 33 (27 vacuous)
//   p8   a |=> b                      fail  8, success 39 (27 vacuous)
//   p9   a ##1 b |=> c[*2]            fail  8, success 39 (35 vacuous)
//   p10  a |-> ##[1:2] b              fail  2, success 45 (27 vacuous)
//   p11  a #-# b                      fail 35, success 13 ( 0 vacuous)
//   p12  a #=# b                      fail 35, success 12 ( 0 vacuous)
//   p13  a ##1 b[=2]                  fail 27, success 20 ( 0 vacuous)
//   p14  disable iff (d) a |-> ##1 b  fail  7, success 37 (25 vacuous)
//   p15  $rose(a) |=> b               fail  6, success 41 (33 vacuous)
//   p16  $fell(a) |-> b               fail  5, success 43 (34 vacuous)
//   p17  $stable(c) && a |-> ##1 b    fail  7, success 40 (34 vacuous)
//   p18  $changed(b) |=> a            fail 16, success 31 (22 vacuous)
//   p19  $past(a, 2) |-> b            fail  7, success 41 (28 vacuous)
//   p20  c throughout (a ##1 b)       fail 42, success  5 ( 0 vacuous)
//   p21  first_match(a ##[1:3] b)     fail 27, success 20 ( 0 vacuous)
//   p22  a |-> first_match(b ##[1:2] c) fail 12, success 36 (27 vacuous)
//   p23  b throughout (a ##[1:2] c)   fail 43, success  5 ( 0 vacuous)
//   p24  (a ##1 b) and (b ##1 c)      fail 45, success  3 ( 0 vacuous)
//   p25  (a ##[1:2] b) intersect (c[*2:3]) fail 41, success  6 ( 0 vacuous)
//   p26  b within (a ##2 c)           fail 35, success 12 ( 0 vacuous)
//   p27  a |-> ((b ##1 c) and (c ##1 a)) fail 21, success 27 (27 vacuous)
//   p28  (a ##1 b) and ((a ##[1:2] b) and ((a ##[1:3] b) and ((a[*1:2] ##1 b) and ((a ##1 b[*1:2]) and ((a ##[1:2] b) and (a ##1 b)))))) fail 35, success 12 ( 0 vacuous)
//   p29  (((((((a ##1 b) and (a ##[1:2] b)) and (a ##[1:3] b)) and (a[*1:2] ##1 b)) and (a ##1 b[*1:2])) and (a ##[1:2] b)) and (a ##1 b)) fail 35, success 12 ( 0 vacuous)
//   t1   not (not (a ##1 b))          fail 35, success 12 ( 0 vacuous)
//   t2   (not (a ##1 b)) and (not (b ##1 c)) fail 27, success 20 ( 0 vacuous)
//   t3   (not (a ##1 b)) or (not (b ##1 c)) fail  3, success 45 ( 0 vacuous)
//   t4   (a ##1 b) implies (b ##1 c)  fail  9, success 38 (35 vacuous)
//   t5   (a ##1 b) iff (b ##1 c)      fail 24, success 23 ( 0 vacuous)
//   t6   if (a) (b ##1 c) else (c ##1 b) fail 33, success 15 ( 0 vacuous)
//   t7   if (a) (b ##1 c)             fail 15, success 33 (27 vacuous)
//   t8   nexttime (a ##1 b)           fail 34, success 12 ( 0 vacuous)
//   t9   nexttime [2] (a ##1 b)       fail 34, success 11 ( 0 vacuous)
//   t10  accept_on (d) (a ##1 b)      fail 32, success 15 ( 3 vacuous)
//   t11  reject_on (d) (a ##1 b)      fail 35, success 12 ( 0 vacuous)
//   t12  sync_accept_on (d) (a ##1 b) fail 32, success 15 ( 3 vacuous)
//   u1   always a                     fail 47, success  0 ( 0 vacuous)
//   u2   always (a ##1 b)             fail 47, success  0 ( 0 vacuous)
//   u3   always [1:3] c               fail 37, success  8 ( 0 vacuous)
//   u4   s_always [0:2] (b ##1 c)     fail 46, success  2 ( 0 vacuous)
//   u5   eventually [1:3] b           fail  1, success 45 ( 0 vacuous)
//   u6   s_eventually [1:4] (a ##1 b) fail  9, success 34 ( 0 vacuous)
//   u7   s_eventually a               fail  0, success 48 ( 0 vacuous)
//   u8   a until b                    fail 10, success 37 ( 0 vacuous)
//   u9   a until_with b               fail 31, success 16 ( 0 vacuous)
//   u10  a s_until b                  fail 10, success 37 ( 0 vacuous)
//   u11  always [0:4] (a || b)        fail 37, success  7 ( 0 vacuous)
//   u12  s_eventually (b ##1 c)       fail  0, success 47 ( 0 vacuous)
//   v1   a |-> not (b ##1 c)          fail  6, success 42 (27 vacuous)
//   v2   a |=> (b until c)            fail  5, success 42 (27 vacuous)
//   v3   a |-> always [0:2] b         fail 17, success 31 (27 vacuous)
//   v4   a |-> s_eventually [1:3] c   fail  3, success 44 (27 vacuous)
//   v5   a |=> nexttime b             fail  7, success 40 (27 vacuous)
//   v6   a #-# not (b ##1 c)          fail 33, success 15 ( 0 vacuous)
//   v7   a |-> (if (b) (c ##1 a) else (a ##1 c)) fail 15, success 32 (27 vacuous)
//   v8   a |-> (b |-> c)              fail  5, success 43 (27 vacuous)
//   v9   (a ##1 b) |=> (c implies a)  fail  4, success 43 (35 vacuous)
//   cs   cover sequence (a ##1 b[*1:2])  matches 19
//   cf   cover sequence (first_match(a ##[1:3] b))  matches 20
module top;
  bit clk = 0;
  always #5 clk = ~clk;
  logic a = 0, b = 0, c = 0, d = 0;
  int cyc = 0;
  logic [2:0] rows [0:47] = '{3'b111, 3'b010, 3'b011, 3'b111, 3'b001, 3'b111, 3'b010, 3'b011, 3'b100, 3'b000, 3'b100, 3'b110, 3'b101, 3'b111, 3'b011, 3'b000, 3'b110, 3'b011, 3'b101, 3'b111, 3'b000, 3'b000, 3'b010, 3'b010, 3'b101, 3'b011, 3'b110, 3'b011, 3'b111, 3'b000, 3'b010, 3'b110, 3'b010, 3'b110, 3'b101, 3'b011, 3'b111, 3'b000, 3'b011, 3'b111, 3'b100, 3'b010, 3'b001, 3'b011, 3'b011, 3'b011, 3'b101, 3'b111};
  logic dis [0:47] = '{1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b1, 1'b1, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b1, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0};
  always @(negedge clk) begin
    {a, b, c} = rows[cyc];
    d = dis[cyc];
    cyc <= cyc + 1;
  end
  int f1 = 0, s1 = 0, f2 = 0, s2 = 0, f3 = 0, s3 = 0, f4 = 0, s4 = 0, f5 = 0, s5 = 0, f6 = 0, s6 = 0, f7 = 0, s7 = 0, f8 = 0, s8 = 0, f9 = 0, s9 = 0, f10 = 0, s10 = 0, f11 = 0, s11 = 0, f12 = 0, s12 = 0, f13 = 0, s13 = 0, f14 = 0, s14 = 0, f15 = 0, s15 = 0, f16 = 0, s16 = 0, f17 = 0, s17 = 0, f18 = 0, s18 = 0, f19 = 0, s19 = 0, f20 = 0, s20 = 0, f21 = 0, s21 = 0, f22 = 0, s22 = 0, f23 = 0, s23 = 0, f24 = 0, s24 = 0, f25 = 0, s25 = 0, f26 = 0, s26 = 0, f27 = 0, s27 = 0, f28 = 0, s28 = 0, f29 = 0, s29 = 0, f30 = 0, s30 = 0, f31 = 0, s31 = 0, f32 = 0, s32 = 0, f33 = 0, s33 = 0, f34 = 0, s34 = 0, f35 = 0, s35 = 0, f36 = 0, s36 = 0, f37 = 0, s37 = 0, f38 = 0, s38 = 0, f39 = 0, s39 = 0, f40 = 0, s40 = 0, f41 = 0, s41 = 0, f42 = 0, s42 = 0, f43 = 0, s43 = 0, f44 = 0, s44 = 0, f45 = 0, s45 = 0, f46 = 0, s46 = 0, f47 = 0, s47 = 0, f48 = 0, s48 = 0, f49 = 0, s49 = 0, f50 = 0, s50 = 0, f51 = 0, s51 = 0, f52 = 0, s52 = 0, f53 = 0, s53 = 0, f54 = 0, s54 = 0, f55 = 0, s55 = 0, f56 = 0, s56 = 0, f57 = 0, s57 = 0, f58 = 0, s58 = 0, f59 = 0, s59 = 0, f60 = 0, s60 = 0, f61 = 0, s61 = 0, f62 = 0, s62 = 0, cs = 0, cf = 0;
  assert property (@(posedge clk) a ##1 b) else f1++;
  cover property (@(posedge clk) a ##1 b) s1++;
  assert property (@(posedge clk) a ##[1:3] b) else f2++;
  cover property (@(posedge clk) a ##[1:3] b) s2++;
  assert property (@(posedge clk) a ##1 b[*2]) else f3++;
  cover property (@(posedge clk) a ##1 b[*2]) s3++;
  assert property (@(posedge clk) a[*2:3] ##1 b) else f4++;
  cover property (@(posedge clk) a[*2:3] ##1 b) s4++;
  assert property (@(posedge clk) a ##1 b[->1] ##1 c) else f5++;
  cover property (@(posedge clk) a ##1 b[->1] ##1 c) s5++;
  assert property (@(posedge clk) not (a ##1 b)) else f6++;
  cover property (@(posedge clk) not (a ##1 b)) s6++;
  assert property (@(posedge clk) a |-> b ##1 c) else f7++;
  cover property (@(posedge clk) a |-> b ##1 c) s7++;
  assert property (@(posedge clk) a |=> b) else f8++;
  cover property (@(posedge clk) a |=> b) s8++;
  assert property (@(posedge clk) a ##1 b |=> c[*2]) else f9++;
  cover property (@(posedge clk) a ##1 b |=> c[*2]) s9++;
  assert property (@(posedge clk) a |-> ##[1:2] b) else f10++;
  cover property (@(posedge clk) a |-> ##[1:2] b) s10++;
  assert property (@(posedge clk) a #-# b) else f11++;
  cover property (@(posedge clk) a #-# b) s11++;
  assert property (@(posedge clk) a #=# b) else f12++;
  cover property (@(posedge clk) a #=# b) s12++;
  assert property (@(posedge clk) a ##1 b[=2]) else f13++;
  cover property (@(posedge clk) a ##1 b[=2]) s13++;
  assert property (@(posedge clk) disable iff (d) a |-> ##1 b) else f14++;
  cover property (@(posedge clk) disable iff (d) a |-> ##1 b) s14++;
  assert property (@(posedge clk) $rose(a) |=> b) else f15++;
  cover property (@(posedge clk) $rose(a) |=> b) s15++;
  assert property (@(posedge clk) $fell(a) |-> b) else f16++;
  cover property (@(posedge clk) $fell(a) |-> b) s16++;
  assert property (@(posedge clk) $stable(c) && a |-> ##1 b) else f17++;
  cover property (@(posedge clk) $stable(c) && a |-> ##1 b) s17++;
  assert property (@(posedge clk) $changed(b) |=> a) else f18++;
  cover property (@(posedge clk) $changed(b) |=> a) s18++;
  assert property (@(posedge clk) $past(a, 2) |-> b) else f19++;
  cover property (@(posedge clk) $past(a, 2) |-> b) s19++;
  assert property (@(posedge clk) c throughout (a ##1 b)) else f20++;
  cover property (@(posedge clk) c throughout (a ##1 b)) s20++;
  assert property (@(posedge clk) first_match(a ##[1:3] b)) else f21++;
  cover property (@(posedge clk) first_match(a ##[1:3] b)) s21++;
  assert property (@(posedge clk) a |-> first_match(b ##[1:2] c)) else f22++;
  cover property (@(posedge clk) a |-> first_match(b ##[1:2] c)) s22++;
  assert property (@(posedge clk) b throughout (a ##[1:2] c)) else f23++;
  cover property (@(posedge clk) b throughout (a ##[1:2] c)) s23++;
  assert property (@(posedge clk) (a ##1 b) and (b ##1 c)) else f24++;
  cover property (@(posedge clk) (a ##1 b) and (b ##1 c)) s24++;
  assert property (@(posedge clk) (a ##[1:2] b) intersect (c[*2:3])) else f25++;
  cover property (@(posedge clk) (a ##[1:2] b) intersect (c[*2:3])) s25++;
  assert property (@(posedge clk) b within (a ##2 c)) else f26++;
  cover property (@(posedge clk) b within (a ##2 c)) s26++;
  assert property (@(posedge clk) a |-> ((b ##1 c) and (c ##1 a))) else f27++;
  cover property (@(posedge clk) a |-> ((b ##1 c) and (c ##1 a))) s27++;
  assert property (@(posedge clk) (a ##1 b) and ((a ##[1:2] b) and ((a ##[1:3] b) and ((a[*1:2] ##1 b) and ((a ##1 b[*1:2]) and ((a ##[1:2] b) and (a ##1 b))))))) else f28++;
  cover property (@(posedge clk) (a ##1 b) and ((a ##[1:2] b) and ((a ##[1:3] b) and ((a[*1:2] ##1 b) and ((a ##1 b[*1:2]) and ((a ##[1:2] b) and (a ##1 b))))))) s28++;
  assert property (@(posedge clk) (((((((a ##1 b) and (a ##[1:2] b)) and (a ##[1:3] b)) and (a[*1:2] ##1 b)) and (a ##1 b[*1:2])) and (a ##[1:2] b)) and (a ##1 b))) else f29++;
  cover property (@(posedge clk) (((((((a ##1 b) and (a ##[1:2] b)) and (a ##[1:3] b)) and (a[*1:2] ##1 b)) and (a ##1 b[*1:2])) and (a ##[1:2] b)) and (a ##1 b))) s29++;
  assert property (@(posedge clk) not (not (a ##1 b))) else f30++;
  cover property (@(posedge clk) not (not (a ##1 b))) s30++;
  assert property (@(posedge clk) (not (a ##1 b)) and (not (b ##1 c))) else f31++;
  cover property (@(posedge clk) (not (a ##1 b)) and (not (b ##1 c))) s31++;
  assert property (@(posedge clk) (not (a ##1 b)) or (not (b ##1 c))) else f32++;
  cover property (@(posedge clk) (not (a ##1 b)) or (not (b ##1 c))) s32++;
  assert property (@(posedge clk) (a ##1 b) implies (b ##1 c)) else f33++;
  cover property (@(posedge clk) (a ##1 b) implies (b ##1 c)) s33++;
  assert property (@(posedge clk) (a ##1 b) iff (b ##1 c)) else f34++;
  cover property (@(posedge clk) (a ##1 b) iff (b ##1 c)) s34++;
  assert property (@(posedge clk) if (a) (b ##1 c) else (c ##1 b)) else f35++;
  cover property (@(posedge clk) if (a) (b ##1 c) else (c ##1 b)) s35++;
  assert property (@(posedge clk) if (a) (b ##1 c)) else f36++;
  cover property (@(posedge clk) if (a) (b ##1 c)) s36++;
  assert property (@(posedge clk) nexttime (a ##1 b)) else f37++;
  cover property (@(posedge clk) nexttime (a ##1 b)) s37++;
  assert property (@(posedge clk) nexttime [2] (a ##1 b)) else f38++;
  cover property (@(posedge clk) nexttime [2] (a ##1 b)) s38++;
  assert property (@(posedge clk) accept_on (d) (a ##1 b)) else f39++;
  cover property (@(posedge clk) accept_on (d) (a ##1 b)) s39++;
  assert property (@(posedge clk) reject_on (d) (a ##1 b)) else f40++;
  cover property (@(posedge clk) reject_on (d) (a ##1 b)) s40++;
  assert property (@(posedge clk) sync_accept_on (d) (a ##1 b)) else f41++;
  cover property (@(posedge clk) sync_accept_on (d) (a ##1 b)) s41++;
  assert property (@(posedge clk) always a) else f42++;
  cover property (@(posedge clk) always a) s42++;
  assert property (@(posedge clk) always (a ##1 b)) else f43++;
  cover property (@(posedge clk) always (a ##1 b)) s43++;
  assert property (@(posedge clk) always [1:3] c) else f44++;
  cover property (@(posedge clk) always [1:3] c) s44++;
  assert property (@(posedge clk) s_always [0:2] (b ##1 c)) else f45++;
  cover property (@(posedge clk) s_always [0:2] (b ##1 c)) s45++;
  assert property (@(posedge clk) eventually [1:3] b) else f46++;
  cover property (@(posedge clk) eventually [1:3] b) s46++;
  assert property (@(posedge clk) s_eventually [1:4] (a ##1 b)) else f47++;
  cover property (@(posedge clk) s_eventually [1:4] (a ##1 b)) s47++;
  assert property (@(posedge clk) s_eventually a) else f48++;
  cover property (@(posedge clk) s_eventually a) s48++;
  assert property (@(posedge clk) a until b) else f49++;
  cover property (@(posedge clk) a until b) s49++;
  assert property (@(posedge clk) a until_with b) else f50++;
  cover property (@(posedge clk) a until_with b) s50++;
  assert property (@(posedge clk) a s_until b) else f51++;
  cover property (@(posedge clk) a s_until b) s51++;
  assert property (@(posedge clk) always [0:4] (a || b)) else f52++;
  cover property (@(posedge clk) always [0:4] (a || b)) s52++;
  assert property (@(posedge clk) s_eventually (b ##1 c)) else f53++;
  cover property (@(posedge clk) s_eventually (b ##1 c)) s53++;
  assert property (@(posedge clk) a |-> not (b ##1 c)) else f54++;
  cover property (@(posedge clk) a |-> not (b ##1 c)) s54++;
  assert property (@(posedge clk) a |=> (b until c)) else f55++;
  cover property (@(posedge clk) a |=> (b until c)) s55++;
  assert property (@(posedge clk) a |-> always [0:2] b) else f56++;
  cover property (@(posedge clk) a |-> always [0:2] b) s56++;
  assert property (@(posedge clk) a |-> s_eventually [1:3] c) else f57++;
  cover property (@(posedge clk) a |-> s_eventually [1:3] c) s57++;
  assert property (@(posedge clk) a |=> nexttime b) else f58++;
  cover property (@(posedge clk) a |=> nexttime b) s58++;
  assert property (@(posedge clk) a #-# not (b ##1 c)) else f59++;
  cover property (@(posedge clk) a #-# not (b ##1 c)) s59++;
  assert property (@(posedge clk) a |-> (if (b) (c ##1 a) else (a ##1 c))) else f60++;
  cover property (@(posedge clk) a |-> (if (b) (c ##1 a) else (a ##1 c))) s60++;
  assert property (@(posedge clk) a |-> (b |-> c)) else f61++;
  cover property (@(posedge clk) a |-> (b |-> c)) s61++;
  assert property (@(posedge clk) (a ##1 b) |=> (c implies a)) else f62++;
  cover property (@(posedge clk) (a ##1 b) |=> (c implies a)) s62++;
  cover sequence (@(posedge clk) a ##1 b[*1:2]) cs++;
  cover sequence (@(posedge clk) first_match(a ##[1:3] b)) cf++;
  initial begin
    #480;
    $display("inl auto p1 f=%0d s=%0d", f1, s1);
    $display("inl auto p2 f=%0d s=%0d", f2, s2);
    $display("inl auto p3 f=%0d s=%0d", f3, s3);
    $display("inl auto p4 f=%0d s=%0d", f4, s4);
    $display("inl auto p5 f=%0d s=%0d", f5, s5);
    $display("inl auto p6 f=%0d s=%0d", f6, s6);
    $display("inl auto p7 f=%0d s=%0d", f7, s7);
    $display("inl auto p8 f=%0d s=%0d", f8, s8);
    $display("inl auto p9 f=%0d s=%0d", f9, s9);
    $display("inl auto p10 f=%0d s=%0d", f10, s10);
    $display("inl auto p11 f=%0d s=%0d", f11, s11);
    $display("inl auto p12 f=%0d s=%0d", f12, s12);
    $display("inl auto p13 f=%0d s=%0d", f13, s13);
    $display("inl auto p14 f=%0d s=%0d", f14, s14);
    $display("inl auto p15 f=%0d s=%0d", f15, s15);
    $display("inl auto p16 f=%0d s=%0d", f16, s16);
    $display("inl auto p17 f=%0d s=%0d", f17, s17);
    $display("inl auto p18 f=%0d s=%0d", f18, s18);
    $display("inl auto p19 f=%0d s=%0d", f19, s19);
    $display("inl auto p20 f=%0d s=%0d", f20, s20);
    $display("inl auto p21 f=%0d s=%0d", f21, s21);
    $display("inl auto p22 f=%0d s=%0d", f22, s22);
    $display("inl auto p23 f=%0d s=%0d", f23, s23);
    $display("inl auto p24 f=%0d s=%0d", f24, s24);
    $display("inl auto p25 f=%0d s=%0d", f25, s25);
    $display("inl auto p26 f=%0d s=%0d", f26, s26);
    $display("inl auto p27 f=%0d s=%0d", f27, s27);
    $display("inl auto p28 f=%0d s=%0d", f28, s28);
    $display("inl auto p29 f=%0d s=%0d", f29, s29);
    $display("inl auto t1 f=%0d s=%0d", f30, s30);
    $display("inl auto t2 f=%0d s=%0d", f31, s31);
    $display("inl auto t3 f=%0d s=%0d", f32, s32);
    $display("inl auto t4 f=%0d s=%0d", f33, s33);
    $display("inl auto t5 f=%0d s=%0d", f34, s34);
    $display("inl auto t6 f=%0d s=%0d", f35, s35);
    $display("inl auto t7 f=%0d s=%0d", f36, s36);
    $display("inl auto t8 f=%0d s=%0d", f37, s37);
    $display("inl auto t9 f=%0d s=%0d", f38, s38);
    $display("inl auto t10 f=%0d s=%0d", f39, s39);
    $display("inl auto t11 f=%0d s=%0d", f40, s40);
    $display("inl auto t12 f=%0d s=%0d", f41, s41);
    $display("inl auto u1 f=%0d s=%0d", f42, s42);
    $display("inl auto u2 f=%0d s=%0d", f43, s43);
    $display("inl auto u3 f=%0d s=%0d", f44, s44);
    $display("inl auto u4 f=%0d s=%0d", f45, s45);
    $display("inl auto u5 f=%0d s=%0d", f46, s46);
    $display("inl auto u6 f=%0d s=%0d", f47, s47);
    $display("inl auto u7 f=%0d s=%0d", f48, s48);
    $display("inl auto u8 f=%0d s=%0d", f49, s49);
    $display("inl auto u9 f=%0d s=%0d", f50, s50);
    $display("inl auto u10 f=%0d s=%0d", f51, s51);
    $display("inl auto u11 f=%0d s=%0d", f52, s52);
    $display("inl auto u12 f=%0d s=%0d", f53, s53);
    $display("inl auto v1 f=%0d s=%0d", f54, s54);
    $display("inl auto v2 f=%0d s=%0d", f55, s55);
    $display("inl auto v3 f=%0d s=%0d", f56, s56);
    $display("inl auto v4 f=%0d s=%0d", f57, s57);
    $display("inl auto v5 f=%0d s=%0d", f58, s58);
    $display("inl auto v6 f=%0d s=%0d", f59, s59);
    $display("inl auto v7 f=%0d s=%0d", f60, s60);
    $display("inl auto v8 f=%0d s=%0d", f61, s61);
    $display("inl auto v9 f=%0d s=%0d", f62, s62);
    $display("inl auto cs=%0d", cs);
    $display("inl auto cf=%0d", cf);
    $finish;
  end
endmodule
