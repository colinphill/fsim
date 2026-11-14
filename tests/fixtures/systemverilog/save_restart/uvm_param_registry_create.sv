// SPDX-License-Identifier: Apache-2.0
// `T::type_id::create` makes the specialization its type expression denotes
// (IEEE 1800-2023 8.25, 6.22.1; IEEE 1800.2-2020 8.2.4.2). The class box has
// registered specializations: box#(3) and box#(5), named by `#(...)`, box#(7), named
// through a typedef (6.18), and box#(1), which the bare name `box`
// denotes. Hand-derived output: each create makes the class its expression
// names, so the widths are 3, 5, 7 and 1. The create statements `s3` and `s5`
// discard the handle; each specialization has a static counter of its own
// (8.25.1), so box#(3) and box#(5) count two objects, box#(7) and box one.
`timescale 1ns/1ns
import uvm_pkg::*;
`include "uvm_macros.svh"
class box #(int W = 1) extends uvm_component;
  `uvm_component_param_utils(box#(W))
  static int made = 0;
  function new(string name, uvm_component parent); super.new(name, parent); made++; endfunction
  function int width(); return W; endfunction
endclass
typedef box#(7) box7_t;
class mytest extends uvm_test;
  `uvm_component_utils(mytest)
  box#(3) b3; box#(5) b5; box7_t b7; box b1;
  function new(string name, uvm_component parent); super.new(name, parent); endfunction
  function void build_phase(uvm_phase phase);
    b3 = box#(3)::type_id::create("b3", this);
    b5 = box#(5)::type_id::create("b5", this);
    b7 = box7_t::type_id::create("b7", this);
    b1 = box::type_id::create("b1", this);
    box#(3)::type_id::create("s3", this);
    box#(5)::type_id::create("s5", this);
  endfunction
  function void report_phase(uvm_phase phase);
    $display("b3=%0d b5=%0d b7=%0d b1=%0d", b3.width(), b5.width(), b7.width(), b1.width());
    $display("made: box#(3)=%0d box#(5)=%0d box#(7)=%0d box#(1)=%0d",
        box#(3)::made, box#(5)::made, box7_t::made, box#(1)::made);
  endfunction
endclass
module top;
  initial run_test("mytest");
endmodule
