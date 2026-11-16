// fsim: top=sv_kernel_associative_sparse std=2017
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 7.8, 7.9, 7.4.6: automatic associative arrays with integral
// indices and fixed arrays too large to hold densely, held by the compiled
// kernel. Associative reads of missing keys, a `default:` value, exists,
// delete, num, first/last/next/prev in signed and unsigned index order,
// whole copies; sparse array reads of unknown and out-of-range indices,
// writes back to the background, a copy, enough writes to fill the array
// densely, and contents kept across a suspension.
module sv_kernel_associative_sparse;
  int failures;

  task automatic check(input string what, input longint got, input longint want);
    if (got != want) begin
      $display("FAIL %s: got %0d want %0d", what, got, want);
      failures++;
    end
  endtask

  function automatic void associative();
    int a[int];
    int c[int];
    byte s[byte];
    bit [3:0] u[bit [3:0]];
    int d[int];
    int k;
    int sum;
    byte sk;
    bit [3:0] uk;

    a[5] = 50;
    a[-3] = 30;
    a[100] = 1000;
    a[0] = 7;
    check("num", a.num(), 4);
    check("read", a[-3], 30);
    check("missing key", a[42], 0);
    check("exists", a.exists(5), 1);
    check("not exists", a.exists(6), 0);
    check("first", a.first(k), 1);
    check("first key", k, -3);
    sum = 0;
    do sum += a[k]; while (a.next(k));
    check("walk", sum, 1087);
    check("last", a.last(k), 1);
    check("last key", k, 100);
    check("prev", a.prev(k), 1);
    check("prev key", k, 5);
    k = 1000;
    check("next past the end", a.next(k), 0);
    check("key kept", k, 1000);
    a.delete(0);
    a.delete(77);
    check("deleted key", a.exists(0), 0);
    c = a;
    a.delete();
    check("emptied", a.num(), 0);
    check("copy num", c.num(), 3);
    check("copy read", c[100], 1000);
    check("empty first", a.first(k), 0);

    s[-128] = 1;
    s[127] = 2;
    s[-1] = 3;
    check("signed first", s.first(sk), 1);
    check("signed first key", sk, -128);
    check("signed last", s.last(sk), 1);
    check("signed last key", sk, 127);

    u[4'hf] = 4'd1;
    u[4'h1] = 4'd2;
    check("unsigned first", u.first(uk), 1);
    check("unsigned first key", uk, 1);
    check("unsigned next", u.next(uk), 1);
    check("unsigned next key", uk, 15);

    d = '{1: 10, 2: 20, default: -1};
    check("default read", d[9], -1);
    check("default num", d.num(), 2);
    c = d;
    c.delete();
    check("copy keeps default", c[9], -1);

    for (int i = 0; i < 300; i++) a[i * 7 % 101] = i;
    sum = 0;
    if (a.first(k)) do sum += a[k]; while (a.next(k));
    check("many num", a.num(), 101);
    check("many sum", sum, 25149);
  endfunction

  function automatic void sparse();
    logic [7:0] m[16777215:0];
    logic [7:0] c[16777215:0];
    bit w[0:16777215];
    integer index;
    int ones;

    m[5] = 8'h55;
    m[16777215] = 8'h1x;
    if (m[7] !== 8'bxxxx_xxxx) begin
      $display("FAIL unwritten element: %b", m[7]);
      failures++;
    end
    index = 'x;
    if (m[index] !== 8'bxxxx_xxxx) begin
      $display("FAIL unknown index read: %b", m[index]);
      failures++;
    end
    index = 16777216;
    if (m[index] !== 8'bxxxx_xxxx) begin
      $display("FAIL out-of-range read: %b", m[index]);
      failures++;
    end
    check("read back", m[5], 8'h55);
    m[5] = 8'hxx;
    if (m[5] !== 8'bxxxx_xxxx) begin
      $display("FAIL background write: %b", m[5]);
      failures++;
    end
    m[0] = 8'h11;
    c = m;
    m[0] = 8'h77;
    check("copy element", c[0], 8'h11);
    check("source element", m[0], 8'h77);
    if (c[16777215] !== 8'b0001_xxxx) begin
      $display("FAIL copied four-state element: %b", c[16777215]);
      failures++;
    end

    for (int i = 0; i < 100000; i++) w[i * 13 % 16777216] = 1'b1;
    ones = 0;
    for (int i = 0; i < 200000; i++) ones += w[i];
    check("dense ones", ones, 15385);
    w[13] = 1'b0;
    check("dense clear", w[13], 0);
    check("dense untouched", w[14], 0);
  endfunction

  task automatic kept(output int first, output int count, output int bit_sum);
    int a[int];
    bit w[0:16777215];
    a[-7] = 70;
    a[9] = 90;
    w[100] = 1'b1;
    #10;
    a[3] = 30;
    w[200] = 1'b1;
    void'(a.first(first));
    count = a.num();
    bit_sum = w[100] + w[200] + w[300];
  endtask

  initial begin
    int first;
    int count;
    int bit_sum;
    associative();
    sparse();
    kept(first, count, bit_sum);
    check("kept first", first, -7);
    check("kept count", count, 3);
    check("kept sparse", bit_sum, 2);
    if (failures == 0) $display("PASS");
    $finish;
  end
endmodule
