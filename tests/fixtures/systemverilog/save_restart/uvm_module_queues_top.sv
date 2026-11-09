// SPDX-License-Identifier: Apache-2.0
// uvm_root::find_all and uvm_root::get_children called from module code on
// uvm_top (the object uvm_root::get() returns), with queues declared in the
// module as the ref actuals (IEEE 1800-2017 13.5.2, 7.10; IEEE 1800.2-2020
// 13.2.4, 13.1.4). The hierarchy is uvm_test_top (mytest) with e (env), whose
// children are a and b, and b has c. The build phases run at time 0, before
// the module's process wakes at time 5. find_all walks the children of a
// component first, in name order, then the component itself when its full
// name matches the glob; the root has the empty name and is never returned.
// Both methods add to the queue they are given (the library's push_back).
// The module reads uvm_top itself: UVM 1.2 initializes it with
// uvm_root::get(), 1800.2-2020.3.1 assigns it when run_test makes the root
// (uvm_root::m_uvm_get_root). Hand-derived results:
//
//   find_all("uvm_test_top.e.*", all)   uvm_test_top.e.a, uvm_test_top.e.b.c,
//                                       uvm_test_top.e.b: 3 components
//   get_children(kids)                  the top components: uvm_test_top
//   find_all("*.b", all)                appends uvm_test_top.e.b: 4 in all
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
  task run_phase(uvm_phase phase);
    phase.raise_objection(this);
    #10;
    `uvm_info("DONE", "ok", UVM_LOW)
    phase.drop_objection(this);
  endtask
endclass

module top;
  uvm_component all[$];
  uvm_component kids[$];
  initial run_test("mytest");
  initial begin
    #5;
    uvm_top.find_all("uvm_test_top.e.*", all);
    uvm_top.get_children(kids);
    $display("all=%0d kids=%0d", all.size(), kids.size());
    foreach (all[i])
      $display("all%0d=%s", i, all[i].get_full_name());
    $display("kid0=%s", kids[0].get_full_name());
    uvm_top.find_all("*.b", all);
    $display("all=%0d last=%s", all.size(), all[3].get_full_name());
  end
endmodule
