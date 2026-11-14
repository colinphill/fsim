// SPDX-License-Identifier: Apache-2.0
// The frames of methods the simulation's UVM services run, which are no
// generated code: methods of a class derived from the UVM library whose
// bodies call a static method of the library, `uvm_coreservice_t::get`
// (IEEE 1800-2023 37.43, 38.36.1). Their automatic variables are listed,
// read and written with no delay while the frame runs, and their statements
// take cbStmt, as any frame's.
`include "uvm_macros.svh"
package p;
  import uvm_pkg::*;
  class pkt extends uvm_object;
    `uvm_object_utils(pkt)
    function new(string name = "pkt");
      super.new(name);
    endfunction
    static function int twice(int x);
      int y;
      uvm_coreservice_t cs;
      cs = uvm_coreservice_t::get();
      y = x * 2;
      return y;
    endfunction
    static function int quad(int x, string who);
      int half;
      logic [7:0] code;
      string tag;
      code = 8'h5a;
      tag = {who, "!"};
      half = twice(x);
      $display("#c quad %s %0d %h %0d", tag, half, code,
        uvm_root::get() != null);
      return twice(half);
    endfunction
  endclass
endpackage
module top;
  import p::*;
  int result;
  initial begin
    result = pkt::quad(3, "q");
    $display("#c result %0d", result);
  end
endmodule
