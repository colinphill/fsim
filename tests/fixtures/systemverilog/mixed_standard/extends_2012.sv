// SPDX-License-Identifier: Apache-2.0
// Compiled with --standard 2012. Derivation (1800-2023 8.13, 8.15): the
// derived get() overrides the virtual method and returns the inherited
// v + 100 = 7 + 100 = 107.
import p09::*;
class derived extends base;
  function new(int a);
    super.new(a);
  endfunction
  virtual function int get();
    return v + 100;
  endfunction
endclass
module tb_extends;
  derived d;
  initial begin
    d = new(7);
    $display("MIX extends %0d", d.get());
  end
endmodule
