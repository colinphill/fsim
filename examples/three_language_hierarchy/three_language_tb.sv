// SPDX-License-Identifier: Apache-2.0
module three_language_tb;
  logic stimulus;
  logic observed;

  // The manifest binds this placeholder to the SystemC factory. That factory
  // declares the VHDL child below it during recursive elaboration.
  mixed_bridge_placeholder u_bridge (
    .source(stimulus),
    .result(observed)
  );

  initial begin
    stimulus = 1'b0;
    #1;
    if (observed !== stimulus)
      $fatal(1, "three-language mismatch for stimulus 0");
    $display("SV observed %b after the SystemC -> VHDL path", observed);

    stimulus = 1'b1;
    #1;
    if (observed !== stimulus)
      $fatal(1, "three-language mismatch for stimulus 1");
    $display("SV observed %b after the SystemC -> VHDL path", observed);

    stimulus = 1'b0;
    #1;
    if (observed !== stimulus)
      $fatal(1, "three-language mismatch for final stimulus 0");
    $display("PASS: SystemVerilog -> SystemC -> VHDL hierarchy");
    $finish;
  end
endmodule
