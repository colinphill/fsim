// SPDX-License-Identifier: Apache-2.0
// Reference objects (IEEE 1800-2023 37.15): the low connection of a ref port,
// a use of the port's variable and a select of it in an expression, and the
// ref formal of a task, whose actual is bound to the frame that runs.
module m (ref logic [2:0] r, input logic i);
  logic [3:0] loc;
  task automatic poke(ref logic [3:0] t, input int n);
    t[0] = 1'b1;
  endtask
  // The actual of the ref formal is an automatic variable of the caller.
  task automatic relay(input int n);
    logic [3:0] mine;
    poke(mine, n);
  endtask
  initial begin
    r[0] = 1'b0;
    poke(loc, 1);
    relay(2);
  end
endmodule

module top;
  logic [2:0] a;
  logic c;
  m u1 (a, c);
endmodule
