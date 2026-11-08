// SPDX-License-Identifier: Apache-2.0
// Compiled with --standard 2017. Derivation: "banana" holds three 'a'.
module tb_call;
  import p23::*;
  initial $display("MIX call %0d", count_a("banana"));
endmodule
