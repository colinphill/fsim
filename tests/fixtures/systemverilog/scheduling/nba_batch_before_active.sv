// SPDX-License-Identifier: Apache-2.0
module nba_batch_before_active;
  logic a;
  logic b;
  logic sampled_b;

  initial begin
    a = 1'b0;
    b = 1'b0;
    sampled_b = 1'b0;
    #1;
    a <= 1'b1;
    b <= 1'b1;
    #1 $display("NBA batch sampled_b=%0b b=%0b", sampled_b, b);
  end

  always @(posedge a)
    sampled_b = b;
endmodule
