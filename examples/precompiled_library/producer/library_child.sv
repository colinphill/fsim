// SPDX-License-Identifier: Apache-2.0
module library_child(
  input  logic value,
  output logic result
);
  assign result = ~value;
endmodule
