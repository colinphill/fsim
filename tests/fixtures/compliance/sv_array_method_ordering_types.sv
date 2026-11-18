// fsim: top=sv_array_method_ordering_types std=2017
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 7.12.2: reverse takes elements of any type, and sort and
// rsort order string and real elements, and order by any with expression of
// the iterator, with the relational operators of the element or key type.
module sv_array_method_ordering_types;
  int failures;

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

  task automatic check_int(input string what, input longint got, input longint want);
    if (got != want) begin
      $display("FAIL %s: got %0d want %0d", what, got, want);
      failures++;
    end
  endtask

  task automatic strings();
    string s[$] = '{"b", "c", "a", "b"};
    string w[$] = '{"ccc", "a", "bb", "dd"};
    string x[$] = '{"x", "y", "z"};
    string fixed[3] = '{"z", "x", "y"};
    s.sort();
    check_str("sort 0", s[0], "a");
    check_str("sort 1", s[1], "b");
    check_str("sort 2", s[2], "b");
    check_str("sort 3", s[3], "c");
    s.rsort();
    check_str("rsort 0", s[0], "c");
    check_str("rsort 3", s[3], "a");
    x.reverse();
    check_str("reverse 0", x[0], "z");
    check_str("reverse 1", x[1], "y");
    check_str("reverse 2", x[2], "x");
    // Lengths of w: 3 1 2 2; equal keys keep their order.
    w.sort() with (item.len());
    check_str("sort len 0", w[0], "a");
    check_str("sort len 1", w[1], "bb");
    check_str("sort len 2", w[2], "dd");
    check_str("sort len 3", w[3], "ccc");
    w.rsort() with (item.len());
    check_str("rsort len 0", w[0], "ccc");
    check_str("rsort len 1", w[1], "bb");
    check_str("rsort len 2", w[2], "dd");
    check_str("rsort len 3", w[3], "a");
    fixed.sort();
    check_str("fixed 0", fixed[0], "x");
    check_str("fixed 1", fixed[1], "y");
    check_str("fixed 2", fixed[2], "z");
    x.shuffle();
    check_int("shuffle size", x.size(), 3);
    x.sort();
    check_str("shuffle sorted 0", x[0], "x");
    check_str("shuffle sorted 2", x[2], "z");
  endtask

  task automatic reals();
    real q[$] = '{2.5, -1.5, 3.5, -1.5};
    q.sort();
    check_real("sort 0", q[0], -1.5);
    check_real("sort 1", q[1], -1.5);
    check_real("sort 2", q[2], 2.5);
    check_real("sort 3", q[3], 3.5);
    q.rsort();
    check_real("rsort 0", q[0], 3.5);
    check_real("rsort 3", q[3], -1.5);
    q.reverse();
    check_real("reverse 0", q[0], -1.5);
    check_real("reverse 3", q[3], 3.5);
  endtask

  task automatic integers();
    int q[$] = '{5, 3, 9, 6, 1};
    // Keys of item % 4: 1 3 1 2 1; equal keys keep their order.
    q.sort() with (item % 4);
    check_int("rem 0", q[0], 5);
    check_int("rem 1", q[1], 9);
    check_int("rem 2", q[2], 1);
    check_int("rem 3", q[3], 6);
    check_int("rem 4", q[4], 3);
    q.sort() with (-item);
    check_int("neg 0", q[0], 9);
    check_int("neg 4", q[4], 1);
  endtask

  initial begin
    strings();
    reals();
    integers();
    if (failures == 0) $display("PASS");
    $finish;
  end
endmodule
