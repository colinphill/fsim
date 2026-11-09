// SPDX-License-Identifier: Apache-2.0
// uvm_root::find_all and uvm_root::get_children on the object uvm_root::get()
// returns (IEEE 1800.2-2020 13.2.4, 13.1.4), called from a class method with a
// class property queue as the output actual. The hierarchy is uvm_test_top
// (mytest) with e (env), whose children are a and b, and b has c. find_all
// walks the children of a component first, in name order, then the component
// itself when its full name matches the glob; the root is the empty full name
// and matches neither pattern below. Hand-derived results:
//
//   find_all("uvm_test_top.e.*", all)   a, then c (child of b), then b:
//                                       uvm_test_top.e.a, uvm_test_top.e.b.c,
//                                       uvm_test_top.e.b  (3 components; "*"
//                                       matches any run of characters,
//                                       dots included; uvm_test_top.e does
//                                       not match as it lacks the final dot)
//   get_children(kids)                  the top components: uvm_test_top
//   find_all("*.b", h.list)             uvm_test_top.e.b only ("*.b" ends
//                                       with ".b"; uvm_test_top.e.b.c does
//                                       not), into the queue of another
//                                       object, h
//
// The queues are class properties: all and kids of mytest, list of holder.
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

class holder;
  uvm_component list[$];
endclass

class mytest extends uvm_test;
  `uvm_component_utils(mytest)
  env e;
  uvm_component all[$];
  uvm_component kids[$];
  function new(string name, uvm_component parent);
    super.new(name, parent);
  endfunction
  function void build_phase(uvm_phase phase);
    super.build_phase(phase);
    e = env::type_id::create("e", this);
  endfunction
  function void end_of_elaboration_phase(uvm_phase phase);
    uvm_root r;
    holder h;
    super.end_of_elaboration_phase(phase);
    r = uvm_root::get();
    h = new;
    r.find_all("uvm_test_top.e.*", all);
    r.get_children(kids);
    r.find_all("*.b", h.list);
    $display("all=%0d kids=%0d held=%0d", all.size(), kids.size(),
      h.list.size());
    $display("all0=%s", all[0].get_full_name());
    $display("all1=%s", all[1].get_full_name());
    $display("all2=%s", all[2].get_full_name());
    $display("kid0=%s", kids[0].get_full_name());
    $display("held0=%s", h.list[0].get_full_name());
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
