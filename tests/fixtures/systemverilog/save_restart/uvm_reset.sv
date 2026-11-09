// SPDX-License-Identifier: Apache-2.0
// $reset of a UVM testbench (IEEE 1800-2023 D.8; IEEE 1800.2-2020 9, 12, 13,
// 15, 16): the same test as uvm_save_restart.sv (hand-derived there: the driver
// prints at 3, 13, 23 and 33 ns with sums 10, 30, 60 and 100, the report phase
// prints at 45 ns, the summary counts 2 infos and 1 error). A second process
// calls $reset(1, 5) at t=18 of the first life, in the middle of the run phase
// with the objection raised and the sequence running.
//
//  life 1: t=0 ENV line (cfg=7); t=1 "start count=0 value=0"; the driver
//          prints at 3 and 13; $reset at 18.
//  life 2: time is 0 again, all variables and class objects are back to their
//          initial values, every initial procedure starts again and run_test
//          builds the test from nothing: the component hierarchy, phases,
//          objection, configuration and the report counts are those of a
//          fresh run. The ENV line, "start count=1 value=5" at t=1, the four
//          driver lines, the RESULT info and EXPECTED error at 45 and the
//          summary are the uninterrupted run's; the summary counts only life 2
//          (2 infos, 1 error, each id once), and sum=100 shows that the
//          driver object is a new one. $reset_count and $reset_value (1 and 5)
//          are the only state that survives (uvm_reset.expected).
`timescale 1ns/1ns
import uvm_pkg::*;
`include "uvm_macros.svh"

class packet extends uvm_sequence_item;
  int data;
  `uvm_object_utils(packet)
  function new(string name = "packet");
    super.new(name);
  endfunction
endclass

class seq3 extends uvm_sequence #(packet);
  `uvm_object_utils(seq3)
  function new(string name = "seq3");
    super.new(name);
  endfunction
  task body;
    packet p;
    for (int i = 1; i <= 4; i++) begin
      p = packet::type_id::create("p");
      start_item(p);
      p.data = i * 10;
      finish_item(p);
      #10;
    end
  endtask
endclass

class sqr extends uvm_sequencer #(packet);
  `uvm_component_utils(sqr)
  function new(string name, uvm_component parent);
    super.new(name, parent);
  endfunction
endclass

class drv extends uvm_driver #(packet);
  `uvm_component_utils(drv)
  int sum;
  packet pk;
  function new(string name, uvm_component parent);
    super.new(name, parent);
  endfunction
  task run_phase(uvm_phase phase);
    repeat (4) begin
      seq_item_port.get(pk);
      #3;
      sum += pk.data;
      $display("DRV t=%0t item %0d sum=%0d", $time, pk.data, sum);
    end
  endtask
endclass

class env extends uvm_env;
  `uvm_component_utils(env)
  sqr s;
  drv d;
  function new(string name, uvm_component parent);
    super.new(name, parent);
  endfunction
  function void build_phase(uvm_phase phase);
    super.build_phase(phase);
    s = sqr::type_id::create("s", this);
    d = drv::type_id::create("d", this);
    uvm_config_db#(int)::set(this, "d", "cfg", 7);
  endfunction
  function void connect_phase(uvm_phase phase);
    d.seq_item_port.connect(s.seq_item_export);
  endfunction
  task run_phase(uvm_phase phase);
    seq3 q;
    int c;
    phase.raise_objection(this);
    q = seq3::type_id::create("q");
    if (!uvm_config_db#(int)::get(d, "", "cfg", c)) c = -1;
    `uvm_info("ENV", $sformatf("cfg=%0d", c), UVM_LOW)
    q.start(s);
    #5;
    phase.drop_objection(this);
  endtask
  function void report_phase(uvm_phase phase);
    `uvm_info("RESULT", $sformatf("sum=%0d", d.sum), UVM_LOW)
    `uvm_error("EXPECTED", "one error for the summary")
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
endclass

module top;
  initial run_test("mytest");
  initial begin
    #1 $display("start count=%0d value=%0d", $reset_count, $reset_value);
    #17 if ($reset_count == 0) $reset(1, 5);
  end
endmodule
