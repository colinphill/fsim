// SPDX-License-Identifier: Apache-2.0
// Class members the generated code runs without the host class executor:
// a pure virtual function with a string result dispatches to the object's
// class (IEEE 1800-2023 8.21), an instance constant is assigned in its
// class's constructor (8.19), a class-property mailbox's try_get is an
// expression (15.4.8), and an event property assigned another denotes the
// event that one holds (15.5.5).
virtual class Shape;
  pure virtual function string name();
endclass

class Square extends Shape;
  virtual function string name();
    return "square";
  endfunction
endclass

class Circle extends Shape;
  virtual function string name();
    return "circle";
  endfunction
endclass

class Limit;
  const int max;
  function new(int m);
    max = m;
  endfunction
endclass

class Box;
  mailbox #(int) m;
  function new();
    m = new;
  endfunction
  function bit take(output int t);
    if (!m.try_get(t)) return 0;
    return 1;
  endfunction
endclass

class Events;
  event a, b;
  task join_b();
    a = b;
  endtask
endclass

module top;
  Shape s[2];
  Limit l;
  Box q;
  Events e;
  int v;
  bit got;
  initial begin
    Square sq;
    Circle ci;
    sq = new;
    ci = new;
    s[0] = sq;
    s[1] = ci;
    $display("names %s %s", s[0].name(), s[1].name());
    l = new(7);
    $display("max %0d", l.max);
    q = new;
    got = q.take(v);
    $display("empty %0d", got);
    q.m.put(42);
    got = q.take(v);
    $display("took %0d %0d", got, v);
    e = new;
    e.join_b();
    fork
      begin
        @(e.a);
        $display("a at %0t", $time);
      end
      begin
        #5 ->e.b;
      end
    join
  end
endmodule
