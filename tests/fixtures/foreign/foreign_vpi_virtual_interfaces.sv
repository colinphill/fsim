// SPDX-License-Identifier: Apache-2.0
// Virtual interface variables and interface typespecs as VPI objects (IEEE
// 1800-2023 37.29, 37.30).
interface SBus #(parameter WIDTH = 8);
  logic req;
  logic [WIDTH-1:0] addr;
  modport phy(input addr);
endinterface
package pk;
  class Xactor;
    virtual SBus bus;
    virtual SBus.phy pbus;
    function new(virtual SBus s);
      bus = s;
    endfunction
  endclass
endpackage
module top;
  SBus s1();
  SBus s2();
  virtual SBus vi = s1;
  virtual SBus vn;
  virtual SBus.phy vm;
  int plain;
  pk::Xactor t;
  initial begin
    t = new(s2);
    vn = s2;
    #2 $finish;
  end
endmodule
