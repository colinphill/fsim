// SPDX-License-Identifier: Apache-2.0
//
// A blocking assignment of a structure value, a pattern or a container to a
// whole element reached through nested containers or a structure's container
// member (IEEE 1800-2023 7.2, 7.4.5, 7.8-7.10, 10.4.1) replaces the element
// at once: a pattern builds the structure, a structure or queue value is copied
// at the statement, and of two assignments the later one wins.
module top;
  typedef struct { int a; int b; } pair_t;
  typedef struct { int items[$]; int tag; } box_t;
  pair_t aa[int][$];
  pair_t qq[$][$];
  pair_t other;
  int cc[int][$][$];
  int items[$];
  box_t bx[$];
  int inner[$];
  initial begin
    aa[1].push_back('{a:1, b:2});
    aa[1].push_back('{a:3, b:4});
    aa[1][0] = '{a:10, b:20};
    $display("sns blk assoc-queue pattern %0d %0d %0d", aa[1][0].a, aa[1][0].b, aa[1][1].a);
    other.a = 7; other.b = 8;
    aa[1][1] = other;
    other.a = 99;
    $display("sns blk assoc-queue struct %0d %0d", aa[1][1].a, aa[1][1].b);
    qq.push_back({});
    qq[0].push_back('{a:1, b:1});
    qq[0][0] = '{a:2, b:3};
    qq[0][0] = '{a:4, b:5};
    $display("sns blk queue-queue %0d %0d", qq[0][0].a, qq[0][0].b);
    items.push_back(5); items.push_back(6);
    cc[2].push_back({});
    cc[2].push_back({});
    cc[2][1] = items;
    items.push_back(7);
    $display("sns blk cont %0d %0d %0d", cc[2][0].size(), cc[2][1].size(), cc[2][1][1]);
    cc[2][0] = '{8, 9, 10};
    $display("sns blk cont-pattern %0d %0d", cc[2][0].size(), cc[2][0][2]);
    bx.push_back('{items:{}, tag:1});
    inner.push_back(8); inner.push_back(9);
    bx[0].items = inner;
    inner.push_back(10);
    $display("sns blk member %0d %0d tag=%0d", bx[0].items.size(), bx[0].items[1], bx[0].tag);
  end
endmodule
