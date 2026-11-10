// SPDX-License-Identifier: Apache-2.0
//
// Two clocking events are the same clock only when they are identical: the
// same edges of the same objects under the same iff conditions (IEEE
// 1800-2023 16.13.1, 16.16.1), however they are spelled. A name in a
// clocking event resolves from the scope it is written in (16.8: a sequence
// or property declaration's own), so the same spelling can name two clocks
// and two spellings one clock.
//
// top.clk rises at 10 j + 5 and falls at 10 j + 10; g.clk and h.clk (each
// block's own clk) rise at 14 k + 7. rows[j] = {a, b, c, en} holds from
// 10 j + 8 to 10 j + 18 (0000 before), so no tick sees a change (16.5.1).
// An evaluation at a point waits for the nearest tick of its clock at or
// after it (`|->`) or strictly after it (`|=>`); en gates the ticks of
// `posedge clk iff en` (9.4.2.3). The counts are shown at t = 301; an
// attempt not decided by then reports nothing. Each s<i> counts successes
// (vacuous ones too, 16.14.1) and f<i> failures, from an independent
// evaluation of these rules over the table:
//
//   i1  in g: @(posedge clk) a |-> pm, pm's clk being top.clk:
//       two clocks, b at top.clk's tick                    fail  2, success 19
//   i2  @(posedge clk) a ##2 @(posedge top.clk) b: one clock fail 27, success 3
//   i3  @(posedge clk) b ##2 sd(clk), sd's event being
//       posedge (x) with x = clk: one clock                fail 28, success 1
//   i4  @(posedge clk) a |-> @(negedge clk) b: two clocks  fail  7, success 23
//   i5  @(posedge clk) a |=> @(posedge clk iff en) b       fail  6, success 24
//   i6  in h: @(posedge clk) a |-> @(posedge top.clk) b    fail  2, success 19
//   i7  @(cb) a ##2 @(posedge clk) b, cb's event being
//       posedge clk: one clock (14.13, 16.16), as i2      fail 27, success 3
//   i8  @(posedge clk) a |=> @(cb) b: one clock            fail  7, success 23
//
// Taken by spelling, pm's `posedge clk` would be g.clk and i1 would count
// fail 1, success 20; i2, i3 and i7 would join two clocks with ##2
// (16.13.1).
module top;
  bit clk;
  logic a, b, c, en;
  always #5 clk = ~clk;
  property pm;
    @(posedge clk) b;
  endproperty
  sequence sd(x);
    @(posedge x) a ##1 c;
  endsequence
  reg [3:0] rows [0:29];
  initial begin
    rows[0] = 4'b1010;
    rows[1] = 4'b0001;
    rows[2] = 4'b0001;
    rows[3] = 4'b1010;
    rows[4] = 4'b1100;
    rows[5] = 4'b0001;
    rows[6] = 4'b0110;
    rows[7] = 4'b0000;
    rows[8] = 4'b1101;
    rows[9] = 4'b0001;
    rows[10] = 4'b1000;
    rows[11] = 4'b1100;
    rows[12] = 4'b1000;
    rows[13] = 4'b1111;
    rows[14] = 4'b1010;
    rows[15] = 4'b1101;
    rows[16] = 4'b0000;
    rows[17] = 4'b0000;
    rows[18] = 4'b1111;
    rows[19] = 4'b1100;
    rows[20] = 4'b0011;
    rows[21] = 4'b0000;
    rows[22] = 4'b0010;
    rows[23] = 4'b0111;
    rows[24] = 4'b0100;
    rows[25] = 4'b0110;
    rows[26] = 4'b0001;
    rows[27] = 4'b0101;
    rows[28] = 4'b0001;
    rows[29] = 4'b0011;
  end
  initial begin
    {a, b, c, en} = 4'b0000;
    #8;
    for (int k = 0; k <= 29; k++) begin
      {a, b, c, en} = rows[k];
      #10;
    end
  end
  int f2, s2, f3, s3, f4, s4, f5, s5, f7, s7, f8, s8;
  clocking cb @(posedge clk);
  endclocking
  assert property (@(cb) a ##2 @(posedge clk) b) s7++; else f7++;
  assert property (@(posedge clk) a |=> @(cb) b) s8++; else f8++;
  assert property (@(posedge clk) a ##2 @(posedge top.clk) b) s2++; else f2++;
  assert property (@(posedge clk) b ##2 sd(clk)) s3++; else f3++;
  assert property (@(posedge clk) a |-> @(negedge clk) b) s4++; else f4++;
  assert property (@(posedge clk) a |=> @(posedge clk iff en) b) s5++; else f5++;
  if (1) begin : g
    bit clk;
    int f, s;
    initial #7 forever begin clk = 1; #7 clk = 0; #7; end
    assert property (@(posedge clk) a |-> pm) s++; else f++;
  end
  if (1) begin : h
    bit clk;
    int f, s;
    initial #7 forever begin clk = 1; #7 clk = 0; #7; end
    assert property (@(posedge clk) a |-> @(posedge top.clk) b) s++; else f++;
  end
  initial begin
    #301;
    $display("inl clk i1 f=%0d s=%0d", g.f, g.s);
    $display("inl clk i2 f=%0d s=%0d", f2, s2);
    $display("inl clk i3 f=%0d s=%0d", f3, s3);
    $display("inl clk i4 f=%0d s=%0d", f4, s4);
    $display("inl clk i5 f=%0d s=%0d", f5, s5);
    $display("inl clk i6 f=%0d s=%0d", h.f, h.s);
    $display("inl clk i7 f=%0d s=%0d", f7, s7);
    $display("inl clk i8 f=%0d s=%0d", f8, s8);
    $finish;
  end
endmodule
