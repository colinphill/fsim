// SPDX-License-Identifier: Apache-2.0
// An automatic event of a frame is triggered by vpi_put_value (IEEE 1800-2023
// 37.43, 38.34), and the string of an outer activation of a recursive task is
// read and put through the frame of that activation (37.43 detail 1: a frame
// holds the automatic variables of its own activation).
module top;
  task automatic ev_task(input int n);
    event e;
    begin
      fork
        begin @(e); $display("#c event seen at %0d n=%0d", $time, n); end
      join_none
      #2;
      $display("#c ev_task end at %0d", $time);
    end
  endtask
  task automatic rec(input int n);
    string s;
    begin
      s = $sformatf("lvl%0d", n);
      if (n > 1) rec(n - 1);
      $display("#c rec %0d s=%s", n, s);
    end
  endtask
  initial begin
    ev_task(1);
    rec(3);
  end
endmodule
