// SPDX-License-Identifier: Apache-2.0
package fsim_uvm_phase_tlm_example_pkg;
  import uvm_pkg::uvm_object;
  import uvm_pkg::uvm_component;
  import uvm_pkg::uvm_phase;
  import uvm_pkg::uvm_blocking_put_port;
  import uvm_pkg::uvm_tlm_fifo;

  class fsim_uvm_payload extends uvm_object;
    int value;

    function new(string name = "fsim_uvm_payload");
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

  logic observed_pass;
  int observed_payload;

  initial begin
    fsim_uvm_payload payload;
    payload = new("source_payload");
    payload.value = 37;
    observed_payload = payload.value + 5;
    observed_pass = observed_payload == 42;
    $display("FSIM-UVM-PHASE-TLM-SOURCE payload=%0d result=%0d pass=%0d",
             payload.value, observed_payload, observed_pass);
  end
endmodule
