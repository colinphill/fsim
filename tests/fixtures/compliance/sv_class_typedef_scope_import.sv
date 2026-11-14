// fsim: top=top
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 6.18, 8.23, 8.25.1, 26.3: a typedef of a class
// specialization names that specialization, so `T::m()` through it calls the
// static method of the specialization, on its own static properties, as
// `C#(8)::m()` does. The typedef here is a package item made visible by a
// wildcard import and by an explicit import, and is also used by its
// qualified name: each acts on registry#(8) or registry#(2) and leaves the
// default specialization, registry#(4), untouched.
package tsi_p;
  class registry #(int W = 4);
    static int count;
    static logic [W-1:0] last;
    static function void add(int n, logic [W-1:0] tag);
      count += n;
      last = tag;
    endfunction
    static function int width();
      return W;
    endfunction
  endclass
  typedef registry #(8) wide_registry;
  typedef registry #(2) narrow_registry;
endpackage

module top;
  import tsi_p::*;
  import tsi_p::narrow_registry;
  int failures;
  initial begin
    wide_registry::add(5, 8'hA5);
    tsi_p::wide_registry::add(7, 8'h3C);
    if (wide_registry::count != 12 || registry#(8)::count != 12
        || wide_registry::last !== 8'h3C || wide_registry::width() != 8)
      failures++;
    narrow_registry::add(3, 2'b10);
    if (narrow_registry::count != 3 || registry#(2)::last !== 2'b10
        || narrow_registry::width() != 2)
      failures++;
    if (registry#(4)::count != 0 || registry#()::width() != 4)
      failures++;
    if (failures == 0) $display("PASS");
    else $display("FAIL count %0d", failures);
  end
endmodule
