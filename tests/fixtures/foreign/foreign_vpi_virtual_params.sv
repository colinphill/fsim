// SPDX-License-Identifier: Apache-2.0
// The parameter assigns of an interface typespec (IEEE 1800-2023 37.30,
// 37.32 detail 3): one per parameter of the interface in declaration order,
// whose right side is the actual the type gives (evaluated where the type is
// written: a unit's local parameter, a package constant, a class parameter)
// or else the default the parameter was declared with, evaluated with the
// actuals applied.
interface SBus #(parameter int W = 8, parameter int D = 2 * W);
  logic [W-1:0] data;
endinterface

package pkg;
  localparam int PW = 24;
  typedef virtual SBus#(PW) SBP;
  virtual SBus#(PW) pv;
  class Box #(int CW = 10);
    virtual SBus#(CW) bc;
    virtual SBus#(PW) bq;
  endclass
  class Sub extends Box #(7);
  endclass
endpackage

module top;
  localparam int LW = 6;
  virtual SBus vd;
  virtual SBus #(LW) vl;
  virtual SBus #(pkg::PW) vq;
  virtual SBus #(.D(5)) vn;
  virtual SBus #(LW + 1) ve;
  pkg::Box #(5) b5;
  pkg::Box b;
  pkg::Box #() b0;
  pkg::Sub s7;
  initial begin
    b5 = new;
    b = new;
    b0 = new;
    s7 = new;
    #1 $finish;
  end
endmodule
