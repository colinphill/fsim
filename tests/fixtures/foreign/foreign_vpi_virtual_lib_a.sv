// SPDX-License-Identifier: Apache-2.0
// Library liba: an interface SBus with another parameter list than the one of
// library libb, and a module whose virtual interface type designates it
// (IEEE 1800-2023 3.13, 25.9, 33).
interface SBus #(parameter int W = 4);
  logic [W-1:0] data;
endinterface

module mid;
  SBus s ();
  virtual SBus v;
  initial begin
    v = s;
    v.data = 4'h5;
    #1 $display("#c mid data=%h bits=%0d", v.data, $bits(v.data));
  end
endmodule
