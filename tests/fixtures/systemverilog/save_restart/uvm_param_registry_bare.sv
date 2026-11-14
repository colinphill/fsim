// SPDX-License-Identifier: Apache-2.0
// The bare name of a parameterized class denotes its default specialization
// (IEEE 1800-2023 8.25): `box::type_id::create` makes a box#(1) although the
// design names only box#(3) otherwise. Hand-derived output: widths 3 and 1.
`timescale 1ns/1ns
import uvm_pkg::*;
`include "uvm_macros.svh"
class box #(int W = 1) extends uvm_component;
  `uvm_component_param_utils(box#(W))
  function new(string name, uvm_component parent); super.new(name, parent); endfunction
  function int width(); return W; endfunction
endclass
class mytest extends uvm_test;
  `uvm_component_utils(mytest)
  box#(3) b3; box b1;
  function new(string name, uvm_component parent); super.new(name, parent); endfunction
  function void build_phase(uvm_phase phase);
    b3 = box#(3)::type_id::create("b3", this);
    b1 = box::type_id::create("b1", this);
  endfunction
  function void report_phase(uvm_phase phase);
    $display("b3=%0d b1=%0d", b3.width(), b1.width());
  endfunction
endclass
module top;
  initial run_test("mytest");
endmodule
