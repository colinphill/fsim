// fsim: top=mixed_vhdl_flop_after_sv_flop before=mixed_vhdl_flop_after_sv_flop_stage.vhd.in
// SPDX-License-Identifier: Apache-2.0
// An SV flop feeds a VHDL flop on the same SystemVerilog clock. The VHDL
// process that the clock edge wakes runs in the same Active region as the
// SV flop, before its nonblocking update, so it samples the SV flop's old
// value, as a second SV flop would. No standard orders zero-delay events
// across languages; this is the order Vivado gives (S16). The clock is
// written blocking, nonblocking, and through a continuous assignment.
module mixed_flop_sv_stage(input clk, input [7:0] d, output reg [7:0] q);
  always @(posedge clk) q <= d;
endmodule

module mixed_vhdl_flop_after_sv_flop;
  reg blocking_clk = 1'b0;
  reg nonblocking_clk = 1'b0;
  wire assigned_clk;
  reg [7:0] d = 8'h00;
  wire [7:0] m0, m1, m2, q0, q1, q2;
  integer errors = 0;

  assign assigned_clk = blocking_clk;

  mixed_flop_sv_stage s0(.clk(blocking_clk), .d(d), .q(m0));
  mixed_flop_vhdl_stage v0(.clk(blocking_clk), .d(m0), .q(q0));
  mixed_flop_sv_stage s1(.clk(nonblocking_clk), .d(d), .q(m1));
  mixed_flop_vhdl_stage v1(.clk(nonblocking_clk), .d(m1), .q(q1));
  mixed_flop_sv_stage s2(.clk(blocking_clk), .d(d), .q(m2));
  mixed_flop_vhdl_stage v2(.clk(assigned_clk), .d(m2), .q(q2));

  task check(input [7:0] m, input [7:0] q, input [7:0] want_m,
             input [7:0] want_q, input integer stage);
    if (m !== want_m || q !== want_q) begin
      $display("stage %0d: m=%h q=%h, expected m=%h q=%h",
               stage, m, q, want_m, want_q);
      errors = errors + 1;
    end
  endtask

  initial begin
    #5 d = 8'h11;
    #5 blocking_clk = 1'b1;
    nonblocking_clk <= 1'b1;
    #1;
    check(m0, q0, 8'h11, 8'hxx, 0);
    check(m1, q1, 8'h11, 8'hxx, 1);
    check(m2, q2, 8'h11, 8'hxx, 2);
    #4 blocking_clk = 1'b0;
    nonblocking_clk <= 1'b0;
    d = 8'h22;
    #5 blocking_clk = 1'b1;
    nonblocking_clk <= 1'b1;
    #1;
    check(m0, q0, 8'h22, 8'h11, 0);
    check(m1, q1, 8'h22, 8'h11, 1);
    check(m2, q2, 8'h22, 8'h11, 2);
    if (errors == 0)
      $display("PASS");
    $finish;
  end
endmodule
