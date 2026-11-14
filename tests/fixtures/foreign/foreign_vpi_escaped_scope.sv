// SPDX-License-Identifier: Apache-2.0
// A module whose variable has an escaped identifier and an initializer next
// to a generate block: its scopes and variables iterate in full, the
// escaped name keeping its terminating white space (IEEE 1800-2023 5.6.1,
// 37.10, 37.12).
module top;
  genvar i;
  for (i = 0; i < 2; i = i + 1) begin : g
    reg r;
  end
  reg [3:0] \(.*|regex) = 0;
  reg plain = 1'b1;
endmodule
