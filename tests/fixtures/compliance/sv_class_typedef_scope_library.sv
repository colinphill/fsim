// fsim: top=top before=sv_class_typedef_scope_library_pkg.sv.in
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 8.23, 8.25.1, 13.4.1: a class scope resolution names the
// static members of a specialization, `C#(8)::m()`, and a void function
// called as a statement runs for its effect. Here the class lives in a
// package compiled on its own before this unit imports it: a void function
// called as a statement, a function called in an expression and static
// property reads act on registry#(8) and leave registry#(4) untouched; a
// typedef of the class without parameter values (6.18) names its default
// specialization, registry#(4), and a typedef of a specialization, with a
// value or a type actual, names that specialization; an explicit type actual
// equal to a type parameter's default names the defaulted specialization.
module top;
  import tsl_p::*;
  int failures;
  initial begin
    item a = new(5);
    item b = new(7);
    registry#(8)::add(a, 8'hA5);
    registry#(8)::add(b, 8'h3C);
    if (registry#(8)::count != 12 || registry#(8)::last !== 8'h3C)
      failures++;
    if (registry#(8)::width() != 8 || registry#()::width() != 4)
      failures++;
    if (registry#(4)::count != 0 || registry#(4)::last !== 4'hx)
      failures++;
    plain_registry::add(b, 4'h9);
    if (registry#(4)::count != 7 || plain_registry::count != 7
        || plain_registry::last !== 4'h9 || plain_registry::width() != 4
        || registry#(8)::count != 12)
      failures++;
    wide_registry::add(a, 8'h77);
    if (wide_registry::count != 17 || registry#(8)::count != 17
        || wide_registry::last !== 8'h77 || wide_registry::width() != 8
        || registry#(4)::count != 7)
      failures++;
    narrow_registry::add(a, 2'b01);
    if (narrow_registry::count != 5 || registry#(2)::last !== 2'b01
        || narrow_registry::width() != 2 || registry#(8)::count != 17)
      failures++;
    // The explicit type actual names the specialization the default does
    // (6.22.1, 8.25).
    registry#(2, item)::add(b, 2'b10);
    if (registry#(2)::count != 12 || narrow_registry::last !== 2'b10
        || registry#(2, item)::count != 12)
      failures++;
    if (failures == 0) $display("PASS");
    else $display("FAIL count %0d", failures);
  end
endmodule
