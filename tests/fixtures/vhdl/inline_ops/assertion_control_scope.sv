// SPDX-License-Identifier: Apache-2.0
//
// The assertion control tasks select assertions (IEEE 1800-2023 20.11) by
// assertion type (Table 20-6: 1 concurrent, 2 simple immediate, 4 observed
// deferred, 8 final deferred, 16 expect), directive type (Table 20-7: 1
// assert, 2 cover, 4 assume; not checked for expect statements), levels (as
// $dumpvars, 21.7.1.2: 0 every level, n the scope's instance and n - 1
// levels of instances below) and a list of scopes or assertions, searched
// upward from the calling scope (23.8). $asserton/$assertoff/$assertkill
// are $assertcontrol(3/4/5, 15, 7, levels, list) and the action tasks
// $assertcontrol(6..11, 31, 7, levels, list). Lock (1) keeps later controls
// from an assertion until Unlock (2). On, Off and Kill do not affect expect
// statements. Off leaves a queued deferred report to mature; Kill flushes it
// (16.4). The controls apply to concurrent assertions on the monitor and on
// the earlier evaluator (b's assertions have local variables), to immediate
// and deferred assertions and covers, and to expect statements, on both
// engines.
//
// clk rises at 10 j + 5 (j = 0..25 by t = 261); the controls change at
// multiples of 20, between ticks, so each phase has two ticks. Every
// assertion below passes at every tick and its pass statement counts; f.f1
// fails while win is set (80 <= t < 120) and shows the tick. Before each
// phase:
//
//   0    (none)
//   20   $assertoff(0, u1)                     u1 and below off
//   40   $asserton; $assertcontrol(4, 1, 7, 1, u2)
//                                              u2.ma only (u2.l is level 2)
//   60   $asserton; $assertcontrol(4, 2, 7, 0, u3.blk.ia1);
//        $assertcontrol(4, 15, 7, 0, u1.l.pa)  two assertions by name
//   80   $asserton; $assertcontrol(4, 4)       every observed deferred
//   100  $asserton; $assertcontrol(4, 15, 2); $assertcontrol(4, 8, 1, 0, f)
//                                              every cover; f.f1
//   120  $asserton; Lock u3; $assertoff; Unlock u3
//                                              all but u3 off
//   140  $assertcontrol(3, 15, 7, 0, u1)       u1 and below on again
//   160  $asserton; $assertpassoff(0, u2.l); $assertcontrol(7, 2, 7, 0, u3);
//        $assertcontrol(7, 1, 2, 0, b)         pass statements off: u2.l's,
//                                              u3's simple immediate ones,
//                                              b's concurrent cover
//   180  $assertpasson; $assertcontrol(4, 15, 1)
//                                              every assert directive off
//   200  $asserton; at 205 after q.d1 queued its report: $assertkill(0, q)
//                                              the 205 report is flushed
//   220  $asserton(0, q); at 225 after the report: $assertoff(0, q)
//                                              the 225 report matures
//   240  $asserton
//
// So, per assertion, the ticks its pass statement runs at (of 26), from an
// independent evaluation of these rules:
//
//   u1.ma 20; u1.l: pa 18, pc 20, ia1 20, ic1 20, da1 18
//   u2.ma 18; u2.l: pa 18, pc 18, ia1 18, ic1 18, da1 16
//   u3: pa 24, pc 24, ia1 20, ic1 22, da1 22
//   q.d1 15; b: fa 20, fc 18; the expect statement 26
//   f.f1 fails at 85 and 95 (Off of type 4 leaves type 8 on)
module leaf(input bit clk);
  bit one = 1'b1;
  int ca, cc, ia, ic, da;
  task bump_da();
    da++;
  endtask
  pa: assert property (@(posedge clk) one) ca++;
  pc: cover property (@(posedge clk) one) cc++;
  always @(posedge clk) begin : blk
    ia1: assert (one) ia++;
    ic1: cover (one) ic++;
    da1: assert #0 (one) bump_da();
  end
endmodule

module mid(input bit clk);
  bit one = 1'b1;
  int ca;
  ma: assert property (@(posedge clk) one) ca++;
  leaf l(clk);
endmodule

module dq(input bit clk);
  bit one = 1'b1;
  int n;
  task bump();
    n++;
  endtask
  always @(posedge clk) d1: assert #0 (one) bump();
endmodule

module fq(input bit clk, input bit win);
  always @(posedge clk) f1: assert final (!win) else $display("inl final t=%0t", $time);
endmodule

module fbm(input bit clk);
  int n, m;
  property p_loc;
    int x;
    @(posedge clk) (1, x = 1) |-> x == 1;
  endproperty
  fa: assert property (p_loc) n++;
  fc: cover property (p_loc) m++;
endmodule

module top;
  bit clk, win, one = 1'b1;
  int ne;
  always #5 clk = ~clk;
  mid u1(clk);
  mid u2(clk);
  leaf u3(clk);
  dq q(clk);
  fq f(clk, win);
  fbm b(clk);
  initial forever expect (@(posedge clk) one) ne++;
  initial begin
    #80 win = 1'b1;
    #40 win = 1'b0;
  end
  initial begin
    #20 $assertoff(0, u1);
    #20 $asserton;
    $assertcontrol(4, 1, 7, 1, u2);
    #20 $asserton;
    $assertcontrol(4, 2, 7, 0, u3.blk.ia1);
    $assertcontrol(4, 15, 7, 0, u1.l.pa);
    #20 $asserton;
    $assertcontrol(4, 4);
    #20 $asserton;
    $assertcontrol(4, 15, 2);
    $assertcontrol(4, 8, 1, 0, f);
    #20 $asserton;
    $assertcontrol(1, 15, 7, 0, u3);
    $assertoff;
    $assertcontrol(2, 15, 7, 0, u3);
    #20 $assertcontrol(3, 15, 7, 0, u1);
    #20 $asserton;
    $assertpassoff(0, u2.l);
    $assertcontrol(7, 2, 7, 0, u3);
    $assertcontrol(7, 1, 2, 0, b);
    #20 $assertpasson;
    $assertcontrol(4, 15, 1);
    #20 $asserton;
    @(posedge clk);
    #0 $assertkill(0, q);
    #15 $asserton(0, q);
    @(posedge clk);
    #0 $assertoff(0, q);
    #15 $asserton;
    #21;
    $display("inl scope u1.ma=%0d u2.ma=%0d", u1.ca, u2.ca);
    $display("inl scope u1.l pa=%0d pc=%0d ia1=%0d ic1=%0d da1=%0d",
      u1.l.ca, u1.l.cc, u1.l.ia, u1.l.ic, u1.l.da);
    $display("inl scope u2.l pa=%0d pc=%0d ia1=%0d ic1=%0d da1=%0d",
      u2.l.ca, u2.l.cc, u2.l.ia, u2.l.ic, u2.l.da);
    $display("inl scope u3 pa=%0d pc=%0d ia1=%0d ic1=%0d da1=%0d",
      u3.ca, u3.cc, u3.ia, u3.ic, u3.da);
    $display("inl scope q.d1=%0d b.fa=%0d b.fc=%0d expect=%0d",
      q.n, b.n, b.m, ne);
    $finish;
  end
endmodule
