// fsim: top=sv_defparam_elaboration_order std=2017
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 23.10.1, 23.10.4.1: a defparam that is not in or under a
// generate block or instance array may change any parameter of the
// hierarchy: an ancestor's instance (top.u1.P from inside md), the
// instance's own parameter (`defparam ..._mid.Z` names md by its module
// name, Syntax 23-8) and a sibling's descendant (top.u9.l.Q). The values
// are final before the generate schemes are evaluated, so md's generate
// condition sees Z = 5, and a defparam's value is evaluated in the scope of
// the defparam (Z + 100 = 105, also for a grandchild target, mg.u.l.Q).
module sv_defparam_elaboration_order_leaf #(parameter int Q = 1) ();
endmodule

module sv_defparam_elaboration_order_sub #(parameter int P = 1) ();
  sv_defparam_elaboration_order_leaf l();
endmodule

module sv_defparam_elaboration_order_mid;
  parameter int Z = 1;
  defparam sv_defparam_elaboration_order.u1.P = 11;
  defparam sv_defparam_elaboration_order_mid.Z = 5;
  defparam sv_defparam_elaboration_order.u9.l.Q = Z + 100;
  if (Z == 5) begin : gz
    sv_defparam_elaboration_order_leaf gl();
  end
endmodule

module sv_defparam_elaboration_order_grand;
  parameter int T = 1;
  sv_defparam_elaboration_order_sub u();
  defparam u.l.Q = T + 100;
endmodule

module sv_defparam_elaboration_order;
  int failures;
  sv_defparam_elaboration_order_sub u1();
  sv_defparam_elaboration_order_mid md();
  sv_defparam_elaboration_order_sub u9();
  sv_defparam_elaboration_order_grand #(.T(5)) mg();

  task automatic check(input string what, input int got, input int want);
    if (got != want) begin
      $display("FAIL %s: got %0d, want %0d", what, got, want);
      failures++;
    end
  endtask

  initial begin
    check("ancestor's instance", u1.P, 11);
    check("own parameter by module name", md.Z, 5);
    check("generate sees the final value", md.gz.gl.Q, 1);
    check("sibling's descendant", u9.l.Q, 105);
    check("other instance unchanged", u9.P, 1);
    check("grandchild value in the defparam's scope", mg.u.l.Q, 105);
    if (failures == 0) $display("PASS");
  end
endmodule
