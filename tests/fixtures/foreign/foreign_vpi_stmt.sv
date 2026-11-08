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
    #1 r = 4'h1; $write("");
    #5 r = 4'h2;
  end
  initial #2 disable worker;
  // The event control of a process runs its callback at each activation.
  reg g;
  initial begin g = 0; #20 g = 1; end
  always @(posedge g) r = 4'h3;
  // A statement of a generate block's process (38.36.1.1).
  if (1) begin : gen
    initial #30 begin : gblk
      n = n + 1;
    end
  end
  // A disable of a block that has finished runs no cbDisable callback.
  reg q;
  initial begin : idle q = 0; end
  initial #3 disable idle;
  // A named block of an unlabelled generate block, which takes the implicit
  // name genblk<n> (27.6), is a scope beneath it (37.43).
  if (1) begin
    initial #35 begin : ublk
      n = n + 1;
    end
  end
  // A generate scope that holds an asserted process is published at once,
  // with the named blocks of its other processes.
  if (1) begin : eager
    assert property (@(posedge g) 1'b1);
    initial #40 begin : eblk
      n = n + 1;
    end
  end
endmodule
