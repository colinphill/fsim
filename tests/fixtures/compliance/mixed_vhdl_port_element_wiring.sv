// fsim: top=mixed_vhdl_port_element_wiring before=mixed_vhdl_port_element_wiring_block.vhd.in
// SPDX-License-Identifier: Apache-2.0
// IEEE 1076-2008 6.5.7.1, 14.7.3.2, 14.7.3.3 in a mixed design: a VHDL
// formal associated with an element of its actual follows (in) or drives
// (out) the element in the same simulation cycle. The SystemVerilog top
// starts the VHDL block, which checks the deltas itself.
module mixed_vhdl_port_element_wiring;
  reg go = 1'b0;
  wire [3:0] lane1;
  mixed_vhdl_port_element_wiring_block block (.go(go), .lane1(lane1));
  initial begin
    #1 go = 1'b1;
    #5;
    if (lane1 !== 4'h6) $display("FAIL lane1=%h", lane1);
    $finish;
  end
endmodule
