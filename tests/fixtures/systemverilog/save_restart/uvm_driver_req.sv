// SPDX-License-Identifier: Apache-2.0
// A user driver derived from uvm_driver #(pkt) calls methods on `req`, the
// property of type REQ it inherits from the library class (IEEE 1800-2023
// 8.25, 8.13; IEEE 1800.2-2020 14.2): the receiver's class is the one the
// derived class's extends clause binds REQ to, and the calls run as compiled
// code, no class executor. A sequence sends two items through a sequencer.
//
// Hand-derived output: item i has id i; the driver prints req.convert2string()
// ("pkt id=i") and req.some_user_method(10), id + 10: 11 and 12.
`timescale 1ns/1ns
import uvm_pkg::*;
`include "uvm_macros.svh"

class pkt extends uvm_sequence_item;
  int id;
  `uvm_object_utils(pkt)
  function new(string name = "pkt");
    super.new(name);
  endfunction
  function string convert2string();
    return $sformatf("pkt id=%0d", id);
  endfunction
  function int some_user_method(int d);
    return id + d;
  endfunction
endclass

class pkt_seq extends uvm_sequence #(pkt);
  `uvm_object_utils(pkt_seq)
  function new(string name = "pkt_seq");
    super.new(name);
  endfunction
  task body();
    pkt p;
    for (int i = 1; i <= 2; i++) begin
      p = pkt::type_id::create("p");
      start_item(p);
      p.id = i;
      finish_item(p);
    end
  endtask
endclass

class my_drv extends uvm_driver #(pkt);
  `uvm_component_utils(my_drv)
  function new(string name, uvm_component parent);
    super.new(name, parent);
  endfunction
  task run_phase(uvm_phase phase);
    forever begin
      seq_item_port.get_next_item(req);
      $display("drv %s plus=%0d", req.convert2string(),
          req.some_user_method(10));
      seq_item_port.item_done();
    end
  endtask
endclass

class mytest extends uvm_test;
  `uvm_component_utils(mytest)
  my_drv drv;
  uvm_sequencer #(pkt) sqr;
  function new(string name, uvm_component parent);
    super.new(name, parent);
  endfunction
  function void build_phase(uvm_phase phase);
    super.build_phase(phase);
    sqr = new("sqr", this);
    drv = my_drv::type_id::create("drv", this);
  endfunction
  function void connect_phase(uvm_phase phase);
    drv.seq_item_port.connect(sqr.seq_item_export);
  endfunction
  task run_phase(uvm_phase phase);
    pkt_seq seq;
    phase.raise_objection(this);
    seq = pkt_seq::type_id::create("seq");
    seq.start(sqr);
    phase.drop_objection(this);
  endtask
endclass

module top;
  initial run_test("mytest");
endmodule
