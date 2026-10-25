// fsim: top=mixed_vhdl_delta_after_sv_write before=mixed_vhdl_delta_after_sv_write_ram.vhd.in
// SPDX-License-Identifier: Apache-2.0
// IEEE 1076-2019 14.7.5.3: a VHDL process woken by a signal change runs in
// the next delta cycle, whatever the writer's language. A SystemVerilog clock
// edge wakes the VHDL counter and the VHDL read port together, so the read
// port samples the counter's value from before the edge: rdata is the table
// entry of the previous address.
module mixed_vhdl_delta_after_sv_write;
  reg clk = 1'b0;
  wire [7:0] rdata;
  wire [3:0] addr;
  integer cycle;
  integer errors = 0;

  mixed_delta_ram ram (.clk(clk), .rdata(rdata), .addr_o(addr));

  always #5 clk = ~clk;

  initial begin
    for (cycle = 0; cycle < 6; cycle = cycle + 1) begin
      @(posedge clk);
      #1;
      if (addr !== cycle + 1 || rdata !== cycle * 3) begin
        $display("cycle %0d: addr=%0d rdata=%0d, expected addr=%0d rdata=%0d",
                 cycle, addr, rdata, cycle + 1, cycle * 3);
        errors = errors + 1;
      end
    end
    if (errors == 0)
      $display("PASS");
    $finish;
  end
endmodule
