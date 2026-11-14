// SPDX-License-Identifier: Apache-2.0
//
// A nonblocking assignment to a whole element that is an unpacked structure or
// a container (IEEE 1800-2023 7.2, 7.5, 7.8, 7.10, 10.4.2) samples the value
// and the index when the statement executes and replaces the element in the
// NBA region: an assignment pattern or another structure value, a queue or a
// pattern for a container element, a key the associative array does not hold
// is created by the assignment, and of two assignments to one element the
// later one wins.
module top;
  typedef struct { int a; int b; } pair_t;
  pair_t sq[$];
  pair_t sf[3];
  pair_t sd[];
  pair_t sm[int];
  pair_t other;
  int dm[][];
  int aq[int][$];
  int qq[$];
  initial begin
    sq.push_back('{a:1, b:2});
    sq.push_back('{a:3, b:4});
    sq[0] <= '{a:10, b:20};
    $display("sns agg queue same-step %0d %0d", sq[0].a, sq[0].b);
    #1;
    $display("sns agg queue next-step %0d %0d %0d %0d", sq[0].a, sq[0].b, sq[1].a, sq[1].b);
    other.a = 7;
    other.b = 8;
    sq[1] <= other;
    other.a = 99;
    sq[0] <= '{a:5, b:5};
    sq[0] <= '{a:6, b:6};
    $display("sns agg queue sampled %0d %0d %0d", sq[1].a, sq[1].b, sq[0].a);
    #1;
    $display("sns agg queue sampled-next %0d %0d %0d %0d", sq[1].a, sq[1].b, sq[0].a, sq[0].b);
    sf[1] <= '{a:30, b:31};
    $display("sns agg fixed same-step %0d %0d", sf[1].a, sf[1].b);
    #1;
    $display("sns agg fixed next-step %0d %0d %0d", sf[0].a, sf[1].a, sf[1].b);
    sd = new[2];
    sd[1] <= '{a:40, b:41};
    #1;
    $display("sns agg dynamic %0d %0d %0d", sd[0].a, sd[1].a, sd[1].b);
    sm[4] <= '{a:9, b:10};
    $display("sns agg assoc same-step n=%0d e4=%0d", sm.num(), sm.exists(4));
    #1;
    $display("sns agg assoc next-step n=%0d e4=%0d %0d %0d", sm.num(), sm.exists(4), sm[4].a, sm[4].b);
    sm[4] <= other;
    sm[5] <= #1 '{a:50, b:51};
    #1;
    $display("sns agg assoc other n=%0d a=%0d b=%0d e5=%0d", sm.num(), sm[4].a, sm[4].b, sm.exists(5));
    #1;
    $display("sns agg assoc delayed n=%0d a5=%0d b5=%0d", sm.num(), sm[5].a, sm[5].b);
    dm = new[2];
    dm[1] <= '{1, 2, 3};
    $display("sns cont dynamic same-step %0d", dm[1].size());
    #1;
    $display("sns cont dynamic next-step %0d %0d %0d %0d", dm[0].size(), dm[1].size(), dm[1][0], dm[1][2]);
    qq.push_back(5);
    aq[3] <= qq;
    qq.push_back(6);
    $display("sns cont assoc same-step n=%0d", aq.num());
    #1;
    $display("sns cont assoc next-step n=%0d size=%0d q0=%0d", aq.num(), aq[3].size(), aq[3][0]);
  end
endmodule
