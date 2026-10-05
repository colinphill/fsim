// SPDX-License-Identifier: Apache-2.0
module mixed_domain_boundary;
  logic source;
  wire result;
  logic event_seen;

  mixed_language_leaf leaf(.a(source), .y(result));

  always @(posedge result) begin
    event_seen = 1'b1;
  end

  initial begin
    source = 1'b0;
    event_seen = 1'b0;
    #1 source = 1'b1;
    #0 $display("MIX inactive result=%0b", result);
    #1 $display("MIX settled result=%0b event=%0b", result, event_seen);
    #1;
  end
endmodule
