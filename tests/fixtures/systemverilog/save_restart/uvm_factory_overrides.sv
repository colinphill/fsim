// SPDX-License-Identifier: Apache-2.0
// Factory type overrides applied by `T::type_id::create` (IEEE 1800.2-2020
// 8.2.4.2, 8.3.1.4.1): with item overridden by item2 and leaf by leaf2, the
// creates of item and leaf make item2 and leaf2, whose constructors run
// (each sets its kind after `super.new`); the create of other, which has no
// override, makes other. Hand-derived output: l=leaf2 i=item2 o=other, and
// the full name of l is uvm_test_top.l.
`timescale 1ns/1ns
import uvm_pkg::*;
`include "uvm_macros.svh"
class item extends uvm_sequence_item;
  `uvm_object_utils(item)
  string kind = "item";
  function new(string name = "item"); super.new(name); endfunction
endclass
class item2 extends item;
  `uvm_object_utils(item2)
  function new(string name = "item2"); super.new(name); kind = "item2"; endfunction
endclass
class leaf extends uvm_component;
  `uvm_component_utils(leaf)
  string kind = "leaf";
  function new(string name, uvm_component parent); super.new(name, parent); endfunction
endclass
class leaf2 extends leaf;
  `uvm_component_utils(leaf2)
  function new(string name, uvm_component parent); super.new(name, parent); kind = "leaf2"; endfunction
endclass
class other extends uvm_object;
  `uvm_object_utils(other)
  string kind = "other";
  function new(string name = "other"); super.new(name); endfunction
endclass
class mytest extends uvm_test;
  `uvm_component_utils(mytest)
  leaf l; item i; other o;
  function new(string name, uvm_component parent); super.new(name, parent); endfunction
  function void build_phase(uvm_phase phase);
    uvm_factory f = uvm_factory::get();
    f.set_type_override_by_type(item::get_type(), item2::get_type());
    f.set_type_override_by_type(leaf::get_type(), leaf2::get_type());
    l = leaf::type_id::create("l", this);
    i = item::type_id::create("i");
    o = other::type_id::create("o");
  endfunction
  function void report_phase(uvm_phase phase);
    $display("l=%s i=%s o=%s", l.kind, i.kind, o.kind);
    $display("path=%s", l.get_full_name());
  endfunction
endclass
module top;
  initial run_test("mytest");
endmodule
