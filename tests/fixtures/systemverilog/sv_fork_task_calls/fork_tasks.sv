// SPDX-License-Identifier: Apache-2.0
//
// A task call in a `fork ... join_none` runs in a thread of its own (IEEE
// 1800-2023 9.3.2, 9.3.4): the forking process goes on at once, and the call's
// delay and event wait suspend only the new thread (9.4.1, 9.4.2, 15.5.1).
// The always procedure forks a virtual task of a class on each request; one
// task waits a delay and an event, the other triggers the event after a
// delay. The thread's property writes after its waits (8.5) must land.
class P;
  int id;
endclass

class C;
  int waited;
  event go;
  virtual task run(P ph);
    #5;
    waited = 1;
    @go;
    waited = 2;
  endtask
  virtual task pre(P ph);
    #7;
    -> go;
  endtask
endclass

module top;
  C c;
  P ph;
  event req;
  int kind;
  always @(req) begin
    if (kind == 1) fork c.run(ph); join_none
    else if (kind == 2) fork c.pre(ph); join_none
  end
  initial begin
    c = new;
    ph = new;
    #1 kind = 1; -> req;
    #1 kind = 2; -> req;
    #1 $display("fk %0t waited=%0d", $time, c.waited);
    #5 $display("fk %0t waited=%0d", $time, c.waited);
    #5 $display("fk %0t waited=%0d", $time, c.waited);
    #5 $display("fk %0t waited=%0d", $time, c.waited);
  end
endmodule
