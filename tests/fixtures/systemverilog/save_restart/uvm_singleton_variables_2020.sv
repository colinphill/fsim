// SPDX-License-Identifier: Apache-2.0
// The package variables of the UVM 1800.2-2020.3.1 library that hold
// singletons: uvm_top and uvm_cmdline_proc are plain variables (uvm_root.svh,
// uvm_cmdline_processor.svh) that the getters assign when they make the
// object (uvm_root::m_uvm_get_root: `uvm_top = top`;
// uvm_cmdline_processor::get_inst: `uvm_cmdline_proc = m_inst`). The native
// services make both objects, so the getter stores the handle into the
// variable as its source does. After the getters ran, each variable is
// non-null and equals the getter's result. Expected output, derived by hand:
// both checks hold, so the two lines print once each, in this order, at time
// 0.
import uvm_pkg::*;

module top;
  uvm_root r;
  uvm_cmdline_processor c;
  initial begin
    r = uvm_root::get();
    if (uvm_top == null) $fatal(1, "uvm_top is null");
    if (uvm_top == r) $display("uvm_top is the root");
    c = uvm_cmdline_processor::get_inst();
    if (uvm_cmdline_proc == null) $fatal(1, "uvm_cmdline_proc is null");
    if (uvm_cmdline_proc == c) $display("uvm_cmdline_proc is the processor");
  end
endmodule
