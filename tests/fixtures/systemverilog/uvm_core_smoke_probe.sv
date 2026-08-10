// SPDX-License-Identifier: Apache-2.0
package fsim_uvm_core_smoke_probe_pkg;
  import uvm_pkg::*;

  class fsim_uvm_core_smoke_callback extends uvm_callback;
    function new(string name = "fsim_uvm_core_smoke_callback");
      super.new(name);
    endfunction
  endclass

  class fsim_uvm_core_smoke_test extends uvm_test;
    uvm_printer printer_policy;
    uvm_comparer comparer_policy;
    uvm_packer packer_policy;
    uvm_recorder recorder_policy;
    uvm_factory factory;
    uvm_resource_base resource;
    uvm_component configuration_context;
    uvm_cmdline_processor command_line;
    uvm_report_server report_server;
    fsim_uvm_core_smoke_callback callback;
    int completed_suites;

    function new(string name = "fsim_uvm_core_smoke_test",
                 uvm_component parent = null);
      super.new(name, parent);
      completed_suites = 0;
    endfunction

    function void object_policy_smoke();
      completed_suites += 1;
    endfunction

    function void factory_smoke();
      completed_suites += 1;
    endfunction

    function void resource_smoke();
      completed_suites += 1;
    endfunction

    function void configuration_smoke();
      completed_suites += 1;
    endfunction

    function void command_line_smoke();
      completed_suites += 1;
    endfunction

    function void reporting_smoke();
      completed_suites += 1;
    endfunction

    function void callback_smoke();
      completed_suites += 1;
    endfunction

    function void test_selection_smoke();
      completed_suites += 1;
    endfunction

    function void topology_smoke();
      completed_suites += 1;
    endfunction

    function void timeout_smoke();
      completed_suites += 1;
    endfunction

    function void seed_smoke();
      completed_suites += 1;
    endfunction
  endclass
endpackage
