// SPDX-License-Identifier: Apache-2.0
// The singleton getters of the UVM library (IEEE 1800.2-2020 F.3.1.2
// uvm_root::get, 12.3.4 uvm_cmdline_processor::get_inst, 10.5.2
// uvm_resource_pool::get): the native UVM services hold one object of each
// class, so a getter answers a live handle (not null) and a second call
// answers the same handle. Expected output, derived by hand: the four checks
// pass, so the four lines below print once each, in this order, at time 0.
import uvm_pkg::*;

module top;
  initial begin
    uvm_root root;
    uvm_cmdline_processor cmdline;
    uvm_resource_pool pool;
    root = uvm_root::get();
    cmdline = uvm_cmdline_processor::get_inst();
    pool = uvm_resource_pool::get();
    if (root != null) $display("root live");
    if (root == uvm_root::get()) $display("root unique");
    if (cmdline != null && cmdline == uvm_cmdline_processor::get_inst())
      $display("cmdline unique");
    if (pool != null && pool == uvm_resource_pool::get())
      $display("pool unique");
  end
endmodule
