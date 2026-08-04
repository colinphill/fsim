// SPDX-License-Identifier: Apache-2.0
module tb;
  logic value;
  logic result;

  library_child u_library_child(
    .value(value),
    .result(result)
  );

  initial begin
    value = 1'b0;
    #1;
    value = 1'b1;
    #1;
    $finish;
  end
endmodule
