// SPDX-License-Identifier: Apache-2.0
// Library libb: the design, and its own interface SBus (parameters W and D).
// A virtual interface type written in libb designates libb's SBus and the
// instances of it only, never the instance `m.s` of the same-named interface of
// library liba that the configuration brings in with `m` (IEEE 1800-2023
// 3.13, 25.9, 33).
interface SBus #(parameter int W = 12, parameter int D = 2 * W);
  logic [W-1:0] data;
endinterface

module top;
  SBus s ();
  virtual SBus v;
  virtual SBus #(3) v3;
  mid m ();
  initial begin
    v = s;
    v.data = 12'habc;
    #2 $display("#c top data=%h bits=%0d", v.data, $bits(v.data));
    #1 $finish;
  end
endmodule

config cfg;
  design libb.top;
  instance top.m liblist liba;
endconfig
