// fsim: top=sv_sequence_properties std=2017
// IEEE 1800-2017 16.7-16.12: sequence properties with ranged delays,
// consecutive and goto repetition, `or`, `not`, named sequences and property
// instances with actuals, `|->` and `|=>`, with overlapping attempts; 16.14:
// pass and fail action blocks. xsim 2025.2 does not simulate most of these
// forms; the expected counts follow from the stimulus table, where the
// posedge at 10k + 5 samples row k - 1.
module sv_sequence_properties;
  bit clk = 0;
  always #5 clk = ~clk;
  logic a = 0, b = 0, c = 0;
  int cyc = 0;
  int f1 = 0, f2 = 0, f3 = 0, f4 = 0, f5 = 0, f6 = 0, c1 = 0, c2 = 0;
  logic [2:0] rows [0:15] = '{3'b100, 3'b010, 3'b001, 3'b100, 3'b000,
    3'b000, 3'b110, 3'b011, 3'b001, 3'b100, 3'b010, 3'b010, 3'b010,
    3'b001, 3'b000, 3'b000};
  always @(negedge clk) begin
    {a, b, c} = rows[cyc];
    cyc <= cyc + 1;
  end

  sequence s_bc;
    b ##1 c;
  endsequence

  property p_next(x, y);
    x |=> y;
  endproperty

  property p_clocked(x, y);
    @(posedge clk) x |-> ##1 y;
  endproperty

  assert property (@(posedge clk) a |-> ##[1:2] b) else f1++;
  assert property (@(posedge clk) a |=> s_bc) else f2++;
  assert property (@(posedge clk) a |-> b[->1] ##1 c) else f3++;
  assert property (@(posedge clk) not (b ##1 b ##1 b)) else f4++;
  assert property (@(posedge clk) p_next(b, c || b)) else f5++;
  assert property (p_clocked(a, b)) else f6++;
  cover property (@(posedge clk) a ##1 b[*2]) c1++;
  cover property (@(posedge clk) (a ##1 b) or (b ##1 c)) c2++;

  int failures = 0;
  task check(input int got, input int expected, input string what);
    if (got !== expected) begin
      $display("FAIL %s: got %0d expected %0d", what, got, expected);
      failures++;
    end
  endtask

  initial begin
    #160;
    check(f1, 1, "a |-> ##[1:2] b failures");
    check(f2, 2, "a |=> s_bc failures");
    check(f3, 1, "a |-> b[->1] ##1 c failures");
    check(f4, 1, "not (b ##1 b ##1 b) failures");
    check(f5, 0, "property instance failures");
    check(f6, 1, "clocked property instance failures");
    check(c1, 1, "a ##1 b[*2] matches");
    check(c2, 6, "(a ##1 b) or (b ##1 c) matches");
    if (failures == 0) $display("PASS");
    $finish;
  end
endmodule
