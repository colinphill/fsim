// SPDX-License-Identifier: Apache-2.0
//
// A VHDL clock clocks a SystemVerilog flop and a VHDL flop (the VHDL top,
// mixed_clocked_port.vhd). The SystemVerilog flop's nonblocking update of q
// reaches the VHDL signal q, which the VHDL flop samples on the same edge:
// the VHDL processes the edge resumes read the signal values of the start
// of that simulation cycle (IEEE 1076-2019 14.7.5.3), and the update of q
// is a later one (10.5.2.2, 14.7.3), so r runs a cycle behind q. The other
// way, the VHDL flop's update of p is not visible to the SystemVerilog flop
// that the edge wakes (IEEE 1800-2023 4.4, 9.4.2; 1076-2019 14.7.3), so s
// runs a cycle behind p.
timeunit 1ps / 1ps;
module mixed_clocked_leaf(input clk, input [7:0] d, output reg [7:0] q,
    input [7:0] p, output reg [7:0] s);
  always @(posedge clk) begin
    q <= d;
    s <= p;
  end
endmodule
