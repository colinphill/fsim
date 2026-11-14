// SPDX-License-Identifier: Apache-2.0
// uvm_root::print_topology with a printer argument (IEEE 1800.2-2020
// 13.2.4.2, 16.2): the layout is the printer's kind, decided by the
// printer's class and the classes it derives from. The test builds
// uvm_test_top (type mytest) with the child e (env) and e's children a and b
// (leaf), and at t=1 prints the topology three times:
//
//   a uvm_tree_printer   the tree layout: each component "name: (type) @id {",
//                        its children indented two more spaces, "}" closing
//                        it (uvm_tree_printer::emit)
//   a uvm_line_printer   the line layout: the tree layout on one line, with a
//                        newline of " " and no indentation
//   a my_printer         a user class derived from uvm_tree_printer: the
//                        tree layout, as for its base class
//
// The instance ids are run state: the runner replaces "@<digits>" with "@N".
`timescale 1ns/1ns
import uvm_pkg::*;
`include "uvm_macros.svh"

class leaf extends uvm_component;
  `uvm_component_utils(leaf)
  function new(string name, uvm_component parent);
    super.new(name, parent);
  endfunction
endclass

class env extends uvm_env;
  `uvm_component_utils(env)
  leaf a;
  leaf b;
  function new(string name, uvm_component parent);
    super.new(name, parent);
  endfunction
  function void build_phase(uvm_phase phase);
    super.build_phase(phase);
    a = leaf::type_id::create("a", this);
    b = leaf::type_id::create("b", this);
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
    phase.drop_objection(this);
  endtask
endclass

class my_printer extends uvm_tree_printer;
  function new();
    super.new();
  endfunction
endclass

module top;
  initial run_test("mytest");
  initial begin
    uvm_root r;
    uvm_tree_printer tree;
    uvm_line_printer line;
    my_printer mine;
    #1;
    r = uvm_root::get();
    tree = new();
    line = new();
    mine = new();
    $display("tree:");
    r.print_topology(tree);
    $display("line:");
    r.print_topology(line);
    $display("derived:");
    r.print_topology(mine);
  end
endmodule
