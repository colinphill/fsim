// fsim: top=sv_kernel_string_containers std=2017
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 7.4.6, 7.5, 7.9, 7.10: queues, dynamic arrays and
// associative arrays of strings in automatic tasks, held by the compiled
// kernel. Element reads and writes (invalid, out-of-range and missing
// indices read the empty string or the `default:` text; a queue write one
// past the end appends), push/pop at both ends, delete of one element and
// of all, queue concatenation (including the queue itself), a bounded
// queue, new[n] with and without an initializer, whole copies, associative
// traversal, and elements kept across a suspension.
module sv_kernel_string_containers;
  int failures;

  task automatic check(input string what, input string got, input string want);
    if (got != want) begin
      $display("FAIL %s: got \"%s\" want \"%s\"", what, got, want);
      failures++;
    end
  endtask

  task automatic check_int(input string what, input longint got, input longint want);
    if (got != want) begin
      $display("FAIL %s: got %0d want %0d", what, got, want);
      failures++;
    end
  endtask

  task automatic queues();
    string q[$];
    string b[$:2];
    string d[];
    string e[];
    int p[$];
    string s;
    integer unknown;
    unknown = 'x;
    q.push_back("one");
    q.push_front("zero");
    q.push_back("two");
    s = q[1];
    check("read", s, "one");
    q[0] = "ZERO";
    q[3] = "three";
    q[9] = "nine";
    q[-1] = "negative";
    q[unknown] = "unknown";
    check_int("size", q.size(), 4);
    check("written", q[0], "ZERO");
    check("appended", q[3], "three");
    check("out of range", q[9], "");
    check("unknown index", q[unknown], "");
    s = q.pop_back();
    check("pop_back", s, "three");
    s = q.pop_front();
    check("pop_front", s, "ZERO");
    q.delete(1);
    check_int("deleted one", q.size(), 1);
    check("after delete", q[0], "one");
    for (int i = 0; i < 40; i++) q.push_back($sformatf("item%0d", i));
    check_int("grown", q.size(), 41);
    check("grown element", q[40], "item39");
    q = {q[0], "mid", q[1]};
    check_int("concatenation", q.size(), 3);
    check("concatenated", q[1], "mid");
    q = {q, q};
    check_int("self concatenation", q.size(), 6);
    check("self concatenated", q[4], "mid");
    b.push_back("a");
    b.push_back("b");
    b.push_back("c");
    b.push_back("d");
    check_int("bounded", b.size(), 3);
    check("bounded last", b[2], "c");
    d = new[3];
    d[1] = "x";
    d[2] = "a string of more than fifteen bytes";
    e = new[5](d);
    d = new[2](d);
    check_int("resized", d.size(), 2);
    check("kept", d[1], "x");
    check("initializer", e[2], "a string of more than fifteen bytes");
    check("new element", e[4], "");
    e = d;
    check_int("copy", e.size(), 2);
    check("copied", e[1], "x");
    q.delete();
    check_int("deleted all", q.size(), 0);
    p.push_back(1);
    p = {p, 5, p};
    check_int("packed append", p.size(), 3);
    check_int("packed appended", p[1], 5);
    #10;
    q.push_back("after");
    check("after a wait", q[0], "after");
    check("kept after a wait", d[1], "x");
  endtask

  task automatic associative();
    string a[int];
    string b[int];
    string c[byte];
    string d[int] = '{default: "dflt"};
    int k;
    byte kb;
    string walk;
    a[5] = "five";
    a[1] = "one";
    a[-3] = "minus three";
    a[9] = "a string of more than fifteen bytes";
    check_int("associative size", a.size(), 4);
    check("associative read", a[5], "five");
    check("missing key", a[2], "");
    a[1] = "ONE";
    walk = "";
    if (a.first(k)) begin
      walk = {walk, a[k]};
      while (a.next(k)) walk = {walk, ",", a[k]};
    end
    check("traversal", walk, "minus three,ONE,five,a string of more than fifteen bytes");
    a.delete(5);
    check_int("deleted key", a.exists(5), 0);
    check("after delete", a[9], "a string of more than fifteen bytes");
    a[7] = "seven";
    b = a;
    a[7] = "changed";
    check("copy is separate", b[7], "seven");
    c[-1] = "negative";
    c[100] = "hundred";
    if (c.last(kb)) check_int("signed byte keys", kb, 100);
    check("default", d[3], "dflt");
    check_int("default adds no key", d.size(), 0);
    d[3] = "three";
    b = d;
    check("copied default", b[8], "dflt");
    for (int i = 0; i < 50; i++) a[i * 3] = $sformatf("v%0d", i);
    check("grown", a[21], "v7");
    #10;
    check("kept after a wait", a[147], "v49");
    check("default kept", b[100], "dflt");
    a.delete();
    a[2] = "again";
    check_int("refilled", a.size(), 1);
  endtask

  initial begin
    queues();
    associative();
    if (failures == 0) $display("PASS");
    $finish;
  end
endmodule
