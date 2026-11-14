// SPDX-License-Identifier: Apache-2.0
// A class handle named through a hierarchical path (IEEE 1800-2023 23.6,
// 23.8): a `$root.` path, an element of an instance array in the path, a
// variable declared in a generate scope of the instance, and an indexed
// element of an array or queue of handles, read, called through and
// assigned.
class C #(int W = 4);
  bit [W-1:0] v;
  function new(int x); v = x; endfunction
  function int get(); return v; endfunction
endclass

module leaf #(int BASE = 0);
  C #(8) m;
  C #(8) q[$];
  if (BASE >= 0) begin : g
    C #(8) gm;
    initial gm = new(BASE + 40);
  end
  initial begin
    C #(8) t;
    m = new(BASE + 90);
    t = new(BASE + 91);
    q.push_back(t);
    t = new(BASE + 92);
    q.push_back(t);
  end
endmodule

module mid;
  leaf #(0) a[2]();
  leaf #(100) b();
endmodule

module top;
  mid u();
  C #(8) h;
  C #(8) k;
  int r1, r2, r3, r4, r5;
  initial begin
    #1;
    h = $root.top.u.b.q[1];
    r1 = $root.top.u.b.q[0].get();
    r2 = u.a[1].m.get();
    r3 = u.b.g.gm.get();
    r4 = $root.top.u.b.g.gm.get();
    k = u.a[0].g.gm;
    r5 = k.get();
    $display("h=%0d r1=%0d r2=%0d r3=%0d r4=%0d r5=%0d", h.get(), r1, r2, r3,
        r4, r5);
  end
endmodule
