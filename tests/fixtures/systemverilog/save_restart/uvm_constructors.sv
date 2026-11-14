// SPDX-License-Identifier: Apache-2.0
// Constructors of classes deriving from the UVM library, run by the process
// that creates the object: run_test's uvm_test_top (IEEE 1800.2-2020
// F.3.1.2), factory creates of a component and an object (8.2.4.2) and a
// direct `new` (IEEE 1800-2023 8.7). Each constructor reads the base state
// that `super.new` made (the name and full name, 5.3.4, 13.1.3), and its
// property initializers run after `super.new` (8.7).
//
// Hand-derived output: the test is built first (its constructor runs before
// any phase), then build_phase of the test makes env e (factory, parent the
// test), whose build_phase makes c1 (factory) and c2 (`new`), and the item
// it (factory) with the name the create passes, read in its constructor.
// A create with an empty name constructs the object with `new()`, so its
// constructor's default name applies (8.2.4.2.1): it2 is named "item". Each
// constructor counts itself in a static counter, so the report sees 4
// components (test, e, c1, c2) and 2 items.
`timescale 1ns/1ns
import uvm_pkg::*;
`include "uvm_macros.svh"

class counter;
  static int components;
  static int items;
endclass

class item extends uvm_sequence_item;
  `uvm_object_utils(item)
  string seen;
  int value = 7;
  function new(string name = "item");
    super.new(name);
    seen = get_name();
    value += 1;
    counter::items++;
  endfunction
endclass

class leaf extends uvm_component;
  `uvm_component_utils(leaf)
  string path;
  int width = 4;
  function new(string name, uvm_component parent);
    super.new(name, parent);
    path = get_full_name();
    width *= 2;
    counter::components++;
  endfunction
endclass

class env extends uvm_env;
  `uvm_component_utils(env)
  leaf c1;
  leaf c2;
  item it;
  item it2;
  string path;
  function new(string name, uvm_component parent);
    super.new(name, parent);
    path = get_full_name();
    counter::components++;
  endfunction
  function void build_phase(uvm_phase phase);
    super.build_phase(phase);
    c1 = leaf::type_id::create("c1", this);
    c2 = new("c2", this);
    it = item::type_id::create("it");
    it2 = item::type_id::create("");
  endfunction
endclass

class mytest extends uvm_test;
  `uvm_component_utils(mytest)
  env e;
  string path;
  bit orphan;
  function new(string name, uvm_component parent);
    super.new(name, parent);
    path = get_full_name();
    orphan = parent == null;
    counter::components++;
  endfunction
  function void build_phase(uvm_phase phase);
    super.build_phase(phase);
    e = env::type_id::create("e", this);
  endfunction
  function void report_phase(uvm_phase phase);
    $display("test=%s orphan=%0d", path, orphan);
    $display("env=%s", e.path);
    $display("c1=%s width=%0d", e.c1.path, e.c1.width);
    $display("c2=%s width=%0d parent=%s", e.c2.path, e.c2.width,
      e.c2.get_parent().get_full_name());
    $display("it=%s value=%0d", e.it.seen, e.it.value);
    $display("it2=%s", e.it2.seen);
    $display("components=%0d items=%0d", counter::components,
      counter::items);
    `uvm_info("DONE", "ok", UVM_LOW)
  endfunction
endclass

module top;
  initial run_test("mytest");
endmodule
