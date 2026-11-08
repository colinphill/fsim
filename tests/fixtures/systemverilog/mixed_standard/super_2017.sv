// SPDX-License-Identifier: Apache-2.0
// Compiled with --standard 2017 after the package of package_class_2009.sv.
// The derived class lives in the compilation unit and its base in another
// compiled package. Derivation (1800-2023 8.15): super.get() calls the base
// method itself and is not a virtual call, so it returns v = 7; the derived
// get() adds 100, giving 107. A virtual call would recurse without end.
import p09::*;
class derived_super extends base;
  function new(int a);
    super.new(a);
  endfunction
  virtual function int get();
    return super.get() + 100;
  endfunction
endclass
module tb_super;
  derived_super d;
  initial begin
    d = new(7);
    $display("MIX super %0d", d.get());
  end
endmodule
