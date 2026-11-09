// SPDX-License-Identifier: Apache-2.0
// The package variables of the UVM 1.2 library that hold its singletons
// (uvm_top, uvm_cmdline_proc, uvm_test_done, uvm_resources, 1800.2-2020's
// predecessor UVM 1.2 base/uvm_root.svh, uvm_cmdline_processor.svh,
// uvm_globals.svh, uvm_resource.svh): each is initialized, in declaration
// order, with the result of its getter, which the native services answer, so
// a direct read gives the same live handle as the getter. uvm_default_printer
// is initialized with the handle of uvm_default_table_printer. Expected
// output, derived by hand: all five checks hold, so the five lines print once
// each, in this order, at time 0.
import uvm_pkg::*;

module top;
  initial begin
    if (uvm_top == null) $fatal(1, "uvm_top is null");
    if (uvm_top == uvm_root::get()) $display("uvm_top is the root");
    if (uvm_cmdline_proc == uvm_cmdline_processor::get_inst())
      $display("uvm_cmdline_proc is the processor");
    if (uvm_test_done != null) $display("uvm_test_done is live");
    if (uvm_resources == uvm_resource_pool::get())
      $display("uvm_resources is the pool");
    if (uvm_default_printer == uvm_default_table_printer)
      $display("uvm_default_printer is the table printer");
  end
endmodule
