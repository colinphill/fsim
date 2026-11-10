// SPDX-License-Identifier: Apache-2.0
// Nonblocking assignments are performed in the order their statements
// executed (IEEE 1800-2023 4.6 b), 10.4.2), not in the order of the
// processes that executed them. Each procedure below that executes its
// nonblocking assignments later comes first in the source. `x` and `y` are
// read only by procedures; `p` and `q` are also read by $monitor, which
// prints them once, in the Postponed region of time 0 (21.2.3).
module nba_execution_order;
  logic go = 1'b0;
  logic [3:0] x;
  logic [3:0] y;
  logic [3:0] p;
  logic [3:0] q;

  // Woken by the last initial procedure's blocking write of `go`: its
  // statements execute after that procedure's `x <= 1` and `p <= 1`.
  always @(go) begin
    x <= 4'd2;
    p <= 4'd2;
  end

  // Resumes in the Inactive region after the last initial procedure's
  // `y <= 6` and `q <= 6` executed (4.4.2.3).
  initial begin
    #0;
    #0;
    y <= 4'd7;
    q <= 4'd7;
  end

  initial begin
    // The procedures above wait before this resumes (Inactive region).
    #0;
    x <= 4'd1;
    y <= 4'd6;
    p <= 4'd1;
    q <= 4'd6;
    go = 1'b1;
  end

  initial
    $monitor("p=%0d q=%0d", p, q);

  initial
    #1 $display("x=%0d y=%0d", x, y);
endmodule
