// SPDX-License-Identifier: Apache-2.0
// `X::type_id::create` is the factory create only when X's `type_id` denotes
// a registry class of the UVM library (IEEE 1800.2-2020 8.2.4). X may be the
// typedef of a class (IEEE 1800-2023 6.18). A user class whose `type_id` denotes
// its own class with a `create` is an ordinary class scope: its static
// function runs. Hand-derived output: the typedef'd component reports 1, the
// user's create returns 42.
`timescale 1ns/1ns
import uvm_pkg::*;
`include "uvm_macros.svh"
class plain extends uvm_component;
  `uvm_component_utils(plain)
  function new(string name, uvm_component parent); super.new(name, parent); endfunction
  function int tag(); return 1; endfunction
endclass
typedef plain plain_t;
class my_reg;
  static int calls = 0;
  static function int create(string n);
    calls++;
    return 42;
  endfunction
endclass
class widget;
  typedef my_reg type_id;
endclass
class mytest extends uvm_test;
  `uvm_component_utils(mytest)
  plain p;
  plain_t q;
  function new(string name, uvm_component parent); super.new(name, parent); endfunction
  function void build_phase(uvm_phase phase);
    p = plain::type_id::create("p", this);
    q = plain_t::type_id::create("q", this);
  endfunction
  function void report_phase(uvm_phase phase);
    int v;
    v = widget::type_id::create("w");
    widget::type_id::create("w2");
    $display("p=%0d q=%0d widget=%0d calls=%0d", p.tag(), q.tag(), v, my_reg::calls);
  endfunction
endclass
module top;
  initial run_test("mytest");
endmodule
