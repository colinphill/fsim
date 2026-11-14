// SPDX-License-Identifier: Apache-2.0
// A class handle, and an element of an array or queue of handles, declared
// in another design element instance is read through its hierarchical name
// (IEEE 1800-2023 23.6, 7.4, 8.4): the handle assigns to a local one, and a
// method of the object it denotes is called through the path.
class C #(int W = 4);
  bit [W-1:0] v;
  function new(int x); v = x; endfunction
  function int get(); return v; endfunction
endclass

interface ifc;
  C #(8) m;
endinterface

module sub #(int BASE = 0);
  C #(8) m;
  C #(8) arr[2];
  C #(8) que[$];
  C #(8) spare;
  ifc i();
  initial begin
    m = new(BASE + 7);
    arr[0] = new(BASE + 1);
    arr[1] = new(BASE + 5);
    spare = new(BASE + 20);
    que.push_back(spare);
    i.m = new(BASE + 11);
  end
endmodule

module top;
  sub #(0) s1();
  sub #(100) s2();
  C #(8) e;
  C #(8) f;
  C #(8) g;
  int r;
  initial begin
    #1;
    e = s2.m;
    f = s2.arr[1];
    g = s1.que[0];
    r = s1.m.get() + s1.arr[1].get() + s2.i.m.get() + s2.que[0].get();
    $display("e=%0d f=%0d g=%0d r=%0d", e.get(), f.get(), g.get(), r);
    s1.arr[0] = e;
    $display("%0d %0d", s1.arr[0].get(), s2.arr[0].get());
    if (s1.m != s2.m && s1.arr[0] == s2.m) $display("handles");
    // Allocating into another instance's handle makes an object of the
    // class that handle is typed with (8.25).
    s1.arr[1] = new(200);
    s1.m = new(150);
    $display("new %0d %0d", s1.arr[1].get(), s1.m.get());
  end
endmodule
