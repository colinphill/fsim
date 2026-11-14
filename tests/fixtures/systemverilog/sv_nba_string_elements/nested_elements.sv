// SPDX-License-Identifier: Apache-2.0
//
// A nonblocking assignment to a whole element reached through nested containers
// or a structure's container member (IEEE 1800-2023 7.4.5, 7.8, 10.4.2) samples
// the value and every index when the statement executes and replaces the
// element in the NBA region: a structure value or pattern, a queue or a
// pattern for a container element, a key the outer associative array does not
// hold is created by the assignment, of two assignments the later one wins, and
// an intra-assignment delay moves the write to the later time slot.
module top;
  typedef struct { int a; int b; } pair_t;
  typedef struct { int items[$]; int tag; } box_t;
  pair_t aa[int][$];
  pair_t qq[$][$];
  pair_t other;
  int cc[int][$][$];
  int items[$];
  int lists[int][int];
  box_t bx[$];
  int inner[$];
  initial begin
    aa[1].push_back('{a:1, b:2});
    aa[1].push_back('{a:3, b:4});
    aa[1][0] <= '{a:10, b:20};
    $display("sns nest assoc-queue same-step %0d %0d", aa[1][0].a, aa[1][0].b);
    #1;
    $display("sns nest assoc-queue next-step %0d %0d %0d", aa[1][0].a, aa[1][0].b, aa[1][1].a);
    other.a = 7;
    other.b = 8;
    aa[1][1] <= other;
    other.a = 99;
    aa[1][0] <= '{a:5, b:5};
    aa[1][0] <= '{a:6, b:6};
    #1;
    $display("sns nest assoc-queue sampled %0d %0d %0d %0d", aa[1][1].a, aa[1][1].b, aa[1][0].a, aa[1][0].b);
    qq.push_back({});
    qq[0].push_back('{a:1, b:1});
    qq[0][0] <= '{a:2, b:3};
    qq[0][0] <= #1 '{a:4, b:5};
    $display("sns nest queue-queue same-step %0d %0d", qq[0][0].a, qq[0][0].b);
    #1;
    $display("sns nest queue-queue delayed %0d %0d", qq[0][0].a, qq[0][0].b);
    items.push_back(5);
    items.push_back(6);
    cc[2].push_back({});
    cc[2].push_back({});
    cc[2][1] <= items;
    items.push_back(7);
    $display("sns nest cont same-step %0d", cc[2][1].size());
    #1;
    $display("sns nest cont next-step %0d %0d %0d", cc[2][0].size(), cc[2][1].size(), cc[2][1][1]);
    $display("sns nest queue-queue final %0d %0d", qq[0][0].a, qq[0][0].b);
    lists[3][4] <= 9;
    #1;
    $display("sns nest assoc-assoc %0d", lists[3][4]);
    bx.push_back('{items:{}, tag:1});
    inner.push_back(8);
    inner.push_back(9);
    bx[0].items <= inner;
    inner.push_back(10);
    $display("sns nest member same-step %0d", bx[0].items.size());
    #1;
    $display("sns nest member next-step %0d %0d tag=%0d", bx[0].items.size(), bx[0].items[1], bx[0].tag);
  end
endmodule
