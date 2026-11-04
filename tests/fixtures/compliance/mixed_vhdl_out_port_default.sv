// fsim: top=mixed_vhdl_out_port_default before=mixed_vhdl_out_port_default_source.vhd.in
// SPDX-License-Identifier: Apache-2.0
// A VHDL out port that nothing inside its entity drives is a source of its
// SystemVerilog actual with the port's default as its driving value, or
// T'LEFT without one (IEEE 1076-2019 6.5.2, 14.7.3.2): 'U' reads as x, a
// default '0' as 0, on a net and on a variable alike.
module mixed_vhdl_out_port_default;
  wire n0, n1, b1;
  wire [1:0] n2;
  logic v0, v1;
  logic [1:0] v2;
  integer errors = 0;

  mixed_vhdl_out_port_default_source nets(
    .q0(n0), .q1(n1), .q2(n2), .b1(b1));
  mixed_vhdl_out_port_default_source variables(
    .q0(v0), .q1(v1), .q2(v2), .b1());

  task check(input [1:0] got, input [1:0] want, input integer index);
    if (got !== want) begin
      $display("port %0d: %b, expected %b", index, got, want);
      errors = errors + 1;
    end
  endtask

  initial begin
    #1;
    check({1'b0, n0}, 2'b0x, 0);
    check({1'b0, n1}, 2'b00, 1);
    check(n2, 2'b10, 2);
    check({1'b0, b1}, 2'b01, 3);
    check({1'b0, v0}, 2'b0x, 4);
    check({1'b0, v1}, 2'b00, 5);
    check(v2, 2'b10, 6);
    if (errors == 0)
      $display("PASS");
    $finish;
  end
endmodule
