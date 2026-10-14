// fsim: top=sv_package_unknown_integral_constant_batch58 std=2017
// SPDX-License-Identifier: Apache-2.0
// A package localparam can be a constant four-state value even when an
// integral-only evaluator cannot return an int64 result.
package sv_package_unknown_integral_constant_pkg;
  parameter p1 = 1;
  localparam p2 = p1 + 'bx;
endpackage

module sv_package_unknown_integral_constant_batch58;
  import sv_package_unknown_integral_constant_pkg::p2;

  initial begin
    if ((^p2) !== 1'bx) $fatal(1, "package X constant was not preserved");
    $display("PASS");
    $finish;
  end
endmodule
