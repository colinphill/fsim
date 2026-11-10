// SPDX-License-Identifier: Apache-2.0
// Local variables of concurrent properties (IEEE 1800-2023 16.10) on the
// concurrent-property monitor. Each property reports its failure times and
// counts its passes (vacuous successes run the pass statement too, 16.14.1).
// The stimulus row k is set at time 10k and sampled at the clock tick
// 10k + 5 (ticks k = 0 .. 11); $finish comes after the last tick, when no
// attempt is still running. The expected values, from the LRM:
//   pipe (16.10 pipeline example): x takes din at the antecedent's match and
//     ##2 later dout must be x + 1; rows 0, 1, 4, 7 are valid, dout is 4, 14
//     and 74 at rows 2, 3, 9 but 99 at row 6: one failure at 65.
//   dis: pipe under disable iff (rst), rst high from 58 to 60, between two
//     ticks: the attempt of row 4 in flight is disabled at once (16.12), so
//     no failure at 65 and no pass for it.
//   cond: if (x > 40) dout == 0 else dout != 0 one tick after the match (a
//     condition that reads a local, 16.12.11): row 0 (x = 3) sees dout 0 at
//     row 1: a failure at 15.
//   neg: not (dout == x + 1) at the end of (valid, x = din) ##2 1 (16.12.3):
//     rows 0, 1, 7 match dout: failures at 25, 35, 95.
//   or: (a, x = din) or (b, x = din + 100) (16.10: each operand's thread
//     has its own x; 16.12.7: every match of the antecedent needs the
//     consequent): row 2 has a and b, dor is 23 at row 3 but not 123: a
//     failure at 35; rows 5 (a) and 8 (b) pass.
//   and: (a ##1 (1, x = din)) and (b, y = din): x flows out of the left
//     operand, y out of the right (16.10, F.5.4); row 2: dand at row 4 is
//     33 + 23 = 56, a pass.
//   goto: (dor == x)[->1] from the match on (16.9.2): rows 2 and 5 pass.
//   acc: (start, s = din) ##1 (1, s += din)[*2] |=> total == s (16.10
//     accumulation): start at row 3 sums 33 + 43 + 53 = 129, total 129 at
//     row 6; start at row 7 sums 249, total 0 at row 10: a failure at 105.
//   wide: a 100-bit local (16.10, any type of 16.6): wrong at row 2 only:
//     a failure at 25.
//   init: a sequence instance's local with an initialization assignment,
//     c = v + 1 when the instance begins (16.10): as pipe, a failure at 65.
//   lf: a local input formal argument, v initialized with the actual's
//     value when the instance begins (16.8.2): as pipe, a failure at 65.
//   act: a local passed as an untyped actual and assigned in the instance
//     (16.10: the actual is the entire local): as pipe, a failure at 65.
//   samp: cnt changes by a blocking assignment at every tick; the local
//     takes cnt's sampled value (16.5.1, 16.10), so cnt == x + 1 at the next
//     tick always holds.
//   past: $past(din) one tick after the match equals the local that took
//     din's sampled value there; the function takes the assertion's clock
//     (16.9.3) though the monitor evaluates it only for a thread: no
//     failure.
//   call: subroutine calls attached to the sequences (16.11) run at each
//     end point in list order, after the tick in the Reactive region, with
//     the thread's locals and sampled by-value arguments: cnt reads 3, not
//     the 4 the tick's blocking assignment wrote, at 35. start is set at
//     rows 3 and 7.
//   abort, rej: pipe under accept_on (rst) and reject_on (rst), conditions
//     checked with sampled values at every time step of an attempt
//     (16.12.14): rst is sampled 1 at 60 only, so the attempt of row 4 in
//     flight is accepted (a vacuous success, its pass statement runs) or
//     rejected at 60; the monitor's operands carry the locals and the abort
//     state together.
//   sdis: pipe under disable iff ($sampled(rst)): a disable condition with a
//     sampled value function changes at the start of a time step (16.9.3,
//     16.12), so the attempt of row 4 is disabled at 60, as dis.
//   pdis: past under disable iff ($rose(rst, @(posedge clk))), an explicitly
//     clocked sampled value function in the disable condition (16.9.3): it
//     is true at 60 only (rst's sample there against the tick at 55), when
//     no attempt of past is in flight, so pdis counts as past; and the
//     $past of both assertions keeps reading din's history.
module top;
  bit clk;
  always #5 clk = ~clk;
  bit valid, a, b, start, en, rst;
  int din, dout, dor, dand, total, cnt;
  bit [99:0] wide;
  bit vd[12] = '{1, 1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0};
  bit av[12] = '{0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0};
  bit bv[12] = '{0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0};
  bit sv[12] = '{0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0};
  int dov[12] = '{0, 0, 4, 14, 0, 0, 99, 0, 0, 74, 0, 0};
  int dorv[12] = '{0, 0, 0, 23, 0, 0, 53, 0, 0, 183, 0, 0};
  int dandv[12] = '{0, 0, 0, 0, 56, 0, 0, 0, 0, 0, 0, 0};
  int totalv[12] = '{0, 0, 0, 0, 0, 0, 129, 0, 0, 0, 0, 0};
  int n_pipe, n_dis, n_cond, n_neg, n_or, n_and, n_goto, n_acc, n_wide,
      n_init, n_lf, n_act, n_samp, n_past, n_call, n_abort, n_rej, n_sdis, n_pdis;
  string f_pipe, f_dis, f_cond, f_neg, f_or, f_and, f_goto, f_acc, f_wide,
      f_init, f_lf, f_act, f_samp, f_past, f_call, calls, f_abort, f_rej, f_sdis, f_pdis;

  task automatic note(input string tag, input int value);
    calls = $sformatf("%s%s%0d@%0t,", calls, tag, value, $time);
  endtask

  always @(posedge clk) cnt = cnt + 1;

  initial begin
    for (int k = 0; k < 12; k++) begin
      valid = vd[k];
      a = av[k];
      b = bv[k];
      start = sv[k];
      en = k <= 9;
      din = 10 * k + 3;
      dout = dov[k];
      dor = dorv[k];
      dand = dandv[k];
      total = totalv[k];
      case (k)
        1: wide = {4'd3, 32'd3, 32'd3, 32'd3};
        5: wide = {4'hb, 32'd43, 32'd43, 32'd43};
        8: wide = {4'h9, 32'd73, 32'd73, 32'd73};
        default: wide = '0;
      endcase
      #10;
    end
  end
  initial begin
    #58 rst = 1'b1;
    #2 rst = 1'b0;
  end

  property p_pipe;
    int x;
    @(posedge clk) (valid, x = din) |-> ##2 dout == x + 1;
  endproperty
  property p_cond;
    int x;
    @(posedge clk) (valid, x = din) |=> if (x > 40) dout == 0 else dout != 0;
  endproperty
  property p_neg;
    int x;
    @(posedge clk) (valid, x = din) ##2 1 |-> not (dout == x + 1);
  endproperty
  property p_or;
    int x;
    @(posedge clk) ((a, x = din) or (b, x = din + 100)) |=> dor == x;
  endproperty
  property p_and;
    int x, y;
    @(posedge clk) ((a ##1 (1, x = din)) and (b, y = din)) |=> dand == x + y;
  endproperty
  property p_goto;
    int x;
    @(posedge clk) (a, x = din) |-> (dor == x)[->1];
  endproperty
  property p_acc;
    int s;
    @(posedge clk) (start, s = din) ##1 (1, s += din)[*2] |=> total == s;
  endproperty
  property p_wide;
    bit [99:0] w;
    @(posedge clk) (valid, w = {din[3:0], din, din, din}) |=> wide == w;
  endproperty
  sequence s_init(v);
    int c = v + 1;
    ##2 dout == c;
  endsequence
  sequence s_lf(local input int v);
    ##2 dout == v + 1;
  endsequence
  sequence s_set(lv, e);
    (1, lv = e);
  endsequence
  property p_act;
    int x;
    @(posedge clk) (valid ##0 s_set(x, din)) |-> ##2 dout == x + 1;
  endproperty
  property p_samp;
    int x;
    @(posedge clk) (en, x = cnt) |=> cnt == x + 1;
  endproperty
  property p_past;
    int x;
    @(posedge clk) (valid, x = din) |=> $past(din) == x;
  endproperty
  property p_call;
    int x;
    @(posedge clk) (start, x = din, note("v", x), note("c", cnt))
      ##1 (1, x += 1, note("w", x)) |-> 1;
  endproperty

  pipe: assert property (p_pipe) n_pipe++;
    else f_pipe = $sformatf("%s%0t,", f_pipe, $time);
  dis: assert property (@(posedge clk) disable iff (rst) p_pipe) n_dis++;
    else f_dis = $sformatf("%s%0t,", f_dis, $time);
  cond: assert property (p_cond) n_cond++;
    else f_cond = $sformatf("%s%0t,", f_cond, $time);
  neg: assert property (p_neg) n_neg++;
    else f_neg = $sformatf("%s%0t,", f_neg, $time);
  orp: assert property (p_or) n_or++;
    else f_or = $sformatf("%s%0t,", f_or, $time);
  andp: assert property (p_and) n_and++;
    else f_and = $sformatf("%s%0t,", f_and, $time);
  gotop: assert property (p_goto) n_goto++;
    else f_goto = $sformatf("%s%0t,", f_goto, $time);
  acc: assert property (p_acc) n_acc++;
    else f_acc = $sformatf("%s%0t,", f_acc, $time);
  widep: assert property (p_wide) n_wide++;
    else f_wide = $sformatf("%s%0t,", f_wide, $time);
  init: assert property (@(posedge clk) valid |-> s_init(din)) n_init++;
    else f_init = $sformatf("%s%0t,", f_init, $time);
  lf: assert property (@(posedge clk) valid |-> s_lf(din)) n_lf++;
    else f_lf = $sformatf("%s%0t,", f_lf, $time);
  act: assert property (p_act) n_act++;
    else f_act = $sformatf("%s%0t,", f_act, $time);
  samp: assert property (p_samp) n_samp++;
    else f_samp = $sformatf("%s%0t,", f_samp, $time);
  past: assert property (p_past) n_past++;
    else f_past = $sformatf("%s%0t,", f_past, $time);
  call: assert property (p_call) n_call++;
    else f_call = $sformatf("%s%0t,", f_call, $time);
  abort: assert property (@(posedge clk) accept_on (rst) p_pipe) n_abort++;
    else f_abort = $sformatf("%s%0t,", f_abort, $time);
  rej: assert property (@(posedge clk) reject_on (rst) p_pipe) n_rej++;
    else f_rej = $sformatf("%s%0t,", f_rej, $time);
  sdis: assert property (@(posedge clk) disable iff ($sampled(rst)) p_pipe) n_sdis++;
    else f_sdis = $sformatf("%s%0t,", f_sdis, $time);
  pdis: assert property (@(posedge clk) disable iff ($rose(rst, @(posedge clk))) p_past)
    n_pdis++;
    else f_pdis = $sformatf("%s%0t,", f_pdis, $time);

  initial begin
    #122;
    $display("inl pipe pass=%0d fail=%s", n_pipe, f_pipe);
    $display("inl dis pass=%0d fail=%s", n_dis, f_dis);
    $display("inl cond pass=%0d fail=%s", n_cond, f_cond);
    $display("inl neg pass=%0d fail=%s", n_neg, f_neg);
    $display("inl or pass=%0d fail=%s", n_or, f_or);
    $display("inl and pass=%0d fail=%s", n_and, f_and);
    $display("inl goto pass=%0d fail=%s", n_goto, f_goto);
    $display("inl acc pass=%0d fail=%s", n_acc, f_acc);
    $display("inl wide pass=%0d fail=%s", n_wide, f_wide);
    $display("inl init pass=%0d fail=%s", n_init, f_init);
    $display("inl lf pass=%0d fail=%s", n_lf, f_lf);
    $display("inl act pass=%0d fail=%s", n_act, f_act);
    $display("inl samp pass=%0d fail=%s", n_samp, f_samp);
    $display("inl past pass=%0d fail=%s", n_past, f_past);
    $display("inl call pass=%0d fail=%s calls=%s", n_call, f_call, calls);
    $display("inl abort pass=%0d fail=%s", n_abort, f_abort);
    $display("inl rej pass=%0d fail=%s", n_rej, f_rej);
    $display("inl sdis pass=%0d fail=%s", n_sdis, f_sdis);
    $display("inl pdis pass=%0d fail=%s", n_pdis, f_pdis);
    $finish;
  end
endmodule
