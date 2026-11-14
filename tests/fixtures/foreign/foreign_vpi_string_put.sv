// SPDX-License-Identifier: Apache-2.0
// A string variable that a VPI plug-in writes with zero-delay inertial puts
// (foreign_vpi_string_put_plugin.c): every put that changes the value is an
// event its event control waits for (IEEE 1800-2023 9.4.2, 38.34).
module top;
  string s;
  int sum;
  always @(s) begin
    sum = 0;
    for (int i = 0; i < s.len(); i++) sum += s[i];
    $display("#c t=%0t s=%s sum=%0d", $time, s, sum);
  end
endmodule
