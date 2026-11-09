// SPDX-License-Identifier: Apache-2.0
// $save and $restart of a UVM testbench (IEEE 1800-2023 D.9; IEEE 1800.2-2020
// 9, 12, 13, 15, 16): the services hold the component hierarchy, the phases,
// the objection, the configuration entries, the sequencer's handshake and the
// report counts, and a restart from a file written in the middle of the run
// phase finishes the test exactly as the uninterrupted run does.
//
// The test mytest builds env, which builds a sequencer and a driver, stores the
// integer 7 as "cfg" for the driver (build phase) and connects the driver's
// seq_item_port to the sequencer. In its run phase env raises an objection
// (objection count 1), reads cfg back through the driver's scope and reports
// it (UVM_INFO, id ENV), and starts seq3 on the sequencer. seq3 sends four
// items, 10, 20, 30, 40, waiting 10 ns after each item. The driver takes an
// item with get at once, waits 3 ns and adds it to sum, printing the time, the
// item and the sum. After the sequence returns env waits 5 ns and drops the
// objection, which ends the run phase.
//
// Hand-derived times (ns): item k (k = 1..4) is sent at 10 (k - 1), so the
// driver prints at 3, 13, 23 and 33 with sums 10, 30, 60 and 100. The sequence
// body ends at 40 (after the last wait), env drops at 45. The report phase
// prints the sum (UVM_INFO, id RESULT) and one UVM_ERROR (id EXPECTED), both at
// 45; the report summary counts 2 infos and 1 error, ids ENV, EXPECTED and
// RESULT once each (uvm_report_server, 6.5). The unbroken output is
// uvm_save_restart.expected.
//
// The design saves twice: at the end of the slot at t=1 (early.state: the
// driver has taken item 1 and waits, the sequence waits 10 ns, the objection
// is raised) and at t=18 (late.state: item 2 is done, the driver waits in get
// for item 3, the sequence waits until 20). A restart from either prints the
// lines of the unbroken run after the save point: the ENV line (t=0) is only
// in the saving run, and the summary counts include it, so the restored report
// server carries the count of 1 for ENV (early.expected, late.expected).
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
    #1 $save("early.state");
    #17 $save("late.state");
  end
endmodule
