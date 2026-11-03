// SPDX-License-Identifier: Apache-2.0
// cbStmt (IEEE 1800-2023 38.36.1.1): statements, loops and blocks.
module top;
  integer i, n;
  reg [3:0] r;
  task bump;
    begin
      n = n + 1;
      n = n + 1;
    end
  endtask
  initial begin : main
    n = 0;
    for (i = 0; i < 3; i = i + 1) n = n + i;
    while (n < 8) n = n + 2;
    repeat (2) bump;
    begin
      r = 4'h5;
    end
    #5 $display("#c done n=%0d", n);
  end
  initial begin : worker
    #1 r = 4'h1;
    #5 r = 4'h2;
  end
  initial #2 disable worker;
  // The event control of a process runs its callback at each activation.
  reg g;
  initial begin g = 0; #20 g = 1; end
  always @(posedge g) r = 4'h3;
endmodule
