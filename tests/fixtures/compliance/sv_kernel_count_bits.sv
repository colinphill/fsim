// fsim: top=sv_kernel_count_bits std=2017
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 20.9: $countones and $countbits compiled into the kernel,
// on narrow, 64-bit, wide and very wide (word-loop) values, with every
// control-bit combination (0, 1, X, Z, repeated controls), in a
// combinational always block and in an initial block across a suspension.
module sv_kernel_count_bits;
  int failures;
  logic [7:0] n;
  logic [63:0] m;
  logic [99:0] w;
  logic [1023:0] v;
  bit [3:0] b;
  int comb;

  task automatic check(input string what, input int got, input int want);
    if (got != want) begin
      $display("FAIL %s: got %0d want %0d", what, got, want);
      failures++;
    end
  endtask

  always @(n) comb = $countones(n) + $countbits(n, 1'bx, 1'bz);

  initial begin
    n = 8'b1x0z_1101;
    #1;
    check("comb", comb, 6);
    check("ones", $countones(n), 4);
    check("zeros", $countbits(n, 0), 2);
    check("x", $countbits(n, 'x), 1);
    check("z", $countbits(n, 'z), 1);
    check("all", $countbits(n, 0, 1, 'x, 'z), 8);
    check("repeated", $countbits(n, 1, 1), 4);
    m = {32'hffff_ffff, 28'h0, 4'bxzx1};
    check("64 ones", $countones(m), 33);
    check("64 x", $countbits(m, 'x), 2);
    check("64 zeros and z", $countbits(m, 0, 'z), 29);
    w = {4'bxzzz, 96'h0};
    w[0] = 1;
    w[64] = 1;
    w[65] = 1'bx;
    check("wide ones", $countones(w), 2);
    check("wide x", $countbits(w, 'x), 2);
    check("wide z", $countbits(w, 'z), 3);
    check("wide zeros", $countbits(w, 0), 93);
    v = '1;
    v[700] = 1'bz;
    v[3] = 1'bx;
    v[1023] = 0;
    check("very wide ones", $countones(v), 1021);
    check("very wide unknown", $countbits(v, 'x, 'z), 2);
    check("very wide zeros", $countbits(v, 0), 1);
    b = 4'b1011;
    check("two-state", $countones(b), 3);
    check("two-state zeros", $countbits(b, 0), 1);
    check("two-state x", $countbits(b, 'x), 0);
    #1;
    check("after a wait", $countones(w ^ 100'h3), 2);
    if (failures == 0) $display("PASS");
    $finish;
  end
endmodule
