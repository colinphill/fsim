// fsim: top=sv_array_method_sort_keyed_large
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 7.12.2: sort() and rsort() with a with expression order
// the elements by the expression's value with the iterator bound to each
// element, ascending for sort and descending for rsort; the elements
// themselves are only reordered. Large arrays check the order of every
// adjacent pair and that each value keeps its count; small arrays check
// hand-derived orders (no two keys equal, as 7.12.2 does not fix the order
// of equal keys).
module sv_array_method_sort_keyed_large;
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

  // Element values are (i * 7919) % 1000; counts[v] is how often v occurs
  // before the sort less how often after.
  task automatic counts_zero(input string what, input int counts[1000]);
    int wrong;
    wrong = 0;
    foreach (counts[v]) if (counts[v] != 0) wrong++;
    check_int({what, " counts"}, wrong, 0);
  endtask

  task automatic large_fixed();
    int a[4096];
    int counts[1000];
    int unordered;
    foreach (a[i]) begin
      a[i] = (i * 7919) % 1000;
      counts[a[i]]++;
    end
    a.sort() with (item % 7);
    unordered = 0;
    for (int i = 0; i + 1 < 4096; i++)
      if (a[i] % 7 > a[i + 1] % 7) unordered++;
    check_int("fixed sort order", unordered, 0);
    foreach (a[i]) counts[a[i]]--;
    counts_zero("fixed sort", counts);
    // The smallest key is 0 and the largest 6.
    check_int("fixed sort first key", a[0] % 7, 0);
    check_int("fixed sort last key", a[4095] % 7, 6);
  endtask

  task automatic large_queue();
    int q[$];
    int counts[1000];
    int unordered;
    // 2500 elements: the last run of each merge pass is a partial one.
    for (int i = 0; i < 2500; i++) begin
      q.push_back((i * 7919) % 1000);
      counts[q[i]]++;
    end
    q.rsort() with ((item * 37) % 101);
    unordered = 0;
    for (int i = 0; i + 1 < q.size(); i++)
      if ((q[i] * 37) % 101 < (q[i + 1] * 37) % 101) unordered++;
    check_int("queue rsort order", unordered, 0);
    check_int("queue rsort size", q.size(), 2500);
    foreach (q[i]) counts[q[i]]--;
    counts_zero("queue rsort", counts);
  endtask

  task automatic large_text_key();
    string s[$];
    int counts[1000];
    int unordered;
    for (int i = 0; i < 1500; i++) begin
      s.push_back($sformatf("%0d", (i * 7919) % 1000));
      counts[s[i].atoi()]++;
    end
    // Strings compare lexicographically (6.16); substr past the end of a
    // shorter string is "" (6.16.8).
    s.rsort() with (item.substr(1, 2));
    unordered = 0;
    for (int i = 0; i + 1 < s.size(); i++)
      if (s[i].substr(1, 2) < s[i + 1].substr(1, 2)) unordered++;
    check_int("text key order", unordered, 0);
    check_int("text key size", s.size(), 1500);
    foreach (s[i]) counts[s[i].atoi()]--;
    counts_zero("text key", counts);
  endtask

  task automatic large_wide_key();
    int q[$];
    int counts[1000];
    int unordered;
    for (int i = 0; i < 1200; i++) begin
      q.push_back((i * 7919) % 1000);
      counts[q[i]]++;
    end
    // A 100-bit key whose order is that of item % 11.
    q.sort() with (100'(item % 11) << 80);
    unordered = 0;
    for (int i = 0; i + 1 < q.size(); i++)
      if (q[i] % 11 > q[i + 1] % 11) unordered++;
    check_int("wide key order", unordered, 0);
    foreach (q[i]) counts[q[i]]--;
    counts_zero("wide key", counts);
  endtask

  task automatic small_arrays();
    int d[] = '{12, 7, 30, 4, 25, 18};
    int e[] = '{12, 7, 30, 4, 25, 18};
    int f[5] = '{3, -8, 5, -1, 2};
    string w[$] = '{"pear", "fig", "apple", "banana"};
    real r[$] = '{1.5, -2.25, 0.5, 3.0};
    string t[$] = '{"b3", "a9", "c1", "d5"};
    int none[$];
    int one[$] = '{4};
    int two[$] = '{5, 6};
    // Keys of item % 10: 2 7 0 4 5 8.
    d.sort() with (item % 10);
    check_int("d 0", d[0], 30);
    check_int("d 1", d[1], 12);
    check_int("d 2", d[2], 4);
    check_int("d 3", d[3], 25);
    check_int("d 4", d[4], 7);
    check_int("d 5", d[5], 18);
    e.rsort() with (item % 10);
    check_int("e 0", e[0], 18);
    check_int("e 1", e[1], 7);
    check_int("e 2", e[2], 25);
    check_int("e 3", e[3], 4);
    check_int("e 4", e[4], 12);
    check_int("e 5", e[5], 30);
    // Keys of item * item: 9 64 25 1 4.
    f.sort() with (item * item);
    check_int("f sort 0", f[0], -1);
    check_int("f sort 1", f[1], 2);
    check_int("f sort 2", f[2], 3);
    check_int("f sort 3", f[3], 5);
    check_int("f sort 4", f[4], -8);
    f.rsort() with (item * item);
    check_int("f rsort 0", f[0], -8);
    check_int("f rsort 1", f[1], 5);
    check_int("f rsort 2", f[2], 3);
    check_int("f rsort 3", f[3], 2);
    check_int("f rsort 4", f[4], -1);
    // Lengths 4 3 5 6.
    w.sort() with (item.len());
    check_str("w sort 0", w[0], "fig");
    check_str("w sort 1", w[1], "pear");
    check_str("w sort 2", w[2], "apple");
    check_str("w sort 3", w[3], "banana");
    w.rsort() with (item.len());
    check_str("w rsort 0", w[0], "banana");
    check_str("w rsort 1", w[1], "apple");
    check_str("w rsort 2", w[2], "pear");
    check_str("w rsort 3", w[3], "fig");
    // Real keys of item * item: 2.25 5.0625 0.25 9.0.
    r.sort() with (item * item);
    check_real("r sort 0", r[0], 0.5);
    check_real("r sort 1", r[1], 1.5);
    check_real("r sort 2", r[2], -2.25);
    check_real("r sort 3", r[3], 3.0);
    r.rsort() with (item * item);
    check_real("r rsort 0", r[0], 3.0);
    check_real("r rsort 1", r[1], -2.25);
    check_real("r rsort 2", r[2], 1.5);
    check_real("r rsort 3", r[3], 0.5);
    // Text keys "3" "9" "1" "5".
    t.sort() with (item.substr(1, 1));
    check_str("t sort 0", t[0], "c1");
    check_str("t sort 1", t[1], "b3");
    check_str("t sort 2", t[2], "d5");
    check_str("t sort 3", t[3], "a9");
    t.rsort() with (item.substr(1, 1));
    check_str("t rsort 0", t[0], "a9");
    check_str("t rsort 1", t[1], "d5");
    check_str("t rsort 2", t[2], "b3");
    check_str("t rsort 3", t[3], "c1");
    none.sort() with (item % 3);
    check_int("none size", none.size(), 0);
    one.rsort() with (item % 3);
    check_int("one size", one.size(), 1);
    check_int("one 0", one[0], 4);
    // Keys of item % 3: 2 0.
    two.sort() with (item % 3);
    check_int("two 0", two[0], 6);
    check_int("two 1", two[1], 5);
    two.rsort() with (item % 3);
    check_int("two rsort 0", two[0], 5);
    check_int("two rsort 1", two[1], 6);
  endtask

  initial begin
    small_arrays();
    large_fixed();
    large_queue();
    large_text_key();
    large_wide_key();
    if (failures == 0) $display("PASS");
    $finish;
  end
endmodule
