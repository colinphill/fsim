// fsim: top=sv_sva_sequence_endpoints
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 9.4.2.4, 16.13.5, 16.13.6, F.6.1: the end point of a
// sequence read through triggered and matched, waited on with wait and @.
//   - s.triggered is true in the time step in which s matches;
//   - s.matched at a destination clock tick is true when s matched at an
//     earlier time step with no destination tick in between (F.6.1);
//   - an unclocked sequence takes the clock of the context it is read in;
//   - a sequence instance as the actual of another (`s2(s1(a))`);
//   - a generate block's sequence that hides the module's `s` has its own
//     end point (23.9);
//   - a procedural event control on a sequence instance, with an event
//     expression or an expression as its actual (`@(se(posedge clk))`,
//     `@(s1c(a))`), and `wait (s1c(a).triggered)`; a function call in an
//     event control stays an expression (`@(f(a, clk))`).
// clk rises at 5, 15, ..., 95; clk2 at 7, 21, 35, 49, 63, 77, 91. Every
// operand is 1, so s = @clk a ##1 b matches at 15, 25, ..., 95:
//   s.triggered at clk: 15..95 (9);
//   s.matched at clk2: 21, 35, 49, 63, 77, 91 (6);
//   s.matched at clk: the match of the previous tick, 25..95 (8);
//   s2(s1(a)) = a ##1 b ##1 c: 25..95 (8);
//   unclocked u = a ##1 b read at clk: 15..95 (9);
//   the generate block's s = @clk a: 5..95 (10), and its procedures'
//     wait (s.triggered) and @s, which see the block's s: 5;
//   wait (s.triggered) resumes at 15, @s at 15 and then 25;
//   @(se(posedge clk)) at 15 and then 25, @(s1c(a)) at 15,
//   wait (s1c(a).triggered) at 15, @(f(a, clk)) when a & clk rises: 5.
module sv_sva_sequence_endpoints;
  logic clk = 0, clk2 = 0, a = 1, b = 1, c = 1;
  always #5 clk = ~clk;
  always #7 clk2 = ~clk2;
  int n_triggered, n_matched, n_matched_same, n_nested, n_unclocked, n_gen;
  time t_wait, t_at, t_at_next;
  sequence s;
    @(posedge clk) a ##1 b;
  endsequence
  sequence u;
    a ##1 b;
  endsequence
  sequence s1(x);
    x ##1 b;
  endsequence
  sequence s2(y);
    y ##1 c;
  endsequence
  sequence se(event ev);
    @(ev) a ##1 b;
  endsequence
  sequence s1c(x);
    @(posedge clk) x ##1 b;
  endsequence
  function automatic logic f(logic x, logic y);
    return x & y;
  endfunction
  time t_se, t_se_next, t_s1c, t_wait_inst, t_f, t_gen_wait, t_gen_at;
  cover property (@(posedge clk) s.triggered) n_triggered++;
  cover property (@(posedge clk2) s.matched) n_matched++;
  cover property (@(posedge clk) s.matched) n_matched_same++;
  cover property (@(posedge clk) s2(s1(a)).triggered) n_nested++;
  cover property (@(posedge clk) u.triggered) n_unclocked++;
  generate
    if (1) begin : g
      sequence s;
        @(posedge clk) a;
      endsequence
      cover property (@(posedge clk) s.triggered) n_gen++;
      initial begin
        wait (s.triggered);
        t_gen_wait = $time;
      end
      initial begin
        @s;
        t_gen_at = $time;
      end
    end
  endgenerate
  initial begin
    wait (s.triggered);
    t_wait = $time;
  end
  initial begin
    @s;
    t_at = $time;
    @s;
    t_at_next = $time;
  end
  initial begin
    @(se(posedge clk));
    t_se = $time;
    @(se(posedge clk));
    t_se_next = $time;
  end
  initial begin
    @(s1c(a));
    t_s1c = $time;
  end
  initial begin
    wait (s1c(a).triggered);
    t_wait_inst = $time;
  end
  initial begin
    @(f(a, clk));
    t_f = $time;
  end
  initial begin
    #100;
    if (n_triggered == 9 && n_matched == 6 && n_matched_same == 8
        && n_nested == 8 && n_unclocked == 9 && n_gen == 10 && t_wait == 15
        && t_at == 15 && t_at_next == 25 && t_se == 15 && t_se_next == 25
        && t_s1c == 15 && t_wait_inst == 15 && t_f == 5 && t_gen_wait == 5
        && t_gen_at == 5)
      $display("PASS");
    else
      $display("FAIL triggered %0d matched %0d matched_same %0d nested %0d unclocked %0d gen %0d wait %0t at %0t next %0t se %0t %0t s1c %0t wait_inst %0t f %0t gen_wait %0t gen_at %0t",
        n_triggered, n_matched, n_matched_same, n_nested, n_unclocked, n_gen,
        t_wait, t_at, t_at_next, t_se, t_se_next, t_s1c, t_wait_inst, t_f,
        t_gen_wait, t_gen_at);
    $finish;
  end
endmodule
