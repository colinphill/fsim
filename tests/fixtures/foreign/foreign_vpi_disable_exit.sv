// SPDX-License-Identifier: Apache-2.0
// cbDisable runs only for an active block (IEEE 1800-2023 9.6.2, 38.36.1):
// a process that left a named block and waits at its event control is no
// longer inside the block, though the last statement it ran is.
module top;
  // Initialized before any procedure starts (6.8): no event at time 0.
  integer x = 0, y = 0, z = 0;
  reg go = 0, kick = 0;
  // Left at time 1, waiting at the event control: not active at time 2.
  always @(go) begin : left
    x = x + 1;
  end
  // Left together with the enclosing block `outer`: neither is active.
  always @(go) begin : outer
    begin : inner
      y = y + 1;
    end
  end
  // Waits inside the block: active, so the disable runs the callback.
  always @(kick) begin : waiting
    z = z + 1;
    #10 z = z + 1;
  end
  initial begin
    #1 go = 1; kick = 1;
    #1 disable left;
    disable outer;
    disable outer.inner;
    disable waiting;
    #1 $display("#c x=%0d y=%0d z=%0d", x, y, z);
  end
endmodule
