// SPDX-License-Identifier: Apache-2.0
// An array of virtual interfaces as VPI objects (IEEE 1800-2023 37.29
// example 1): an array var whose elements are virtual interface variables.
interface SBus;
  logic req;
endinterface
module top;
  SBus s1();
  SBus s2();
  virtual SBus va [2];
  virtual SBus vb;
  initial begin
    va[0] = s1;
    va[1] = s2;
    vb = s2;
    #2 $finish;
  end
endmodule
