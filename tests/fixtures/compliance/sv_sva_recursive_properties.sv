// fsim: top=sv_sva_recursive_properties
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 16.12.17, F.7: recursive and mutually recursive
// properties, with formal arguments passed through, a local variable formal
// argument that each recursive instance binds anew, and the abort operators
// inside a recursive property.
// clk rises at 5, 15, ..., 95, so each assertion has 10 attempts. c falls
// at 50 and d rises at 30:
//   prop_always(a): never fails;
//   prop_always(c): every attempt fails at 55 or at its own start (10);
//   prop_weak_until(c, d): every attempt passes, at 35 or at its start (10);
//   fibonacci(0, 1, 5, fib) from the attempt at 5 checks fib at 5..45 (no
//     failure); against a signal that differs at 45 it fails once;
//   check_phase1/check_phase2 alternate, and check_phase1 needs c: every
//     attempt reaches check_phase1 at or after 55 (10 failures);
//   p3/p4 hold while b stays 0: no attempt is aborted (no failure).
module sv_sva_recursive_properties;
  logic clk = 0, a = 1, b = 0, c = 1, d = 0, abort = 0, start = 1;
  int fib = 0, fib_next = 1;
  int n_always, n_always_c, n_until_pass, n_until_fail, n_fib, n_fib_bad;
  int n_phase, n_abort;
  always #5 clk = ~clk;
  initial #50 c = 0;
  initial #30 d = 1;
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
  property p3(p, bit bb, ab);
    p and (1'b1 |=> p4(p, bb, ab));
  endproperty
  property p4(p, bit bb, ab);
    accept_on(bb) reject_on(ab) p3(p, bb, ab);
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
  a_fib_bad: assert property (@(posedge clk)
      start |-> fibonacci(0, 1, 7, fib + (fib == 3 ? 1 : 0)))
  else
    n_fib_bad++;
  a_phase: assert property (@(posedge clk) check_phase1) else n_phase++;
  a_abort: assert property (@(posedge clk) p3(a, b, abort)) else n_abort++;
  initial begin
    #100;
    if (n_always == 0 && n_always_c == 10 && n_until_pass == 10
        && n_until_fail == 0 && n_fib == 0 && n_fib_bad == 1 && n_phase == 10
        && n_abort == 0)
      $display("PASS");
    else
      $display("FAIL always %0d always_c %0d until %0d/%0d fib %0d fib_bad %0d phase %0d abort %0d",
        n_always, n_always_c, n_until_pass, n_until_fail, n_fib, n_fib_bad,
        n_phase, n_abort);
    $finish;
  end
endmodule
