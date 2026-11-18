// fsim: top=sv_array_method_locator_expressions std=2017
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 7.12.1: min, max, unique and unique_index take any
// expression of the iterator as their with clause (arithmetic, negation,
// remainder) and compare string and real elements, or the values of the
// expression, with their relational operators. min and max keep the first of
// equal values; unique keeps the first element of each distinct value.
module sv_array_method_locator_expressions;
  int failures;

  task automatic check_int(input string what, input longint got, input longint want);
    if (got != want) begin
      $display("FAIL %s: got %0d want %0d", what, got, want);
      failures++;
    end
  endtask

  task automatic check_str(input string what, input string got, input string want);
    if (got != want) begin
      $display("FAIL %s: got \"%s\" want \"%s\"", what, got, want);
      failures++;
    end
  endtask

  task automatic check_real(input string what, input real got, input real want);
    if (got != want) begin
      $display("FAIL %s: got %f want %f", what, got, want);
      failures++;
    end
  endtask

  task automatic integers();
    int q[$] = '{5, 3, 9, 6, 1};
    int r[$];
    int idx[$];
    // Keys of item % 4: 1 3 1 2 1.
    r = q.min() with (item % 4);
    check_int("min rem size", r.size(), 1);
    check_int("min rem", r[0], 5);
    r = q.max() with (item % 4);
    check_int("max rem size", r.size(), 1);
    check_int("max rem", r[0], 3);
    // Keys of -item: -5 -3 -9 -6 -1.
    r = q.min() with (-item);
    check_int("min neg", r[0], 9);
    r = q.max() with (-item);
    check_int("max neg", r[0], 1);
    // Keys of item % 3: 2 0 0 0 1.
    r = q.unique() with (item % 3);
    check_int("unique size", r.size(), 3);
    check_int("unique 0", r[0], 5);
    check_int("unique 1", r[1], 3);
    check_int("unique 2", r[2], 1);
    idx = q.unique_index() with (item % 3);
    check_int("unique_index size", idx.size(), 3);
    check_int("unique_index 0", idx[0], 0);
    check_int("unique_index 1", idx[1], 1);
    check_int("unique_index 2", idx[2], 4);
    q.delete();
    r = q.min() with (item % 4);
    check_int("empty min", r.size(), 0);
    r = q.unique() with (item % 4);
    check_int("empty unique", r.size(), 0);
  endtask

  task automatic strings();
    string s[$] = '{"b", "c", "a", "b"};
    string w[$] = '{"ccc", "a", "bb", "dd"};
    string r[$];
    int idx[$];
    r = s.min();
    check_int("smin size", r.size(), 1);
    check_str("smin", r[0], "a");
    r = s.max();
    check_str("smax", r[0], "c");
    r = s.unique();
    check_int("sunique size", r.size(), 3);
    check_str("sunique 0", r[0], "b");
    check_str("sunique 1", r[1], "c");
    check_str("sunique 2", r[2], "a");
    idx = s.unique_index();
    check_int("sunique_index size", idx.size(), 3);
    check_int("sunique_index 2", idx[2], 2);
    // Lengths of w: 3 1 2 2.
    r = w.min() with (item.len());
    check_str("smin len", r[0], "a");
    r = w.max() with (item.len());
    check_str("smax len", r[0], "ccc");
    r = w.unique() with (item.len());
    check_int("sunique len size", r.size(), 3);
    check_str("sunique len 2", r[2], "bb");
  endtask

  task automatic reals();
    real q[$] = '{2.5, -1.5, 3.5, -1.5};
    real r[$];
    int idx[$];
    // Squares: 6.25 2.25 12.25 2.25.
    r = q.min() with (item * item);
    check_int("rmin size", r.size(), 1);
    check_real("rmin", r[0], -1.5);
    r = q.max() with (item * item);
    check_real("rmax", r[0], 3.5);
    r = q.unique() with (item * item);
    check_int("runique size", r.size(), 3);
    check_real("runique 1", r[1], -1.5);
    idx = q.unique_index() with (item * item);
    check_int("runique_index 2", idx[2], 2);
  endtask

  initial begin
    integers();
    strings();
    reals();
    if (failures == 0) $display("PASS");
    $finish;
  end
endmodule
