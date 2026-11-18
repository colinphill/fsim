// fsim: top=sv_defparam_upward_names std=2017
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 23.8, 23.10.1: the first name of a defparam's hierarchical
// name is searched upward. From a generate block it names an instance or a
// generate block of the block itself or of an enclosing block of the same
// module (u2 from g, g.u2 from g, h.u5 from h), and the top-level instance
// by its name (sv_defparam_upward_names.g.h.u5). A defparam in a generate
// block may target any parameter inside that block; an instance array
// element, a loop generate iteration and a named block's parameter are
// path components like any scope. 23.10.1: of several defparams for one
// parameter the last one in the source text applies, so the module's
// `defparam lp[1].c.Q = 60` after the loop replaces the loop's 51.
module sv_defparam_upward_names_leaf #(parameter int Q = 1) ();
endmodule

module sv_defparam_upward_names_sub #(parameter int P = 1) ();
  sv_defparam_upward_names_leaf l();
endmodule

// The named block checks its own parameter, which the top's defparam sets.
module sv_defparam_upward_names_block;
  initial begin : b
    parameter int R = 1;
    if (R != 41) $display("FAIL named block parameter: got %0d, want 41", R);
  end
endmodule

module sv_defparam_upward_names;
  int failures;
  sv_defparam_upward_names_block bo();
  sv_defparam_upward_names_leaf arr[1:0] ();
  defparam bo.b.R = 41;
  defparam arr[1].Q = 42;
  generate if (1) begin : g
    sv_defparam_upward_names_sub u2();
    defparam u2.P = 7;
    defparam g.u2.l.Q = 8;
    if (1) begin : h
      sv_defparam_upward_names_sub u5();
      defparam u5.P = 22;
      defparam sv_defparam_upward_names.g.h.u5.l.Q = 23;
    end
  end endgenerate
  genvar i;
  for (i = 0; i < 2; i++) begin : lp
    sv_defparam_upward_names_leaf c();
    defparam c.Q = 50 + i;
  end
  defparam lp[1].c.Q = 60;

  task automatic check(input string what, input int got, input int want);
    if (got != want) begin
      $display("FAIL %s: got %0d, want %0d", what, got, want);
      failures++;
    end
  endtask

  initial begin
    check("instance array element", arr[1].Q, 42);
    check("other array element", arr[0].Q, 1);
    check("generate block instance", g.u2.P, 7);
    check("generate block name", g.u2.l.Q, 8);
    check("nested generate instance", g.h.u5.P, 22);
    check("top-level instance name", g.h.u5.l.Q, 23);
    check("loop iteration 0", lp[0].c.Q, 50);
    check("last defparam in source", lp[1].c.Q, 60);
    if (failures == 0) $display("PASS");
  end
endmodule
