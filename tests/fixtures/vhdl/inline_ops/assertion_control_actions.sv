// SPDX-License-Identifier: Apache-2.0
//
// The assertion controls (IEEE 1800-2023 20.11) of immediate and deferred
// assertions, and Kill against Off of a concurrent attempt in progress.
//
// t = 0, simple immediate assertions: the first passes (p = 1); after
// $assertpassoff its pass statement does not run; after $assertfailoff
// neither does the fail statement; after $assertoff the assertion is not
// checked at all (q = 0). On, PassOn and FailOn restore every assertion.
//
// t = 1..3, observed deferred assertions (type 4): an action runs when the
// controls allowed it as the report was queued (an assertion already
// executing is not affected); an Off leaves a queued report to mature and a
// Kill flushes it (16.4):
//   t = 1  pass queued: dp = 1; fail queued, then FailOff: it matures,
//          df = 1; a fail while FailOff: no statement
//   t = 2  FailOn; fail queued, then Kill: flushed; a fail while killed: not
//          checked
//   t = 3  On; pass queued, then Off: it matures, dp = 2; a pass while Off:
//          not checked
// and final deferred ones (type 8) at t = 3: a fail while FailOff of type 8
// shows nothing, after FailOn it shows "inl final on".
//
// ka and kb start one attempt each at the tick at 5 (start is 1 from 4 to
// 6), which matches at the tick at 35. At 20 ka is killed (the attempt is
// aborted: kn = 0) and kb is turned off (the attempt goes on: ko = 1); both
// are on again from 40, when start no longer holds.
module top;
  bit clk;
  always #5 clk = ~clk;
  logic one = 1'b1, zero = 1'b0, start = 1'b0;
  int p, f, q, dp, df, kn, ko;
  task tdp();
    dp++;
  endtask
  task tdf();
    df++;
  endtask
  ka: cover property (@(posedge clk) start ##3 one) kn++;
  kb: cover property (@(posedge clk) start ##3 one) ko++;
  initial begin
    assert (one) p++; else f++;
    $assertpassoff;
    assert (one) p++; else f++;
    $assertfailoff;
    assert (zero) p++; else f++;
    $assertoff;
    assert (zero) q++; else q++;
    $asserton;
    $assertpasson;
    $assertfailon;
    #1;
    assert #0 (one) tdp(); else tdf();
    assert #0 (zero) tdp(); else tdf();
    $assertcontrol(9, 4);
    assert #0 (zero) tdp(); else tdf();
    #1 $assertcontrol(8, 4);
    assert #0 (zero) tdp(); else tdf();
    $assertcontrol(5, 4);
    assert #0 (zero) tdp(); else tdf();
    #1 $assertcontrol(3, 4);
    assert #0 (one) tdp(); else tdf();
    $assertcontrol(4, 4);
    assert #0 (one) tdp(); else tdf();
    $assertcontrol(3, 4);
    $assertcontrol(9, 8);
    assert final (zero) else $display("inl final off");
    $assertcontrol(8, 8);
    assert final (zero) else $display("inl final on");
  end
  initial begin
    #4 start = 1'b1;
    #2 start = 1'b0;
    #14 $assertkill(0, ka);
    $assertoff(0, kb);
    #20 $asserton;
    #11 $display("inl actions p=%0d f=%0d q=%0d dp=%0d df=%0d kn=%0d ko=%0d",
      p, f, q, dp, df, kn, ko);
    $finish;
  end
endmodule
