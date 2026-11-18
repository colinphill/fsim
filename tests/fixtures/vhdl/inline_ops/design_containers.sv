// SPDX-License-Identifier: Apache-2.0
//
// Queues, a dynamic array, an associative array and a bounded queue of the
// module, written and read by several processes, which the v4 kernel holds
// as its own dynamic containers and runs in compiled member code. Each
// process's blocking writes take effect at once (IEEE 1800-2023 10.4.1),
// so a process reading after another one's write sees it.
//   7.10.2: push_back appends, pop_front removes the first element;
//   7.10.5: a bounded queue keeps its first 4 elements (a push beyond its
//   bound is discarded with a warning; none here exceeds it);
//   7.5.1: new[n](d) keeps d's elements and adds n - size defaults (0);
//   7.9.3: delete(k) removes key k; 7.9.2 num() counts the keys;
//   7.12.3: sum() adds the elements in the element type (int).
// The producer runs at posedges 5, 15, ..., 55 (step k = 1..6); the
// consumer at negedges 10, 20, ..., 60, after producer step k:
//   producer step k: q.push_back(k); q[0] += 1; aa[q.size()] = 10*q.size();
//                    bq.push_back(q.size())
//   consumer step k: pop_front while q.size() > 3; d grows by q.size();
//                    aa.delete(2); total += q.sum()
//   k  q after producer   q after consumer   d (appended)  total
//   1  6 6 1              6 6 1              3             13
//   2  7 6 1 2            6 1 2              3 3           22
//   3  7 1 2 3            1 2 3              ... 3         28
//   4  2 2 3 4            2 3 4              ... 3         37
//   5  3 3 4 5            3 4 5              ... 3         49
//   6  4 4 5 6            4 5 6              ... 3         64
// aa: k=1 sets aa[3]=30, k=2 aa[4]=40 (deleted 2 never exists), k>=3 aa[4]=40
// again; bq takes 3, 4, 4, 4 and drops none (its pushes stop at 4).
module top;
  bit clk;
  int q[$] = '{5, 6};
  int d[];
  int aa[int];
  byte bq[$:3];
  int total;
  always #5 clk = ~clk;
  initial begin : producer
    for (int k = 1; k <= 6; k++) begin
      @(posedge clk);
      q.push_back(k);
      q[0] = q[0] + 1;
      aa[q.size()] = q.size() * 10;
      if (bq.size() < 4) bq.push_back(8'(q.size()));
    end
  end
  initial begin : consumer
    repeat (6) begin
      @(negedge clk);
      while (q.size() > 3) void'(q.pop_front());
      d = new[d.size() + 1](d);
      d[d.size() - 1] = q.size();
      if (aa.exists(2)) aa.delete(2);
      total += q.sum();
    end
  end
  initial begin
    #65;
    $display("inl cont q=%0d:%0d,%0d,%0d d=%0d:%0d,%0d", q.size(), q[0], q[1], q[2],
      d.size(), d[0], d[5]);
    $display("inl cont aa=%0d:%0d,%0d bq=%0d:%0d,%0d,%0d,%0d total=%0d", aa.num(),
      aa[3], aa[4], bq.size(), bq[0], bq[1], bq[2], bq[3], total);
    $finish;
  end
endmodule
