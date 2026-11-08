// SPDX-License-Identifier: Apache-2.0
// $scope takes a hierarchical name (IEEE 1800-2023 D.11), not a value, so a
// literal is refused when the design is elaborated.
module top;
  initial begin
    $scope(1);
  end
endmodule
