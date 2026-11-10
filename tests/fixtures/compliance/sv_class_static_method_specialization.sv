// fsim: top=top
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 8.25.1: each specialization of a parameterized class has
// static properties and static methods of its own. A static method called
// through a specialization (`counter#(4)::bump()`, a typedef of one, or
// `counter#(P)::bump()` whose actual is the calling class's parameter) runs
// in that specialization: its static properties, its parameter values and
// its unqualified static calls are that specialization's, so two
// specializations keep separate statics. post_randomize runs the calls in
// the class method executor; the module makes the same calls through the
// compiled lowering, and both see one set of statics per specialization.
class counter #(int P = 1);
  static int n;
  static int total;
  static string tag;
  static function int step();
    n = n + P;
    return n;
  endfunction
  static function int bump();
    total = total + step();
    return total;
  endfunction
  static function int mark();
    if (P == 4)
      tag = "four";
    else
      tag = "seven";
    return P;
  endfunction
endclass

typedef counter #(7) counter7;

class registry #(int P = 1);
  static function int add();
    return counter #(P)::bump();
  endfunction
endclass

class R;
  rand bit [3:0] x;
  int seen4, seen7, seenr, n7, marked;
  string tag7;
  function void post_randomize();
    seen4 = counter #(4)::bump();
    seen7 = counter7::bump();
    seenr = registry #(7)::add();
    marked = counter7::mark() + counter #(4)::mark();
    n7 = counter7::n;
    tag7 = counter7::tag;
    counter #(4)::total = counter #(4)::total + 1;
  endfunction
endclass

module top;
  R r;
  int ok1, ok2, v, failures;
  initial begin
    r = new;
    ok1 = r.randomize();
    ok2 = r.randomize();
    v = counter #(4)::bump();
    if (ok1 != 1 || ok2 != 1) begin
      $display("FAIL randomize %0d %0d", ok1, ok2);
      failures++;
    end
    // counter#(4): n 4, 8, 12; total 4+1, 5+8+1, 14+12.
    if (r.seen4 != 13 || counter #(4)::n != 12 || counter #(4)::total != 26
        || v != 26) begin
      $display("FAIL counter4 %0d %0d %0d %0d", r.seen4, counter #(4)::n,
        counter #(4)::total, v);
      failures++;
    end
    // counter#(7) through the typedef and through registry#(7):
    // n 7, 14, 21, 28; total 7, 21, 42, 70.
    if (r.seen7 != 42 || r.seenr != 70 || r.n7 != 28
        || counter7::n != 28 || counter #(7)::total != 70) begin
      $display("FAIL counter7 %0d %0d %0d %0d %0d", r.seen7, r.seenr, r.n7,
        counter7::n, counter #(7)::total);
      failures++;
    end
    if (r.marked != 11 || r.tag7 != "seven" || counter #(4)::tag != "four"
        || counter7::tag != "seven") begin
      $display("FAIL tag %0d %s %s %s", r.marked, r.tag7, counter #(4)::tag,
        counter7::tag);
      failures++;
    end
    // The default specialization (P = 1) was never called.
    if (counter #(1)::n != 0 || counter #(1)::total != 0
        || counter #(1)::tag != "") begin
      $display("FAIL default %0d %0d", counter #(1)::n, counter #(1)::total);
      failures++;
    end
    if (failures == 0)
      $display("PASS");
  end
endmodule
