// SPDX-License-Identifier: Apache-2.0
//
// `%p` of the containers an automatic task and the module hold (IEEE
// 1800-2023 21.2.1.6), which the v4 kernel holds as its own dynamic
// containers: the display (and $sformatf, 21.3.3) stays a host call that
// formats the kernel's elements, and the member runs in compiled code.
// An unpacked array prints as an assignment pattern, an associative array
// with its index labels in index order (7.8.2, 7.8.4); a string element
// prints in quotes; an integral element prints as it would unformatted
// (decimal), one holding x bits as a sized literal so that the output
// stays a legal assignment pattern (10.9).
//   q: push_back(3), push_back(-1), push_front(7) (7.10.2): '{7,3,-1}
//   d: new[2] (7.5.1) takes x elements; d[0] = 4'b1010 = 10:
//      '{10,4'bxxxx}
//   aa[5] = 50, aa[-2] = 20: keys in signed numerical order (7.8.4)
//      '{-2:20,5:50}
//   sq: "a", "bc": '{"a","bc"}
//   sa["y"] = 2, sa["x"] = 1: keys in lexicographical order (7.8.2)
//      '{"x":1,"y":2}
//   f[0:2]: f[0] = 1, f[2] = -3, f[1] keeps 0 (6.8): '{1,0,-3}
//   b[$:2] (bounded): 8'hff, 8'h0a: '{255,10}
//   $sformatf("%p|%p", q, d): '{7,3,-1}|'{10,4'bxxxx}
//   q.delete() (7.10.2.3): the empty queue '{}
//   the module's mq: push_back(4), push_back(9): '{4,9}
module top;
  int mq[$];
  task automatic run();
    int q[$];
    logic [3:0] d[];
    int aa[int];
    string sq[$];
    int sa[string];
    byte f[0:2];
    bit [7:0] b[$:2];
    string text;
    q.push_back(3);
    q.push_back(-1);
    q.push_front(7);
    d = new[2];
    d[0] = 4'b1010;
    aa[5] = 50;
    aa[-2] = 20;
    sq.push_back("a");
    sq.push_back("bc");
    sa["y"] = 2;
    sa["x"] = 1;
    f[0] = 1;
    f[2] = -3;
    b.push_back(8'hff);
    b.push_back(8'h0a);
    $display("inl q %p", q);
    $display("inl d %p", d);
    $display("inl aa %p", aa);
    $display("inl sq %p", sq);
    $display("inl sa %p", sa);
    $display("inl f %p", f);
    $display("inl b %p", b);
    text = $sformatf("%p|%p", q, d);
    $display("inl text %s", text);
    q.delete();
    $display("inl empty %p", q);
  endtask
  initial begin
    mq.push_back(4);
    mq.push_back(9);
    run();
    $display("inl module %p", mq);
  end
endmodule
