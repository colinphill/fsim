// SPDX-License-Identifier: Apache-2.0
// A PLI 1.0 force of a net (IEEE 1364-2005 9.3.2, 23.96) hides the driver's
// changes from every reader; its release returns the net to the driver's
// value (IEEE 1800-2023 10.6.2). The $acc_av step 6 releases without a show.
module top;
  reg [3:0] src;
  wire [3:0] nv;
  assign nv = src;
  initial begin
    src = 4'b1010;
    #1 $acc_av(nv, 5); $display("#c n nv=%b", nv);
    $acc_av(nv, 2);
    $display("#c n nv=%b", nv);
    src = 4'b0110;
    #1 $acc_av(nv, 5); $display("#c n nv=%b", nv);
    $acc_av(nv, 6);
    #1 $acc_av(nv, 5); $display("#c n nv=%b", nv);
    src = 4'b0001;
    #1 $acc_av(nv, 5); $display("#c n nv=%b", nv);
    #1 $finish;
  end
endmodule
