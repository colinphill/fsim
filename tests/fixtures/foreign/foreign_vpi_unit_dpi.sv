// SPDX-License-Identifier: Apache-2.0
// A DPI import declared in the compilation-unit scope is a function of the
// `$unit` package (IEEE 1800-2023 3.12.1, 37.10, 37.42).
import "DPI-C" function int unit_add(input int a, input int b);
module top;
  initial begin : main
    #1;
  end
endmodule
