// fsim: top=top
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 8.25.1, 8.10: a static task called as a statement through
// a specialization (`counter#(4)::tick(2);`, a typedef of one, or
// `counter#(P)::tick(1);` whose actual is the calling class's parameter)
// runs in that specialization, on its static properties and parameter
// values; a static void function called as a statement in a class method
// does too. R::run makes the calls in a class method; the module makes the
// same calls in its own process. counter#(1), the default specialization,
// is never called and keeps its statics at zero.
class counter #(int P = 1);
  static int n;
  static int t;
  static function void add();
    n = n + P;
  endfunction
  static task tick(int times);
    repeat (times) t = t + P;
  endtask
endclass

typedef counter #(7) counter7;

class relay #(int P = 1);
  static task forward();
    counter #(P)::tick(1);
  endtask
endclass

class R;
  task run();
    counter #(4)::tick(1);
    counter7::tick(1);
    counter #(4)::add();
    counter7::add();
    relay #(7)::forward();
  endtask
endclass

module top;
  R r;
  int failures;
  initial begin
    r = new;
    r.run();
    counter #(4)::tick(2);
    counter7::tick(1);
    relay #(4)::forward();
    counter #(4)::add();
    // tick: counter#(4) 1 + 2 + 1 = 4 calls of +4; counter7 1 + 1 + 1
    // = 3 calls of +7. add: one call each from R::run, one more on
    // counter#(4) from the module.
    if (counter #(4)::t != 16 || counter7::t != 21 || counter #(1)::t != 0) begin
      $display("FAIL tick %0d %0d %0d", counter #(4)::t, counter7::t,
               counter #(1)::t);
      failures++;
    end
    if (counter #(4)::n != 8 || counter7::n != 7 || counter #(1)::n != 0) begin
      $display("FAIL add %0d %0d %0d", counter #(4)::n, counter7::n,
               counter #(1)::n);
      failures++;
    end
    if (failures == 0) $display("PASS");
  end
endmodule
