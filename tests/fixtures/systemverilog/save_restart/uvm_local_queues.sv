// SPDX-License-Identifier: Apache-2.0
// uvm_root::find_all and uvm_root::get_children on the object uvm_root::get()
// returns (IEEE 1800.2-2020 13.2.4, 13.1.4), called from a class method with
// queues declared in the method body as the ref actuals (IEEE 1800-2017
// 13.5.2, 7.10). The hierarchy is uvm_test_top (mytest) with e (env), whose
// children are a and b, and b has c. find_all walks the children of a
// component first, in name order, then the component itself when its full
// name matches the glob; the root has the empty name and is never returned.
// Both methods add to the queue they are given (the library's push_back), so
// a second call extends it. Hand-derived results:
//
//   find_all("uvm_test_top.e.*", all)   uvm_test_top.e.a, then
//                                       uvm_test_top.e.b.c (child of b), then
//                                       uvm_test_top.e.b: 3 components
//   get_children(kids)                  the top components: uvm_test_top
//   find_all("*.b", all)                appends uvm_test_top.e.b: 4 in all
//   a queue declared later is empty     size 0
//
// The queues are locals of the method; the elements are read by index and by
// foreach.
`timescale 1ns/1ns
import uvm_pkg::*;
`include "uvm_macros.svh"

class leaf extends uvm_component;
  `uvm_component_utils(leaf)
  function new(string name, uvm_component parent);
    super.new(name, parent);
  endfunction
endclass

class mid extends uvm_component;
  `uvm_component_utils(mid)
  leaf c;
  function new(string name, uvm_component parent);
    super.new(name, parent);
  endfunction
  function void build_phase(uvm_phase phase);
    super.build_phase(phase);
    c = leaf::type_id::create("c", this);
  endfunction
endclass

class env extends uvm_env;
  `uvm_component_utils(env)
  leaf a;
  mid b;
  function new(string name, uvm_component parent);
    super.new(name, parent);
  endfunction
  function void build_phase(uvm_phase phase);
    super.build_phase(phase);
    a = leaf::type_id::create("a", this);
    b = mid::type_id::create("b", this);
  endfunction
endclass

class mytest extends uvm_test;
  `uvm_component_utils(mytest)
  env e;
  function new(string name, uvm_component parent);
    super.new(name, parent);
  endfunction
  function void build_phase(uvm_phase phase);
    super.build_phase(phase);
    e = env::type_id::create("e", this);
  endfunction
  function void end_of_elaboration_phase(uvm_phase phase);
    uvm_root r;
    uvm_component all[$];
    uvm_component kids[$];
    uvm_component none[$];
    super.end_of_elaboration_phase(phase);
    r = uvm_root::get();
    r.find_all("uvm_test_top.e.*", all);
    r.get_children(kids);
    $display("all=%0d kids=%0d none=%0d", all.size(), kids.size(),
      none.size());
    foreach (all[i])
      $display("all%0d=%s", i, all[i].get_full_name());
    $display("kid0=%s", kids[0].get_full_name());
    r.find_all("*.b", all);
    $display("all=%0d last=%s", all.size(), all[3].get_full_name());
  endfunction
  task run_phase(uvm_phase phase);
    phase.raise_objection(this);
    #10;
    `uvm_info("DONE", "ok", UVM_LOW)
    phase.drop_objection(this);
  endtask
endclass

module top;
  initial run_test("mytest");
endmodule
