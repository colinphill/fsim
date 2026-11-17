// fsim: top=sv_process_typedef_receivers std=2017
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 9.7, 6.18: process methods called on an element of an
// array whose element type is a typedef of the process class.
module sv_process_typedef_receivers;
  typedef process pr;
  pr p[2];
  int order;
  int failures;

  initial begin
    wait (p[1] != null);
    p[1].await();
    if (order != 1) begin
      $display("FAIL await order=%0d", order);
      failures++;
    end
    p[0] = process::self();
    if (failures == 0) begin
      $display("PASS");
    end
    $finish;
  end

  initial begin
    p[1] = process::self();
    #2 order = 1;
  end
endmodule
