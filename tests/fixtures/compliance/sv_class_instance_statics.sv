// fsim: top=top
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 6.22, 8.9, 8.25 and 27.4: a class declared in a module,
// interface or program is a distinct type in each instance, and one declared
// in a loop generate in each generated scope. Each has static properties of
// its own, initialized with that instance's parameters or that scope's
// genvars; its methods read them; and a virtual call through a package base
// class runs the override of the object's own instance.
package results_pkg;
  virtual class base;
    pure virtual function int get();
  endclass
  base all[$];
  int passed = 0;
  function automatic int sum();
    int s = 0;
    foreach (all[i]) s += all[i].get();
    return s;
  endfunction
endpackage

module unit_m #(parameter int W = 8) ();
  class counter extends results_pkg::base;
    static int scaled = W * 2;
    static int made = 0;
    int k;
    function new(); k = W; made++; endfunction
    virtual function int get(); return W * 1000 + made; endfunction
    static function int peek(); return scaled + made; endfunction
  endclass
  class sized #(int K = 1);
    static int start = W * 100 + K;
    function int get(); return W * 10 + K; endfunction
  endclass
  initial begin
    counter c;
    sized #(3) s;
    c = new;
    s = new;
    results_pkg::all.push_back(c);
    #1;
    if (counter::scaled == W * 2 && counter::made == 1
        && counter::peek() == W * 2 + 1 && c.k == W
        && sized#(3)::start == W * 100 + 3 && s.get() == W * 10 + 3)
      results_pkg::passed++;
    else
      $display("FAIL %m %0d %0d %0d %0d %0d", counter::scaled, counter::made,
        counter::peek(), sized#(3)::start, s.get());
  end
endmodule

module gen_m;
  bit ok[1:3];
  results_pkg::base items[1:3];
  for (genvar i = 1; i <= 3; i++) begin : g
    class item extends results_pkg::base;
      static int step = i * 2;
      static int made = 0;
      int k;
      function new(); k = i * 10; made++; endfunction
      virtual function int get(); return step + k; endfunction
    endclass
    initial begin
      item h;
      h = new;
      items[i] = h;
      #1;
      ok[i] = item::step == i * 2 && item::made == 1 && h.get() == i * 12;
      if (!ok[i])
        $display("FAIL %m %0d %0d %0d", item::step, item::made, h.get());
    end
  end
  initial begin
    #2;
    foreach (ok[i])
      if (ok[i]) begin
        results_pkg::passed++;
        results_pkg::all.push_back(items[i]);
      end
  end
endmodule

interface bus_if #(parameter int D = 2) ();
  class tag;
    static int depth = D * 3;
  endclass
  initial begin
    #1;
    if (tag::depth == D * 3)
      results_pkg::passed++;
    else
      $display("FAIL %m %0d", tag::depth);
  end
endinterface

module top;
  unit_m #(.W(8)) a ();
  unit_m #(.W(16)) b ();
  unit_m #(.W(16)) c ();
  gen_m g ();
  bus_if #(.D(2)) i2 ();
  bus_if #(.D(5)) i5 ();
  initial begin
    #5;
    if (results_pkg::passed == 8 && results_pkg::sum() == 40075)
      $display("PASS");
    else
      $display("FAIL passed=%0d sum=%0d", results_pkg::passed,
        results_pkg::sum());
  end
endmodule
