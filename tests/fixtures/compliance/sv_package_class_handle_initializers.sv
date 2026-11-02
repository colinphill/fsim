// fsim: top=top
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 6.21, 8.7: package variables initialized by `new` run their
// constructors in declaration order with the package's other initializers.
package log_pkg;
  int order[$];
  string names[$];
endpackage
package p;
  int base = 5;
  class C;
    int v;
    string n;
    static int made = 0;
    function new(int a, string s);
      v = a + base;
      n = s;
      made++;
      log_pkg::order.push_back(v);
      log_pkg::names.push_back(s);
    endfunction
  endclass
  class B #(int K = 1);
    int k = K * 3;
  endclass
  C first = new(1, "first");
  int mid = first.v * 2;
  C second = new(mid, "second");
  B #(4) b4 = new;
  B b1 = new;
endpackage
module top;
  initial begin
    if (p::first.v == 6 && p::mid == 12 && p::second.v == 17
        && p::C::made == 2 && p::b4.k == 12 && p::b1.k == 3
        && log_pkg::order.size() == 2 && log_pkg::order[1] == 17
        && log_pkg::names[0] == "first")
      $display("PASS");
    else
      $display("FAIL %0d %0d %0d %0d %0d %0d %p", p::first.v, p::mid,
        p::second.v, p::C::made, p::b4.k, p::b1.k, log_pkg::order);
  end
endmodule
