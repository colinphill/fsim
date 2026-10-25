// fsim: top=sv_automatic_fork_join_reuse_batch96 std=2017
// SPDX-License-Identifier: Apache-2.0
module sv_automatic_fork_join_reuse_batch96;
  reg [7:0] cap [0:7];
  reg clk = 0;
  always #5 clk = ~clk;
  function automatic integer twice(input integer x);
    twice = x * 2;
  endfunction
  // Calls a nested automatic function while its own arguments are live: the
  // return of `twice` must not restore the forking task's context over them.
  function automatic integer chk(input integer n, input integer extra);
    integer k, acc; reg good;
    begin
      good = 1;
      acc = 0;
      for (k = 0; k < n; k = k + 1) begin
        if (cap[k] !== k[7:0]) good = 0;
        acc = acc + twice(extra);
      end
      if (acc != n * extra * 2) good = 0;
      chk = good;
    end
  endfunction
  task automatic run_one(input integer total, input integer tag);
    integer i, di;
    reg ok;
    begin
      fork
        begin : DRV
          for (di = 0; di < total; di = di + 1) @(posedge clk);
        end
        begin : CAP
          i = 0;
          while (i < total) begin
            @(posedge clk); #1;
            cap[i] = i[7:0];
            i = i + 1;
          end
        end
      join
      ok = 1'b1;
      for (i = 0; i < total; i = i + 1)
        if (cap[i] !== i[7:0]) ok = 1'b0;
      if (!chk(total, tag + 3)) ok = 1'b0;
      if (!ok || i !== total)
        $fatal(1, "automatic locals written by fork branches were not visible after join");
    end
  endtask
  initial begin
    run_one(4, 0);
    run_one(6, 1);
    run_one(8, 2);
    $display("PASS");
    $finish;
  end
endmodule
