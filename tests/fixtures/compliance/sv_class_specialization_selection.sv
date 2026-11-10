// fsim: top=top
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 8.25, 8.25.1, 6.22.1: a class selection names one
// specialization however it is written. `C#()::m` is the default
// specialization, so `C#()` and `C#(1)` share static storage (8.25.1). A
// built-in type actual and a typedef of the same specialization name one
// type, and so one set of statics: `pair#(4, byte)` and `typedef pair#(4,
// byte) p4b` (6.22.1, 8.25), each type keeping storage of its own. The
// module selects them through the lowered code and post_randomize through
// the class method executor.
class counter #(int P = 1);
  static int n;
endclass

class pair #(int P = 1, type T = int);
  static int n;
endclass

typedef pair #(4, int) p4i;
typedef pair #(4, byte) p4b;
typedef pair #(4, shortint) p4s;
typedef pair #(4, logic) p4l;
typedef pair #(4, bit) p4t;

class R;
  rand bit [3:0] x;
  int calls;
  function void post_randomize();
    calls++;
    if (calls != 1)
      return;
    counter #()::n = counter #()::n + 100;
    pair #(4, int)::n = pair #(4, int)::n + 100;
    pair #(4, byte)::n = pair #(4, byte)::n + 100;
    pair #(4, shortint)::n = pair #(4, shortint)::n + 100;
    pair #(4, logic)::n = pair #(4, logic)::n + 100;
    pair #(4, bit)::n = pair #(4, bit)::n + 100;
  endfunction
endclass

module top;
  R r;
  int ok, failures;
  initial begin
    counter #()::n = 5;
    p4i::n = 1;
    pair #(4, int)::n += 10;
    p4b::n = 2;
    pair #(4, byte)::n += 20;
    p4s::n = 3;
    pair #(4, shortint)::n += 30;
    p4l::n = 4;
    pair #(4, logic)::n += 40;
    p4t::n = 5;
    pair #(4, bit)::n += 50;
    r = new;
    ok = r.randomize();
    if (ok != 1) begin
      $display("FAIL randomize %0d", ok);
      failures++;
    end
    if (counter #(1)::n != 105 || counter #()::n != 105
        || counter #(2)::n != 0) begin
      $display("FAIL default %0d %0d", counter #(1)::n, counter #(2)::n);
      failures++;
    end
    // pair#(4) takes T's default, int: the specialization of pair#(4, int).
    if (p4i::n != 111 || pair #(4, int)::n != 111 || pair #(4)::n != 111) begin
      $display("FAIL int %0d %0d", p4i::n, pair #(4, int)::n);
      failures++;
    end
    if (p4b::n != 122 || pair #(4, byte)::n != 122) begin
      $display("FAIL byte %0d %0d", p4b::n, pair #(4, byte)::n);
      failures++;
    end
    if (p4s::n != 133 || pair #(4, shortint)::n != 133) begin
      $display("FAIL shortint %0d %0d", p4s::n, pair #(4, shortint)::n);
      failures++;
    end
    if (p4l::n != 144 || pair #(4, logic)::n != 144) begin
      $display("FAIL logic %0d %0d", p4l::n, pair #(4, logic)::n);
      failures++;
    end
    if (p4t::n != 155 || pair #(4, bit)::n != 155) begin
      $display("FAIL bit %0d %0d", p4t::n, pair #(4, bit)::n);
      failures++;
    end
    if (pair #(4, longint)::n != 0 || pair #(5, int)::n != 0) begin
      $display("FAIL separate %0d %0d", pair #(4, longint)::n,
        pair #(5, int)::n);
      failures++;
    end
    if (failures == 0)
      $display("PASS");
  end
endmodule
