// SPDX-License-Identifier: Apache-2.0
//
// Nonblocking assignments to elements reached through nested containers of
// static variables declared in a procedural block, or in an automatic task
// with the static lifetime (IEEE 1800-2023 6.21, 10.4.2), behave as the same
// forms on module variables: the value and the indices are sampled at the
// statement and the element is replaced in the NBA region.
module top;
  typedef struct { int a; int b; } pair_t;
  typedef struct { int items[$]; int tag; } box_t;
  task automatic ta;
    static pair_t sq[$][$];
    static int sc[int][$][$];
    sq.push_back({});
    sq[0].push_back('{a:1, b:1});
    sq[0][0] <= '{a:8, b:9};
    sc[1].push_back({});
    sc[1][0].push_back(1);
    sc[1][0][0] <= 7;
    $display("sns loc task same-step %0d %0d", sq[0][0].a, sc[1][0][0]);
    #1;
    $display("sns loc task next-step %0d %0d", sq[0][0].a, sc[1][0][0]);
  endtask
  initial begin : blk
    static pair_t aa[int][$];
    static pair_t qq[$][$];
    static pair_t other;
    static int cc[int][$][$];
    static int items[$];
    static box_t bx[$];
    static int inner[$];
    static int nn[$][$];
    aa[1].push_back('{a:1, b:2});
    aa[1].push_back('{a:3, b:4});
    aa[1][0] <= '{a:10, b:20};
    $display("sns loc assoc-queue same-step %0d %0d", aa[1][0].a, aa[1][0].b);
    #1;
    $display("sns loc assoc-queue next-step %0d %0d %0d", aa[1][0].a, aa[1][0].b, aa[1][1].a);
    other.a = 7; other.b = 8;
    aa[1][1] <= other;
    other.a = 99;
    aa[1][0] <= '{a:5, b:5};
    aa[1][0] <= '{a:6, b:6};
    #1;
    $display("sns loc assoc-queue sampled %0d %0d %0d %0d", aa[1][1].a, aa[1][1].b, aa[1][0].a, aa[1][0].b);
    qq.push_back({});
    qq[0].push_back('{a:1, b:1});
    qq[0][0] <= #1 '{a:4, b:5};
    $display("sns loc queue-queue same-step %0d %0d", qq[0][0].a, qq[0][0].b);
    #1;
    $display("sns loc queue-queue delayed %0d %0d", qq[0][0].a, qq[0][0].b);
    #1;
    $display("sns loc queue-queue final %0d %0d", qq[0][0].a, qq[0][0].b);
    items.push_back(5); items.push_back(6);
    cc[2].push_back({});
    cc[2].push_back({});
    cc[2][1] <= items;
    items.push_back(7);
    $display("sns loc cont same-step %0d", cc[2][1].size());
    #1;
    $display("sns loc cont next-step %0d %0d %0d", cc[2][0].size(), cc[2][1].size(), cc[2][1][1]);
    nn.push_back({}); nn[0].push_back(1);
    nn[0][0] <= 9;
    $display("sns loc num same-step %0d", nn[0][0]);
    #1;
    $display("sns loc num next-step %0d", nn[0][0]);
    bx.push_back('{items:{}, tag:1});
    inner.push_back(8); inner.push_back(9);
    bx[0].items <= inner;
    inner.push_back(10);
    $display("sns loc member same-step %0d", bx[0].items.size());
    #1;
    $display("sns loc member next-step %0d %0d tag=%0d", bx[0].items.size(), bx[0].items[1], bx[0].tag);
    ta;
  end
endmodule
