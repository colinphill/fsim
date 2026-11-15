// fsim: top=sv_statement_labels std=2017
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 9.3.5: a label may precede any procedural statement. On a
// foreach loop, or a for loop that declares its variables, it names the
// loop's implicit block (12.7.1, 12.7.3); on any other statement it makes a
// named begin-end block around the statement. A labeled statement can be
// disabled like a named block (9.6.2), and a break in a foreach loop over
// several dimensions leaves the whole loop (12.8).
`define LABELED_FOREACH(a) mfl: foreach (a[i, j]) begin : mblk \
    static int mv = 0; mv++; end
module sv_statement_labels;
  int arr[3] = '{1, 2, 3};
  int a2[2][3];
  int n;
  int failures;
  string where;

  task automatic check(input string what, input int got, input int want);
    if (got != want) begin
      $display("FAIL %s: got %0d, want %0d", what, got, want);
      failures++;
    end
  endtask

  initial begin
    lbl: for (int k = 0; k < 3; k++) begin : lb
      static int w = 0;
      w = w + k;
    end
    check("labeled for", sv_statement_labels.lbl.lb.w, 3);

    fl: foreach (arr[i]) begin : blk
      static int v = 0;
      v += arr[i];
    end
    check("labeled foreach", sv_statement_labels.fl.blk.v, 6);

    st: where = $sformatf("%m");
    if (where != "sv_statement_labels.st") begin
      $display("FAIL labeled statement scope: %s", where);
      failures++;
    end

    lw: while (n < 5) begin
      n++;
      if (n == 2) disable lw;
    end
    check("disable labeled while", n, 2);

    lf: for (int q = 0; q < 10; q++) begin
      if (q == 3) disable lf;
      n++;
    end
    check("disable labeled for", n, 5);

    m2: foreach (a2[i, j]) begin : b2
      static int c = 0;
      c++;
      if (i == 1) break;
    end
    check("break labeled 2-D foreach", sv_statement_labels.m2.b2.c, 4);

    n = 0;
    foreach (a2[i, j]) begin
      n++;
      if (i == 1 && j == 1) break;
    end
    check("break 2-D foreach", n, 5);

    `LABELED_FOREACH(a2)
    check("labeled foreach from a macro", sv_statement_labels.mfl.mblk.mv, 6);

    // A for loop with several initializations is one implicit block that
    // declares every variable; they run in order, once (12.7.1).
    n = 0;
    ml: for (int x = 1, y = x + 4; x < y; x++, y--) begin : mb
      static int z = 0;
      z = z + y;
      n = n + 1;
    end
    check("multi-initializer for iterations", n, 2);
    check("multi-initializer for body", sv_statement_labels.ml.mb.z, 9);

    if (failures == 0) $display("PASS");
  end
endmodule
