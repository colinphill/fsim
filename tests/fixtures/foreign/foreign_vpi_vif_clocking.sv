// SPDX-License-Identifier: Apache-2.0
// A clocking block named through a virtual interface (IEEE 1800-2023 37.48
// details 2 and 3): `vif.cb` has the variable as its vpiPrefix, and its
// vpiActual is the clocking block of the interface instance the variable
// refers to now, NULL when it refers to none.
interface bus_if(input logic clk);
  logic [3:0] d;
  clocking cb @(posedge clk);
    output d;
  endclocking
endinterface
module top;
  logic clk = 0;
  bus_if u0(clk);
  bus_if u1(clk);
  virtual bus_if vif;
  virtual bus_if idle;
  initial begin : main
    vif = u1;
    @(vif.cb);
    @(idle.cb);
  end
endmodule
