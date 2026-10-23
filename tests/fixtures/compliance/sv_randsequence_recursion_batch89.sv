// fsim: top=top std=2017
// SPDX-License-Identifier: Apache-2.0
// Recursive randsequence productions (IEEE 1800-2017 18.17): direct and
// mutual recursion ended by `break` or by an `if` item, a `repeat` of a
// recursive production, and inline productions called between them.
module top;
  int depth, trail, count;
  bit ok = 1;

  initial begin
    depth = 0;
    trail = 0;
    randsequence(main)
      main : step step down down;
      down : { depth++; if (depth >= 3) break; } step down;
      step : { trail = trail * 10 + 1; };
    endsequence
    if (depth != 3 || trail != 1111) begin
      $display("FAIL direct %0d %0d", depth, trail);
      ok = 0;
    end

    count = 0;
    trail = 0;
    randsequence(ping)
      ping : { count++; trail = trail * 10 + 1; } if (count < 4) pong;
      pong : { trail = trail * 10 + 2; } ping;
    endsequence
    if (count != 4 || trail != 1212121) begin
      $display("FAIL mutual %0d %0d", count, trail);
      ok = 0;
    end

    count = 0;
    randsequence(top_rule)
      top_rule : repeat (2) tree;
      tree : { count++; } if (count < 5) tree;
    endsequence
    if (count != 6) begin
      $display("FAIL repeat %0d", count);
      ok = 0;
    end

    if (ok) $display("PASS");
  end
endmodule
