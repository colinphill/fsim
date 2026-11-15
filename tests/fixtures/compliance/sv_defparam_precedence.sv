// fsim: top=sv_defparam_precedence std=2017
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 23.10: a defparam that targets a parameter the instance
// also assigns takes precedence over the instance parameter value
// assignment. 23.10.1: of several defparams for one parameter, the last
// one in the source text applies.
module sv_defparam_precedence_leaf;
  parameter int P = 0;
endmodule

module sv_defparam_precedence;
  int failures;

  sv_defparam_precedence_leaf #(.P(7)) over ();
  sv_defparam_precedence_leaf #(5) ordered ();
  sv_defparam_precedence_leaf twice ();

  defparam over.P = 8;
  defparam ordered.P = 6;
  defparam twice.P = 1;
  defparam twice.P = 2;

  task automatic check(input string what, input int got, input int want);
    if (got != want) begin
      $display("FAIL %s: got %0d, want %0d", what, got, want);
      failures++;
    end
  endtask

  initial begin
    check("defparam over named override", over.P, 8);
    check("defparam over ordered override", ordered.P, 6);
    check("last defparam", twice.P, 2);
    if (failures == 0) $display("PASS");
  end
endmodule
