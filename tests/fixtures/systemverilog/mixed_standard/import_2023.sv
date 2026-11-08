// SPDX-License-Identifier: Apache-2.0
// Compiled with --standard 2023. Derivation: inc(41) = 42 (1800-2023 26.3
// import of the typedef, localparam and function), K = 5.
module tb_import;
  import p17::*;
  byte_t x;
  initial begin
    x = inc(8'd41);
    $display("MIX import %0d %0d", x, K);
  end
endmodule
