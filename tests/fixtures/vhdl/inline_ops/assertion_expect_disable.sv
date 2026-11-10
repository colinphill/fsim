// SPDX-License-Identifier: Apache-2.0
//
// The disable condition of an expect statement's property (IEEE 1800-2023
// 16.17) is asynchronous and reads current values as an assertion's does
// (16.12): when it is true at any time of the attempt, the attempt is
// disabled at once, neither its pass nor its fail statement runs, and the
// statement completes, so the process continues in that time step.
//
//   The first expect starts its attempt at the tick 15; a stays 1 and b 0,
//   so a ##3 b would fail at 45. rst is 1 from 27 to 28: the attempt is
//   disabled at 27 and the process continues at 27.
//   The second starts at the tick 65; rst rises and falls within the time
//   step 70 (#0), which disables the attempt at 70.
//   The third (a ##1 a) starts at the tick 75 and succeeds at 85.
module top;
  bit clk, a = 1'b1, b, rst;
  always #5 clk = ~clk;

  initial begin
    #12;
    expect (@(posedge clk) disable iff (rst) a ##3 b)
      $display("inl pass1 t=%0t", $time);
    else $display("inl fail1 t=%0t", $time);
    $display("inl after1 t=%0t", $time);
    #33;
    expect (@(posedge clk) disable iff (rst) a ##3 b)
      $display("inl pass2 t=%0t", $time);
    else $display("inl fail2 t=%0t", $time);
    $display("inl after2 t=%0t", $time);
    expect (@(posedge clk) disable iff (rst) a ##1 a)
      $display("inl pass3 t=%0t", $time);
    else $display("inl fail3 t=%0t", $time);
    $display("inl after3 t=%0t", $time);
    #10 $finish;
  end

  initial begin
    #27 rst = 1;
    #1 rst = 0;    // 28
    #42 rst = 1;   // 70
    #0 rst = 0;
  end
endmodule
