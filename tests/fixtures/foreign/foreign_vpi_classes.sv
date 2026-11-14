// SPDX-License-Identifier: Apache-2.0
// Class definitions, class typespecs and class variables as VPI objects
// (IEEE 1800-2023 37.31, 37.32, 37.33).
package pk;
  virtual class Base #(int W = 4);
    protected int hidden;
    static int count;
    rand bit [3:0] id;
    localparam int LP = W - 1;
    typedef int unsigned word_t;
    pure virtual function int get();
    virtual function void set(int v);
      hidden = v;
    endfunction
    function new();
    endfunction
  endclass
  class Derived #(type T = int) extends Base #(8);
    int item;
    virtual function int get();
      return 1;
    endfunction
  endclass
endpackage
module top;
  import pk::*;
  class Local;
    int x;
    function int twice();
      return x * 2;
    endfunction
  endclass
  Derived#(int) d;
  Derived#(byte) e;
  Local l;
  mailbox mb;
  initial #3 $finish;
endmodule
