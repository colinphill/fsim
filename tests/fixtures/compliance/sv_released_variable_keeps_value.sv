// fsim: top=sv_released_variable_keeps_value
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 10.6.2: a released variable that no continuous assignment
// drives keeps its forced value until its next procedural assignment; writes
// made while the force is in effect are lost.
module sv_released_variable_keeps_value;
  logic [3:0] v, w;
  logic c = 0;
  logic [3:0] d = 4'd2;
  bit ok = 1;
  always @(posedge c) w = d;
  initial begin
    v = 1; w = 0;
    #1 force v = 5; force w = 6;
    #1 v = 9; d = 4'd11; c = 1;
    #1 c = 0; release v; release w;
    #0 if (v !== 4'd5 || w !== 4'd6) begin
      $display("FAIL release v=%0d w=%0d", v, w);
      ok = 0;
    end
    #1 v = 3; d = 4'd12; c = 1;
    #1 if (v !== 4'd3 || w !== 4'd12) begin
      $display("FAIL assign v=%0d w=%0d", v, w);
      ok = 0;
    end
    if (ok) $display("PASS");
    $finish;
  end
endmodule
