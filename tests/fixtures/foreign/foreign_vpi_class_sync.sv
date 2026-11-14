// SPDX-License-Identifier: Apache-2.0
// Mailbox and semaphore class objects (IEEE 1800-2023 37.33 details 3, 4),
// the inline constraint of a randomize call (37.42 detail 1), a task
// reached through an interface port (37.41) and the properties of a class
// typespec that represents a specialization (37.32 details 5, 6), the
// values of a live object's constraint (37.38) and of string and queue
// function results (37.42, 38.15).
interface bus_if;
  int count;
  task automatic bump();
    count++;
  endtask
endinterface
module user(bus_if bus);
endmodule
class Packet;
  rand int x;
  int limit = 20;
  constraint c_limit { x < limit; }
endclass
typedef int int_q_t[$];
function automatic string greet(int n);
  return $sformatf("n%0d", n);
endfunction
function automatic int_q_t make_q();
  return '{4, 9, 16};
endfunction
class Box #(int W = 4);
  bit [W-1:0] data;
  static int made;
  event done[2];
endclass
module top;
  bus_if bus();
  user u(.bus(bus));
  mailbox #(int) mq = new();
  semaphore sem = new(0);
  Packet p = new;
  Box #(8) b;
  int ok;
  string s;
  int v;
  initial begin
    mq.put(5);
    mq.put(7);
    ok = p.randomize() with { x > 3; x < 10; };
    Box#(8)::made = 3;
    s = greet(4);
    v = make_q()[1];
  end
  initial sem.get(1);
  initial #3 $finish;
endmodule
