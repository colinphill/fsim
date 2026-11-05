// SPDX-License-Identifier: Apache-2.0
// Top-level real parameter overrides (IEEE 1800-2023 23.10, 6.20.2): a real
// literal for real types and an untyped parameter, an integer converted to
// real, and a real rounded for an integral parameter.
module top #(
    parameter real R = 0.0,
    parameter shortreal S = 0.0,
    parameter U = 0,
    parameter int N = 0,
    parameter realtime RT = 0.0
);
  initial begin
    $display("#c R=%0.3f", R);
    $display("#c S=%0.3f", S);
    $display("#c U=%0.3f", U);
    $display("#c N=%0d", N);
    $display("#c RT=%0.3f", RT);
  end
endmodule
