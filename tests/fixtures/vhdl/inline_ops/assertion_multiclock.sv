// SPDX-License-Identifier: Apache-2.0
//
// Multiclocked sequences as generated code (IEEE 1800-2023 16.13.1; task
// #186): `##1 @(c)` continues at the first tick of c strictly after the end of
// the left operand, `##0 @(c)` at or after it. Clocks: c1 every 10 from 5, c2
// every 30 from 25 (always together with c1), c3 every 40 from 12 (never
// together), c4 every 20 from 15. rows[k] is applied at t = 10 (k + 1); a, b
// and c are its bits (the initial value is 000), so a posedge samples the
// value of the last multiple of 10 before it (16.5.1). The counts are derived
// by an independent evaluation of the sampled semantics: an attempt starts
// at each c1 tick, fails at the first boolean that is false and succeeds
// at the end; an attempt that has not been decided when the run ends
// (t = 480) reports nothing (16.12).
//
//   m1  @(posedge c1) a ##1 @(posedge c2) b                            fail 42, success  5
//   m2  @(posedge c1) a ##0 @(posedge c2) b                            fail 43, success  5
//   m3  @(posedge c1) a ##1 @(posedge c3) b                            fail 40, success  7
//   m4  @(posedge c1) a ##1 @(posedge c2) b ##1 @(posedge c3) c        fail 46, success  1
//   m5  @(posedge c1) a[*2] ##1 @(posedge c2) b                        fail 44, success  3
//   m6  @(posedge c1) a ##0 @(posedge c4) b                            fail 36, success 12
module top;
  logic a, b, c;
  bit c1, c2, c3, c4;
  int cs;
  always #5 c1 = ~c1;
  initial begin
    #25;
    forever begin
      c2 = 1; #15; c2 = 0; #15;
    end
  end
  initial begin
    #12;
    forever begin
      c3 = 1; #20; c3 = 0; #20;
    end
  end
  initial begin
    #15;
    forever begin
      c4 = 1; #10; c4 = 0; #10;
    end
  end
  reg [2:0] rows [0:48];
  initial begin
    rows[0] = 3'd1;
    rows[1] = 3'd0;
    rows[2] = 3'd3;
    rows[3] = 3'd2;
    rows[4] = 3'd4;
    rows[5] = 3'd1;
    rows[6] = 3'd3;
    rows[7] = 3'd2;
    rows[8] = 3'd1;
    rows[9] = 3'd0;
    rows[10] = 3'd6;
    rows[11] = 3'd3;
    rows[12] = 3'd7;
    rows[13] = 3'd0;
    rows[14] = 3'd7;
    rows[15] = 3'd7;
    rows[16] = 3'd1;
    rows[17] = 3'd3;
    rows[18] = 3'd1;
    rows[19] = 3'd2;
    rows[20] = 3'd7;
    rows[21] = 3'd6;
    rows[22] = 3'd1;
    rows[23] = 3'd2;
    rows[24] = 3'd5;
    rows[25] = 3'd7;
    rows[26] = 3'd6;
    rows[27] = 3'd7;
    rows[28] = 3'd2;
    rows[29] = 3'd6;
    rows[30] = 3'd7;
    rows[31] = 3'd1;
    rows[32] = 3'd2;
    rows[33] = 3'd3;
    rows[34] = 3'd3;
    rows[35] = 3'd1;
    rows[36] = 3'd4;
    rows[37] = 3'd4;
    rows[38] = 3'd3;
    rows[39] = 3'd6;
    rows[40] = 3'd5;
    rows[41] = 3'd6;
    rows[42] = 3'd7;
    rows[43] = 3'd0;
    rows[44] = 3'd1;
    rows[45] = 3'd1;
    rows[46] = 3'd5;
    rows[47] = 3'd2;
    rows[48] = 3'd1;
  end
  initial begin
    {a, b, c} = 3'd0;
    for (int k = 0; k <= 48; k++) begin
      #10;
      {a, b, c} = rows[k];
    end
  end
  int f1, s1, f2, s2, f3, s3, f4, s4, f5, s5, f6, s6;
  assert property (@(posedge c1) a ##1 @(posedge c2) b) else f1++;
  cover property (@(posedge c1) a ##1 @(posedge c2) b) s1++;
  assert property (@(posedge c1) a ##0 @(posedge c2) b) else f2++;
  cover property (@(posedge c1) a ##0 @(posedge c2) b) s2++;
  assert property (@(posedge c1) a ##1 @(posedge c3) b) else f3++;
  cover property (@(posedge c1) a ##1 @(posedge c3) b) s3++;
  assert property (@(posedge c1) a ##1 @(posedge c2) b ##1 @(posedge c3) c) else f4++;
  cover property (@(posedge c1) a ##1 @(posedge c2) b ##1 @(posedge c3) c) s4++;
  assert property (@(posedge c1) a[*2] ##1 @(posedge c2) b) else f5++;
  cover property (@(posedge c1) a[*2] ##1 @(posedge c2) b) s5++;
  assert property (@(posedge c1) a ##0 @(posedge c4) b) else f6++;
  cover property (@(posedge c1) a ##0 @(posedge c4) b) s6++;
  initial begin
    #480;
    $display("inl mclk m1 f=%0d s=%0d", f1, s1);
    $display("inl mclk m2 f=%0d s=%0d", f2, s2);
    $display("inl mclk m3 f=%0d s=%0d", f3, s3);
    $display("inl mclk m4 f=%0d s=%0d", f4, s4);
    $display("inl mclk m5 f=%0d s=%0d", f5, s5);
    $display("inl mclk m6 f=%0d s=%0d", f6, s6);
    $finish;
  end
endmodule
