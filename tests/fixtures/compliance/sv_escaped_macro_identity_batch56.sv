// fsim: top=sv_escaped_macro_identity_batch56 std=2017
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2017 5.6.1, 22.5.1: escaped and simple macro identities agree.
`define ordinary 7
`define \escaped  11
`define \quoted (x) `"`\`"x`\`"`"
`define \punctuation+  13
`ifdef \ordinary
`define selected 1
`else
`define selected 0
`endif
`undef \ordinary
`ifndef ordinary
`define removed 1
`else
`define removed 0
`endif
`define ordinary 17
module sv_escaped_macro_identity_batch56;
  initial begin
    if (`\ordinary != 17 || `escaped != 11 || `\escaped != 11)
      $fatal(1, "escaped macro lookup differs");
    if (`\punctuation+ != 13 || `selected != 1 || `removed != 1)
      $fatal(1, "escaped macro conditional or undef differs");
    if (`quoted(text) != "\"text\"" || `\quoted (value) != "\"value\"")
      $fatal(1, "escaped function-like macro differs");
    $display("PASS");
  end
endmodule
