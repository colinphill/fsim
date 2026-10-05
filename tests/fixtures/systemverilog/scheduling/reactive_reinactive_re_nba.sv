// SPDX-License-Identifier: Apache-2.0
program automatic reactive_region_probe;
  logic value = 1'b0;

  initial begin
    value <= #1 1'b1;
    #1;
    $display("PROGRAM reactive value=%0d", value);
    #0;
    $display("PROGRAM re-inactive value=%0d", value);
    $strobe("STROBE value=%0d", value);
    #2;
  end
endprogram

module reactive_reinactive_re_nba;
  reactive_region_probe probe();

  initial begin
    #2;
    $finish;
  end
endmodule
