// SPDX-License-Identifier: Apache-2.0
// Compiled with --standard 2023. Derivation: leaf adds 3 to BASE = 10, so y
// is 13 one time unit after time 0.
module tb_verilog;
  import p12::*;
  wire [7:0] y;
  leaf u(.a(BASE), .y(y));
  initial begin
    #1 $display("MIX verilog %0d", y);
  end
endmodule
