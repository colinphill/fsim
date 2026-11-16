// fsim: top=sv_kernel_queue_dynamic_array std=2017
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 7.5, 7.10: automatic queues and dynamic arrays of packed
// elements, held by the compiled kernel. new[] with and without an
// initializer, element reads and writes (out of range reads give the
// default, a write one past the end of a queue appends), size, delete,
// push, pop (an empty queue pops the default), insert, bounded queues,
// whole copies, and queue contents kept across a suspension.
module sv_kernel_queue_dynamic_array;
  logic clk = 1'b0;
  int failures;

  always #5 clk = ~clk;

  task automatic check(input string what, input longint got, input longint want);
    if (got != want) begin
      $display("FAIL %s: got %0d want %0d", what, got, want);
      failures++;
    end
  endtask

  function automatic void values();
    int d[];
    int c[];
    int q[$];
    logic [7:0] lq[$];
    bit [3:0] bq[$:2];
    int index;
    int sum;
    logic [7:0] popped;

    d = new[2];
    d[0] = 5;
    d[1] = 7;
    index = 5;
    check("dynamic read past the end", d[index], 0);
    check("dynamic size", d.size(), 2);
    d = new[4](d);
    check("resized keeps 0", d[0], 5);
    check("resized keeps 1", d[1], 7);
    check("resized fills", d[3], 0);
    c = d;
    d.delete();
    check("deleted size", d.size(), 0);
    check("copy size", c.size(), 4);
    check("copy element", c[1], 7);
    d = new[3](c);
    check("shrunk copy", d[2], 0);
    d = new[2](d);
    check("own initializer", d[1], 7);

    q.push_back(3);
    q.push_front(1);
    q.insert(1, 2);
    check("queue 0", q[0], 1);
    check("queue 1", q[1], 2);
    check("queue 2", q[2], 3);
    check("pop front", q.pop_front(), 1);
    check("pop back", q.pop_back(), 3);
    check("popped size", q.size(), 1);
    q[1] = 9;
    q[5] = 4;
    check("append at size", q[1], 9);
    check("write past the end", q.size(), 2);
    q.delete(0);
    check("delete index", q[0], 9);
    q.insert(7, 1);
    check("insert past the end", q.size(), 1);
    for (int k = 0; k < 100; k++) q.push_front(k);
    sum = 0;
    for (int k = 0; k < q.size(); k++) sum += q[k];
    check("wrapped size", q.size(), 101);
    check("wrapped sum", sum, 4959);
    check("wrapped last", q[q.size() - 1], 9);

    lq.push_back(8'b0001_xxxx);
    if (lq[0] !== 8'b0001_xxxx) begin
      $display("FAIL four-state element: %b", lq[0]);
      failures++;
    end
    if (lq[9] !== 8'bxxxx_xxxx) begin
      $display("FAIL four-state read past the end: %b", lq[9]);
      failures++;
    end
    void'(lq.pop_back());
    popped = lq.pop_back();
    if (popped !== 8'bxxxx_xxxx) begin
      $display("FAIL empty four-state pop: %b", popped);
      failures++;
    end

    for (int k = 0; k < 5; k++) bq.push_back(4'(k));
    check("bounded size", bq.size(), 3);
    check("bounded last", bq[2], 2);
    bq.push_front(4'd9);
    check("bounded front", bq[0], 9);
    check("bounded keeps the first", bq[2], 1);
    check("bounded size after push_front", bq.size(), 3);
  endfunction

  task automatic window(input int count, output int first, output int last,
      output int size);
    int q[$];
    for (int k = 0; k < count; k++) begin
      @(posedge clk);
      q.push_back(k * 3);
      if (q.size() > 3) void'(q.pop_front());
    end
    first = q[0];
    last = q[q.size() - 1];
    size = q.size();
  endtask

  initial begin
    int first;
    int last;
    int size;
    values();
    window(6, first, last, size);
    check("window first", first, 9);
    check("window last", last, 15);
    check("window size", size, 3);
    window(2, first, last, size);
    check("short window first", first, 0);
    check("short window size", size, 2);
    if (failures == 0) $display("PASS");
    $finish;
  end
endmodule
