// SPDX-License-Identifier: Apache-2.0
//
// SystemVerilog behavioral threads under FSIM_KERNEL_VERIFY: the first, third
// and fourth processes only wait (delays, an event control) and make
// nonblocking assignments (IEEE 1800-2023 9.4, 10.4.2), so the kernel replays
// each of their compiled activations on the reference evaluator and compares
// the queued writes and registers; the second prints. Expected results are
// derived by hand from the scheduling semantics (4.4, 4.5):
//   #5: a <= 3 (a = 3 at 5); #10: a <= a + 1 reads 3 (a = 4), b <= 9; the
//   event control wakes after that update and b <= b + 1 reads 9 (b = 10);
//   n counts every 10 time units: 1 at 10, 2 at 20, 3 at 30.
module top;
  logic [7:0] a = 8'd0;
  logic [7:0] b = 8'd0;
  int n = 0;

  initial begin
    #5 a <= 8'd3;
    #5 a <= a + 8'd1;
    b <= 8'd9;
    @(a);
    b <= b + 8'd1;
  end

  initial begin
    #12 $display("t12 a=%0d b=%0d n=%0d", a, b, n);
    #20 $display("t32 a=%0d b=%0d n=%0d", a, b, n);
    $finish;
  end

  always begin
    #10 n <= n + 1;
  end
endmodule
