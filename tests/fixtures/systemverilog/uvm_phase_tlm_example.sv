// SPDX-License-Identifier: Apache-2.0
package fsim_uvm_phase_tlm_example_pkg;
  import uvm_pkg::uvm_object;
  import uvm_pkg::uvm_component;
  import uvm_pkg::uvm_phase;
  import uvm_pkg::uvm_blocking_put_port;
  import uvm_pkg::uvm_tlm_fifo;
  import uvm_pkg::uvm_sequence_item;
  import uvm_pkg::uvm_sequence;
  import uvm_pkg::uvm_sequencer;
  import uvm_pkg::uvm_driver;
  import uvm_pkg::uvm_monitor;
  import uvm_pkg::uvm_agent;
  import uvm_pkg::uvm_scoreboard;
  import uvm_pkg::uvm_env;
  import uvm_pkg::uvm_test;
  import uvm_pkg::uvm_object_wrapper;
  import uvm_pkg::uvm_callback;
  import uvm_pkg::uvm_reg;
  import uvm_pkg::uvm_reg_block;
  import uvm_pkg::uvm_reg_adapter;
  import uvm_pkg::uvm_reg_bus_op;
  import uvm_pkg::uvm_reg_predictor;
  import uvm_pkg::uvm_reg_sequence;
  import uvm_pkg::uvm_reg_cbs;

  class fsim_uvm_payload extends uvm_object;
    int value;

    function new(string name = "fsim_uvm_payload");
      super.new(name);
    endfunction
  endclass

  class fsim_uvm_item extends uvm_sequence_item;
    int value;

    function new(string name = "fsim_uvm_item");
      super.new(name);
    endfunction
  endclass

  class fsim_uvm_sequence extends uvm_sequence #(fsim_uvm_item);
    function new(string name = "fsim_uvm_sequence");
      super.new(name);
    endfunction
  endclass

  class fsim_uvm_virtual_sequence extends uvm_sequence #(fsim_uvm_item);
    function new(string name = "fsim_uvm_virtual_sequence");
      super.new(name);
    endfunction
  endclass

  class fsim_uvm_sequencer extends uvm_sequencer #(fsim_uvm_item);
    function new(string name = "fsim_uvm_sequencer",
                 uvm_component parent = null);
      super.new(name, parent);
    endfunction
  endclass

  class fsim_uvm_driver extends uvm_driver #(fsim_uvm_item);
    function new(string name = "fsim_uvm_driver",
                 uvm_component parent = null);
      super.new(name, parent);
    endfunction
  endclass

  class fsim_uvm_monitor extends uvm_monitor;
    function new(string name = "fsim_uvm_monitor",
                 uvm_component parent = null);
      super.new(name, parent);
    endfunction
  endclass

  class fsim_uvm_agent extends uvm_agent;
    function new(string name = "fsim_uvm_agent",
                 uvm_component parent = null);
      super.new(name, parent);
    endfunction
  endclass

  class fsim_uvm_scoreboard extends uvm_scoreboard;
    function new(string name = "fsim_uvm_scoreboard",
                 uvm_component parent = null);
      super.new(name, parent);
    endfunction
  endclass

  class fsim_uvm_env extends uvm_env;
    function new(string name = "fsim_uvm_env",
                 uvm_component parent = null);
      super.new(name, parent);
    endfunction
  endclass

  class fsim_uvm_test extends uvm_test;
    function new(string name = "fsim_uvm_test",
                 uvm_component parent = null);
      super.new(name, parent);
    endfunction

    static function uvm_object_wrapper get_type();
      return null;
    endfunction

    virtual function uvm_object_wrapper get_object_type();
      return get_type();
    endfunction
  endclass

  class fsim_uvm_callback extends uvm_callback;
    function new(string name = "fsim_uvm_callback");
      super.new(name);
    endfunction
  endclass

  class fsim_uvm_reg extends uvm_reg;
    function new(string name = "fsim_uvm_reg");
      super.new(name, 32, 0);
    endfunction
  endclass

  class fsim_uvm_reg_block extends uvm_reg_block;
    function new(string name = "fsim_uvm_reg_block");
      super.new(name, 0);
    endfunction
  endclass

  class fsim_uvm_reg_adapter extends uvm_reg_adapter;
    function new(string name = "fsim_uvm_reg_adapter");
      super.new(name);
    endfunction

    virtual function uvm_sequence_item reg2bus(const ref uvm_reg_bus_op rw);
      return null;
    endfunction

    virtual function void bus2reg(uvm_sequence_item bus_item,
                                  ref uvm_reg_bus_op rw);
    endfunction
  endclass

  class fsim_uvm_reg_predictor extends uvm_reg_predictor #(fsim_uvm_item);
    function new(string name = "fsim_uvm_reg_predictor",
                 uvm_component parent = null);
      super.new(name, parent);
    endfunction
  endclass

  class fsim_uvm_reg_sequence extends uvm_reg_sequence;
    function new(string name = "fsim_uvm_reg_sequence");
      super.new(name);
    endfunction
  endclass

  class fsim_uvm_reg_callback extends uvm_reg_cbs;
    function new(string name = "fsim_uvm_reg_callback");
      super.new(name);
    endfunction
  endclass

  class fsim_native_component extends uvm_component;
    int marker;
    uvm_blocking_put_port #(fsim_uvm_payload) phase_port;
    uvm_tlm_fifo #(fsim_uvm_payload) phase_fifo;

    function new(string name = "fsim_native_component",
                 uvm_component parent = null);
      super.new(name, parent);
      marker = 10;
    endfunction

    function void build_phase(uvm_phase phase);
      marker = 1;
    endfunction

    function void connect_phase(uvm_phase phase);
      marker = 2;
    endfunction

    function void end_of_elaboration_phase(uvm_phase phase);
      marker = 3;
    endfunction

    function void start_of_simulation_phase(uvm_phase phase);
      marker = 4;
    endfunction

    task run_phase(uvm_phase phase);
      marker = 5;
    endtask

    function void extract_phase(uvm_phase phase);
      marker = 6;
    endfunction

    function void check_phase(uvm_phase phase);
      marker = 7;
    endfunction

    function void report_phase(uvm_phase phase);
      marker = 8;
    endfunction

    function void final_phase(uvm_phase phase);
      marker = 9;
    endfunction
  endclass

endpackage

module fsim_uvm_phase_tlm_example;
  import fsim_uvm_phase_tlm_example_pkg::*;
  import fsim_uvm_core_smoke_probe_pkg::*;

  logic observed_pass;
  int observed_payload;

  initial begin
    fsim_uvm_payload payload;
    fsim_uvm_core_smoke_test smoke;
    payload = new("source_payload");
    payload.value = 37;
    smoke = new("core_smoke", null);
    smoke.object_policy_smoke();
    smoke.factory_smoke();
    smoke.resource_smoke();
    smoke.configuration_smoke();
    smoke.command_line_smoke();
    smoke.reporting_smoke();
    smoke.callback_smoke();
    smoke.test_selection_smoke();
    smoke.topology_smoke();
    smoke.timeout_smoke();
    smoke.seed_smoke();
    observed_payload = payload.value + 5;
    observed_pass = observed_payload == 42 && smoke.completed_suites == 11;
    $display("FSIM-UVM-PHASE-TLM-SOURCE payload=%0d result=%0d pass=%0d",
             payload.value, observed_payload, observed_pass);
    $display("FSIM-UVM-CORE-SMOKE-SOURCE suites=%0d pass=%0d",
             smoke.completed_suites, observed_pass);
  end
endmodule
