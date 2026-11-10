// fsim: top=top
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 7.8, 7.10, 8.9, 8.25.1: each specialization of a
// parameterized class has its own static associative arrays, queues and
// dynamic arrays. post_randomize writes and reads their elements and calls
// their methods in the class method executor, directly through a
// specialization (`store#(4)::m["z"] = 40;`, a typedef of one) and inside
// a static method of the specialization; the module reads the same storage
// in its own process and calls its container methods in statements, and
// the default specialization store#(1) stays empty.
class store #(int P = 1);
  static int m[string];
  static int q[$];
  static int d[];
  static int k[int];
  static function void put(string s);
    m[s] = P;
    q.push_back(P);
    k[P] = P * 10;
    d[1] = d[1] + P;
  endfunction
endclass

typedef store #(7) store7;

class R;
  rand bit [1:0] x;
  int seen_m;
  int seen_q;
  int seen_k;
  function void post_randomize();
    store #(4)::put("a");
    store7::put("b");
    store #(4)::m["z"] = 40;
    store7::q[0] = 70;
    store7::q[1] = 71;
    store7::d[0] = 77;
    store #(4)::q.push_front(3);
    store7::k.delete(7);
    seen_m = store #(4)::m["a"] + store #(4)::m.num();
    seen_q = store7::q[1] + store7::q.size();
    seen_k = store #(4)::k[4] + store7::k.exists(7);
  endfunction
endclass

module top;
  R r;
  int failures;
  initial begin
    r = new;
    store #(4)::d = new[2];
    store7::d = new[2];
    if (!r.randomize()) begin
      $display("FAIL randomize");
      failures++;
    end
    if (store #(4)::m.num() != 2 || store #(4)::m["a"] != 4
        || store #(4)::m["z"] != 40) begin
      $display("FAIL m4 %0d", store #(4)::m.num());
      failures++;
    end
    if (store7::m.num() != 1 || store7::m["b"] != 7) begin
      $display("FAIL m7 %0d", store7::m.num());
      failures++;
    end
    if (store #(4)::q.size() != 2 || store #(4)::q[0] != 3
        || store #(4)::q[1] != 4) begin
      $display("FAIL q4 %0d", store #(4)::q.size());
      failures++;
    end
    if (store7::q.size() != 2 || store7::q[0] != 70 || store7::q[1] != 71) begin
      $display("FAIL q7 %0d", store7::q.size());
      failures++;
    end
    if (store #(4)::d[1] != 4 || store #(4)::d[0] != 0 || store7::d[1] != 7
        || store7::d[0] != 77) begin
      $display("FAIL d %0d %0d %0d", store #(4)::d[1], store7::d[1],
               store7::d[0]);
      failures++;
    end
    if (store #(4)::k.num() != 1 || store #(4)::k[4] != 40
        || store7::k.num() != 0) begin
      $display("FAIL k %0d %0d", store #(4)::k.num(), store7::k.num());
      failures++;
    end
    if (r.seen_m != 6 || r.seen_q != 73 || r.seen_k != 40) begin
      $display("FAIL seen %0d %0d %0d", r.seen_m, r.seen_q, r.seen_k);
      failures++;
    end
    if (store #(1)::m.num() != 0 || store #(1)::q.size() != 0
        || store #(1)::d.size() != 0 || store #(1)::k.num() != 0) begin
      $display("FAIL default");
      failures++;
    end
    // A method statement through the parameter value assignment itself
    // calls the specialization's container method (8.23, 8.25.1).
    store #(4)::q.push_back(5);
    store #(4)::k.delete(4);
    if (store #(4)::q.size() != 3 || store #(4)::q[2] != 5
        || store #(4)::k.num() != 0 || store7::q.size() != 2) begin
      $display("FAIL statement %0d %0d", store #(4)::q.size(),
               store #(4)::k.num());
      failures++;
    end
    if (failures == 0) $display("PASS");
  end
endmodule
