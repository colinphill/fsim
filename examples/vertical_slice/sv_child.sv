// SPDX-License-Identifier: Apache-2.0
module sv_child (
  input  logic [7:0] value,
  output logic [7:0] inverted
);
  assign inverted = ~value;
endmodule
