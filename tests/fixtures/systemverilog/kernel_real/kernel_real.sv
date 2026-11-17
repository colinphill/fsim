// SPDX-License-Identifier: Apache-2.0
//
// real, shortreal, realtime and time variables (IEEE 1800-2023 6.12, 6.11)
// held by the v4 kernel: blocking writes, continuous assignments through
// ports, value-change events, conversions and comparisons, in two instances
// of a cell. A dump of the whole design records every change, a time
// variable's x before its first assignment included (21.7).
module kernel_real_cell #(parameter real SCALE = 1.5) (input real a, output real b,
    input shortreal sa, output shortreal sb, output time changed_at);
  assign b = a * SCALE;
  assign sb = sa - 0.25;
  always @(a) changed_at = $time;
endmodule

module kernel_real;
  real x = 1.0, y, acc;
  real p_out, q_out;
  real invalid_quotient, invalid_difference, invalid_product;
  shortreal s = 0.5, p_short, q_short, invalid_short;
  realtime rt;
  time t, p_time, q_time;
  int events;
  logic [63:0] bits;
  kernel_real_cell #(.SCALE(1.5)) p (.a(x), .b(p_out), .sa(s), .sb(p_short), .changed_at(p_time));
  kernel_real_cell #(.SCALE(-0.5)) q (.a(acc), .b(q_out), .sa(s), .sb(q_short), .changed_at(q_time));

  // Each event prints only its own variable: what a continuous assignment
  // or another process computes from it in the same time step is not yet
  // ordered (4.7). The main loop prints those values a step later.
  always @(acc) begin
    events++;
    $display("%0t acc=%f events=%0d", $time, acc, events);
  end
  always @(p_out) $display("%0t p=%f", $time, p_out);

  initial begin
    $dumpfile("kernel_real.vcd");
    $dumpvars(0, kernel_real);
    for (int i = 0; i < 12; i++) begin
      #1;
      x = x * 1.25 - i;
      y = x / 3.0;
      acc = acc + (y > 0.0 ? y : -y);
      s = s * 2.0;
      if (s > 100.0) s = 0.125;
      rt = $realtime;
      t = $time + 7;
      bits = $realtobits(acc);
      $display("%0t x=%f y=%f s=%f rt=%f t=%0t rtoi=%0d bits=%h back=%f",
          $time, x, y, s, rt, t, $rtoi(y), bits, $bitstoreal(bits));
      #1 $display("%0t p=%f/%f/%0t q=%f/%f/%0t", $time, p_out, p_short, p_time, q_out,
          q_short, q_time);
    end
    // Zero and its negative, an infinity, and an unchanged write.
    #2 acc = 0.0;
    #2 acc = -0.0;
    #2 acc = 1.0e308 * 10.0;
    #2 acc = acc;
    #2 $display("%f %f %f %f %0t", acc, p_out, q_out, p_short, t);
    $finish;
  end

  // Invalid operations of constants and of variables give one NaN, the one
  // the reference's hardware gives (LLVM folds constants to a NaN of its
  // own sign). Time 25 is a step in which nothing else prints.
  initial #25 begin
    invalid_quotient = 0.0 / 0.0;
    invalid_difference = acc - acc;
    invalid_product = 0.0 * (acc + 1.0e308 * 10.0);
    invalid_short = shortreal'(0.0) / shortreal'(0.0);
    $display("nan %h %h %h %h", $realtobits(invalid_quotient),
        $realtobits(invalid_difference), $realtobits(invalid_product),
        $shortrealtobits(invalid_short));
  end
endmodule
