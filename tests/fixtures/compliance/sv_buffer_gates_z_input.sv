// fsim: top=sv_buffer_gates_z_input
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2017 28.5, 28.6: buf and an enabled bufif1 drive x for a z
// input; a disabled bufif1 drives z.
`timescale 1ns/1ns
module sv_buffer_gates_z_input;
  logic data;
  logic enable;
  wire buffered;
  wire tristate;
  int failures;

  buf (buffered, data);
  bufif1 (tristate, data, enable);

  initial begin
    data = 1'bz;
    enable = 1'b1;
    #1;
    if (buffered !== 1'bx) failures++;
    if (tristate !== 1'bx) failures++;
    data = 1'b1;
    #1;
    if (buffered !== 1'b1 || tristate !== 1'b1) failures++;
    enable = 1'b0;
    #1;
    if (tristate !== 1'bz) failures++;
    if (failures == 0) $display("PASS");
    else $display("FAIL count %0d", failures);
  end
endmodule
