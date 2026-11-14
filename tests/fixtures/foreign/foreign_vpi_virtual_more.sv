// SPDX-License-Identifier: Apache-2.0
// Virtual interface variables (IEEE 1800-2023 37.29): vpiExpr is the
// interface instance the declaration assigns, NULL without one; vpiActual is
// the instance the variable refers to now; the variables of generate blocks
// are published with their block. A package declares one too (25.9), which
// the design accepts and assigns. A typedef of a virtual interface type is an
// interface typespec of the scope that declares it, whose actuals are
// parameter assigns (37.30); arrays of virtual interfaces of every kind are
// arrays of virtual interface variables (37.29 example 1).
interface SBus #(parameter int W = 8);
  logic [W-1:0] data;
endinterface

package pkg;
  virtual SBus pv;
  typedef virtual SBus#(32) SB32;
  class Holder;
    virtual SBus one;
    virtual SBus fixed [2];
    virtual SBus dyn [];
    virtual SBus que [$];
  endclass
endpackage

module top;
  SBus s1 ();
  SBus #(16) s16 ();
  virtual SBus v1 = s1;
  virtual SBus #(16) v16 = s16;
  virtual SBus vn;
  virtual SBus v2;
  typedef virtual SBus#(16) SB16;
  SB16 tv;
  virtual SBus da [];
  virtual SBus qa [$];
  virtual SBus fa [2];
  pkg::Holder h;
  for (genvar g = 0; g < 2; g++) begin : gen
    typedef virtual SBus#(4) SB4;
    virtual SBus gv;
    virtual SBus ga [2];
    virtual SBus gd [];
    initial begin
      gv = (g == 0) ? s1 : s16;
      ga[1] = s1;
      gd = new[1];
      gd[0] = s16;
    end
  end
  initial begin
    pkg::pv = s16;
    vn = s1;
    v1 = s16;
    v2 = v1;
    tv = s16;
    da = new[2];
    da[1] = s1;
    qa.push_back(s16);
    qa.push_back(s1);
    fa[0] = s16;
    h = new;
    h.one = s1;
    h.fixed[1] = s16;
    h.dyn = new[2];
    h.dyn[0] = s1;
    h.que.push_back(s16);
    #2 $finish;
  end
endmodule
