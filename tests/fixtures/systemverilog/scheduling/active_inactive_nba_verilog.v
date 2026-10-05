// SPDX-License-Identifier: Apache-2.0
module active_inactive_nba_verilog;
  reg blocking_value;
  reg nba_value;

  initial begin
    blocking_value = 1'b0;
    nba_value = 1'b0;
    $display("ACTIVE before blocking=%0b nba=%0b", blocking_value, nba_value);

    blocking_value = 1'b1;
    nba_value <= 1'b1;
    $display("ACTIVE after blocking=%0b nba=%0b", blocking_value, nba_value);

    #0;
    $display("INACTIVE blocking=%0b nba=%0b", blocking_value, nba_value);
    $strobe("POSTPONED blocking=%0b nba=%0b", blocking_value, nba_value);
    #1;
  end
endmodule
