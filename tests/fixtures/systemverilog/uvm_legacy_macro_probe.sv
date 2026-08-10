// SPDX-License-Identifier: Apache-2.0
`include "uvm_macros.svh"

package fsim_uvm_legacy_macro_probe_pkg;
  import uvm_pkg::*;

  class fsim_uvm_legacy_callback extends uvm_callback;
    function new(string name = "fsim_uvm_legacy_callback");
      super.new(name);
    endfunction

    virtual function void invoked();
    endfunction

    `uvm_object_utils(fsim_uvm_legacy_callback)
  endclass

  class fsim_uvm_legacy_item extends uvm_sequence_item;
    rand int value;
    string label;

    function new(string name = "fsim_uvm_legacy_item");
      super.new(name);
    endfunction

    `uvm_object_utils_begin(fsim_uvm_legacy_item)
      `uvm_field_int(value, UVM_ALL_ON)
      `uvm_field_string(label, UVM_DEFAULT)
    `uvm_object_utils_end
  endclass

  class fsim_uvm_legacy_sequence extends uvm_sequence #(fsim_uvm_legacy_item);
    function new(string name = "fsim_uvm_legacy_sequence");
      super.new(name);
    endfunction

    virtual task body();
      fsim_uvm_legacy_item request;
      `uvm_create(request)
      `uvm_do_pri_with(request, 7, { value > 0; })
      `uvm_send(request)
      `uvm_rand_send_with(request, { value < 32; })
    endtask

    `uvm_object_utils(fsim_uvm_legacy_sequence)
  endclass

  class fsim_uvm_legacy_reg extends uvm_reg;
    function new(string name = "fsim_uvm_legacy_reg");
      super.new(name, 32, 0);
    endfunction

    `uvm_object_utils(fsim_uvm_legacy_reg)
  endclass

  class fsim_uvm_legacy_component extends uvm_component;
    function new(string name = "fsim_uvm_legacy_component",
                 uvm_component parent = null);
      super.new(name, parent);
    endfunction

    function void callback_probe();
      `uvm_do_callbacks(fsim_uvm_legacy_component,
                        fsim_uvm_legacy_callback, invoked())
    endfunction

    function void report_probe();
      `uvm_info("LEGACY_INFO", "message", UVM_LOW)
      `uvm_warning("LEGACY_WARNING", "message")
      `uvm_error("LEGACY_ERROR", "message")
      `uvm_fatal("LEGACY_FATAL", "message")
    endfunction

    `uvm_component_utils(fsim_uvm_legacy_component)
    `uvm_register_cb(fsim_uvm_legacy_component, fsim_uvm_legacy_callback)
  endclass

  `uvm_analysis_imp_decl(_fsim_legacy)
endpackage
