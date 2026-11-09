// SPDX-License-Identifier: Apache-2.0
// The methods of the object uvm_root::get() returns (IEEE 1800.2-2020 13.2,
// 13.1.3, 13.1.4): they are answered from the simulation's component
// hierarchy. The test builds uvm_test_top (type mytest) with the child e (env),
// whose children are a (leaf) and b (mid), and b has the child c (leaf); the
// children are created in name order, so creation order and name order agree.
// At t=1, after the build phases, a second process reads the root:
//
//   get_num_children   1 (uvm_test_top is the only top component)
//   get_name, get_full_name, get_type_name   "__top__", "" and "uvm_root"
//   has_child          1 for "uvm_test_top", 0 for "x"
//   find               "uvm_test_top.e.b.c" matches that component only; a
//                      pattern with no match returns null and reports the
//                      warning CMPNFD (13.2.4.1) from the root
//   print_topology     one report UVMTOP: the table printer's rows, one per
//                      component, level 0 name the full name, deeper levels
//                      the leaf name indented two spaces per level, type
//                      the type name, size "-", value "@<instance id>". The
//                      widths: Name = max(4, indent*level + name length) =
//                      12 ("uvm_test_top"), Type = max(4, 6) = 6 ("mytest"),
//                      Size = 4, Value = 5. Each column is its width plus 2
//                      wide, and the dash lines are 14 + 8 + 6 + 5 = 33
//                      characters (uvm_table_printer::emit). The instance ids
//                      are run state: the runner replaces "@<digits>" at the
//                      end of a row with "@N".
//
// The test ends at t=10 when the run phase drops its objection; the report
// summary counts 1 info (UVMTOP) and 1 warning (CMPNFD).
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
    phase.drop_objection(this);
  endtask
endclass

module top;
  initial run_test("mytest");
  initial begin
    uvm_root r;
    uvm_component f;
    #1;
    r = uvm_root::get();
    $display("children=%0d", r.get_num_children());
    $display("name=%s full=[%s] type=%s", r.get_name(), r.get_full_name(),
      r.get_type_name());
    $display("has=%0d%0d", r.has_child("uvm_test_top"), r.has_child("x"));
    f = r.find("uvm_test_top.e.b.c");
    $display("found=%s", f == null ? "null" : f.get_full_name());
    f = r.find("*.nomatch");
    $display("nomatch=%s", f == null ? "null" : f.get_full_name());
    r.print_topology();
  end
endmodule
