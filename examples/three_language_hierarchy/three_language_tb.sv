// SPDX-License-Identifier: Apache-2.0
module three_language_tb;
  logic stimulus;
  logic bridge_value;
  logic observed;

  // The HDL hierarchy owns both cross-language children. The public SystemC
  // factory and the VHDL entity resolve through the models search library.
  mixed_bridge u_bridge (
    .source(stimulus),
    .result(bridge_value)
  );
  LogicStage u_vhdl (
    .value(bridge_value),
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
