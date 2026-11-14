// SPDX-License-Identifier: Apache-2.0
//
// Recursive properties (IEEE 1800-2023 16.12.17, F.7) as generated code: a
// property instance that reaches its own declaration again is a back edge
// of the plan, and each tick steps the instances through the record's node.
// An instance whose outcome is exactly one operand's gives way to it (an
// `and` whose other side held, an implication with one consequent left, a
// chosen if/else branch, a started weak nexttime), so an attempt keeps a
// short chain of records however long it runs.
//
// clk rises at 5, 15, ..., 395 (40 ticks); a stays 1, c falls at 200, d
// rises at 100:
//   prop_always(a): never fails;
//   prop_always(c): every attempt fails, at 205 or at its own start (40);
//   prop_weak_until(c, d): every attempt passes, at 105 or at its start (40);
//   fibonacci(0, 1, 5, fib) from the attempt at 5 (local formals rebound
//     at each recursion): fib is 0, 1, 1, 2, 3 at 5..45 (no failure);
//   check_phase1/check_phase2 alternate and check_phase1 needs c: every
//     attempt reaches check_phase1 at 205 or later (40 failures);
//   p_if(c) checks c at every tick through a chosen branch and a nexttime:
//     every attempt fails at 205 or at its start (40).
module top;
  logic clk = 0, a = 1, c = 1, d = 0, start = 1;
  int fib = 0, fib_next = 1;
  int n_always, n_always_c, n_until_pass, n_until_fail, n_fib, n_phase, n_if;
  always #5 clk = ~clk;
  initial #200 c = 0;
  initial #100 d = 1;
  initial #10 start = 0;
  always @(posedge clk) begin
    fib <= fib_next;
    fib_next <= fib + fib_next;
  end
  property prop_always(p);
    p and (1'b1 |=> prop_always(p));
  endproperty
  property prop_weak_until(p, q);
    q or (p and (1'b1 |=> prop_weak_until(p, q)));
  endproperty
  property fibonacci(local input int x, y, n, int fib_sig);
    (n > 0) |-> ((fib_sig == x) and (1'b1 |=> fibonacci(y, x + y, n - 1, fib_sig)));
  endproperty
  property check_phase1;
    a |-> (c and (1'b1 |=> check_phase2));
  endproperty
  property check_phase2;
    a |-> (1'b1 and (1'b1 |=> check_phase1));
  endproperty
  property p_if(p);
    if (p) nexttime p_if(p) else 1'b0;
  endproperty
  a_always: assert property (@(posedge clk) prop_always(a)) else n_always++;
  a_always_c: assert property (@(posedge clk) prop_always(c)) else n_always_c++;
  a_until: assert property (@(posedge clk) prop_weak_until(c, d))
    n_until_pass++;
  else
    n_until_fail++;
  a_fib: assert property (@(posedge clk) start |-> fibonacci(0, 1, 5, fib))
  else
    n_fib++;
  a_phase: assert property (@(posedge clk) check_phase1) else n_phase++;
  a_if: assert property (@(posedge clk) p_if(c)) else n_if++;
  initial begin
    #400;
    $display("inl always=%0d always_c=%0d until=%0d/%0d fib=%0d phase=%0d if=%0d",
      n_always, n_always_c, n_until_pass, n_until_fail, n_fib, n_phase, n_if);
    $finish;
  end
endmodule
