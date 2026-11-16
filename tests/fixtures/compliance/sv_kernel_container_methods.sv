// fsim: top=sv_kernel_container_methods std=2017
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 7.12, 11.4.5, 11.4.11: whole-array operations on queues,
// dynamic arrays and associative arrays in automatic tasks, held by the
// compiled kernel. Equality and case equality of whole arrays (sizes, keys,
// unknown bits, real and string elements), the conditional operator with a
// known and an unknown condition, the reduction methods with and without a
// `with` clause, the ordering methods (stable keyed sorts, signed and
// four-state elements), the locator methods (bounded results, the result
// being the source itself), streaming to and from arrays (11.4.14: slices,
// four-state and string elements, a two-state target, several targets),
// and arrays kept across a suspension.
module sv_kernel_container_methods;
  int failures;

  task automatic check(input string what, input longint got, input longint want);
    if (got != want) begin
      $display("FAIL %s: got %0d want %0d", what, got, want);
      failures++;
    end
  endtask

  task automatic check_bits(input string what, input logic [7:0] got, input logic [7:0] want);
    if (got !== want) begin
      $display("FAIL %s: got %b want %b", what, got, want);
      failures++;
    end
  endtask

  task automatic check_queue(input string what, input int got[$], input int want[$]);
    if (got != want) begin
      $display("FAIL %s: size %0d want %0d", what, got.size(), want.size());
      failures++;
    end
  endtask

  task automatic equality();
    int q[$], r[$];
    logic [7:0] l[$], m[$];
    real f[$], g[$];
    string s[$], u[$];
    int a[int], b[int];
    int sa[string], sb[string];
    logic e;
    q = {1, 2, 3, 4};
    r = {1, 2, 3, 4};
    check("equal", q == r, 1);
    check("not equal", q != r, 0);
    r[2] = 9;
    check("element differs", q == r, 0);
    check("case element differs", q === r, 0);
    r = q;
    r.push_back(5);
    check("size differs", q == r, 0);
    l = {8'h1x, 8'h22};
    m = {8'h1x, 8'h22};
    e = l == m;
    check_bits("unknown bits", {7'b0, e}, 8'b0000000x);
    check("case equal unknown bits", l === m, 1);
    m[1] = 8'h23;
    check("known difference decides", l == m, 0);
    f = {0.0, 1.5};
    g = {-0.0, 1.5};
    check("real elements", f == g, 1);
    s = {"a", "bc"};
    u = {"a", "bc"};
    check("strings", s == u, 1);
    u[1] = "bd";
    check("strings differ", s == u, 0);
    a[1] = 1;
    a[5] = 2;
    b[1] = 1;
    b[5] = 2;
    check("associative", a == b, 1);
    b.delete(5);
    b[6] = 2;
    check("keys differ", a == b, 0);
    sa["k"] = 1;
    sb["k"] = 1;
    check("string keys", sa == sb, 1);
    sb["j"] = 1;
    check("string keys differ", sa == sb, 0);
  endtask

  task automatic conditional();
    int q[$], r[$], e[$];
    logic [7:0] l[$], m[$];
    string s[$], u[$];
    logic c;
    q = {1, 2, 3, 4};
    r = {1, 7, 3, 4, 5};
    c = 1;
    e = c ? q : r;
    check_queue("true", e, '{1, 2, 3, 4});
    c = 0;
    e = c ? q : r;
    check_queue("false", e, '{1, 7, 3, 4, 5});
    c = 'x;
    e = c ? q : r;
    check("unknown, sizes differ", e.size(), 0);
    r = {1, 7, 3, 4};
    e = c ? q : r;
    check_queue("unknown, elementwise", e, '{1, 0, 3, 4});
    e = c ? q : q;
    check_queue("unknown, equal", e, '{1, 2, 3, 4});
    l = {8'h1x, 8'h22};
    m = {8'h1x, 8'h23};
    m = c ? l : m;
    check_bits("equal elements kept", m[0], 8'h1x);
    check_bits("differing bits", m[1], 8'hxx);
    s = {"a", "b"};
    u = {"a", "x"};
    s = c ? s : u;
    check("string kept", s[0] == "a", 1);
    check("string differs", s[1] == "", 1);
  endtask

  task automatic reductions();
    int q[$], e[$];
    logic [7:0] l[$];
    int a[int];
    q = {1, 2, 3, 4, 5};
    check("sum", q.sum(), 15);
    check("product", q.product(), 120);
    check("and", q.and(), 0);
    check("or", q.or(), 7);
    check("xor", q.xor(), 1);
    check("sum with", q.sum() with (item > 2 ? item : 0), 12);
    check("sum of indices", q.sum() with (item.index), 10);
    check("empty sum", e.sum(), 0);
    check("empty product", e.product(), 1);
    check("empty and", e.and(), -1);
    l = {8'h01, 8'h0x};
    check_bits("unknown sum", l.sum(), 8'hxx);
    a[3] = 4;
    a[9] = 6;
    check("associative sum", a.sum(), 10);
  endtask

  task automatic ordering();
    int q[$], r[$];
    int d[];
    int unsigned u[$];
    logic signed [7:0] l[$];
    q = {5, -3, 9, 1, 9, 0, -3, 7};
    r = q;
    r.sort();
    check_queue("sort", r, '{-3, -3, 0, 1, 5, 7, 9, 9});
    r = q;
    r.rsort();
    check_queue("rsort", r, '{9, 9, 7, 5, 1, 0, -3, -3});
    r = q;
    r.reverse();
    check_queue("reverse", r, '{7, -3, 0, 9, 1, 9, -3, 5});
    r = q;
    r.sort() with (item > 4 ? item : 0);
    check_queue("stable keyed sort", r, '{-3, 1, 0, -3, 5, 7, 9, 9});
    r = q;
    r.rsort() with (item < 0 ? -item : item);
    check_queue("keyed rsort", r, '{9, 9, 7, 5, -3, -3, 1, 0});
    u = {32'hffff_ffff, 1, 0, 7};
    u.sort();
    check("unsigned sort", u[3], 32'hffff_ffff);
    l = {8'sh7f, 8'sh80, 8'b0000_x000, 8'b1zzz_0000, 8'sh00, -8'sd1};
    l.sort();
    check_bits("signed sort 0", l[0], 8'h80);
    check_bits("signed sort 1", l[1], 8'hff);
    check_bits("signed sort 2", l[2], 8'b1zzz_0000);
    check_bits("signed sort 3", l[3], 8'h00);
    check_bits("signed sort 4", l[4], 8'b0000_x000);
    check_bits("signed sort 5", l[5], 8'h7f);
    d = new[5];
    foreach (d[i]) d[i] = 10 - i * 3;
    d.sort();
    check("dynamic sort", d[0], -2);
    check("dynamic sort last", d[4], 10);
  endtask

  task automatic locators();
    int q[$], r[$];
    int b[$:2];
    logic signed [7:0] l[$];
    q = {5, -3, 9, 1, 9, 0, -3, 7};
    r = q.min();
    check_queue("min", r, '{-3});
    r = q.max();
    check_queue("max", r, '{9});
    r = q.unique();
    check_queue("unique", r, '{5, -3, 9, 1, 0, 7});
    r = q.unique_index();
    check_queue("unique_index", r, '{0, 1, 2, 3, 5, 7});
    r = q.find(x) with (x > 4);
    check_queue("find", r, '{5, 9, 9, 7});
    r = q.find_index(x) with (x == 9);
    check_queue("find_index", r, '{2, 4});
    r = q.find_first_index(x) with (x < 0);
    check_queue("find_first_index", r, '{1});
    r = q.find_last_index(x) with (x < 0);
    check_queue("find_last_index", r, '{6});
    r = q.find_first(x) with (x > 100);
    check("nothing found", r.size(), 0);
    r = q.min() with (item < 0 ? 0 : item);
    check_queue("keyed min", r, '{-3});
    r = q.unique() with (item > 4 ? 1 : 0);
    check_queue("keyed unique", r, '{5, -3});
    r = q.find with (item.index > 5);
    check_queue("by index", r, '{-3, 7});
    q = q.find(x) with (x != 9);
    check_queue("into the source", q, '{5, -3, 1, 0, -3, 7});
    b = q.find(x) with (x != 100);
    check("bounded result", b.size(), 3);
    r.delete();
    r = r.max();
    check("empty max", r.size(), 0);
    l = {8'sh01, 8'b0000_00x1, 8'sh01};
    l = l.unique();
    check("unknown elements are distinct", l.size(), 2);
    #10;
    check_queue("kept after a wait", q, '{5, -3, 1, 0, -3, 7});
    q.sort();
    check_queue("sorted after a wait", q, '{-3, -3, 0, 1, 5, 7});
  endtask

  task automatic streams();
    byte q[$];
    bit [3:0] n[$];
    logic [7:0] l[$];
    string s[$];
    bit [31:0] v;
    logic [15:0] w;
    byte a, b;
    q = {8'h12, 8'h34, 8'hAB};
    v = {>>{q}};
    check("pack", v, 32'h1234ab00);
    v = {<<{q}};
    check("bit reverse", v, 32'hd52c4800);
    v = {<<8{q}};
    check("byte slices", v, 32'hab341200);
    v = {<<4{q}};
    check("nibble slices", v, 32'hba432100);
    n = {>>{16'hBEEF}};
    check("unpack resizes", n.size(), 4);
    check("unpack first", n[0], 4'hb);
    check("unpack last", n[3], 4'hf);
    l = {8'b1010_x0z1, 8'h0f};
    w = {>>{l}};
    if (w !== 16'b1010_x0z1_0000_1111) begin
      $display("FAIL four-state stream: %b", w);
      failures++;
    end
    q = {>>byte{l}};
    check("two-state target", unsigned'(q[0]), 8'b1010_0001);
    {>>{a, b, n}} = 32'hDEAD_BEEF;
    check("first target", unsigned'(a), 8'hde);
    check("second target", unsigned'(b), 8'had);
    s = {"AB", "c"};
    v = {>>{s}};
    check("strings", v, 32'h41426300);
    v = {>>{q, 8'h55}};
    check("array and value", v, 32'ha10f5500);
    #10;
    q = {8'h01};
    v = {<<{q}};
    check("after a wait", v, 32'h80000000);
  endtask

  initial begin
    equality();
    conditional();
    reductions();
    ordering();
    locators();
    streams();
    if (failures == 0) $display("PASS");
    $finish;
  end
endmodule
