// SPDX-License-Identifier: Apache-2.0
module a1_alias_leaf_native_read;
  logic [7:0] source;
  wire [7:0] words [0:1];
  logic [7:0] direct_sample;
  logic [7:0] mixed_sample;

  assign words[0] = source;
  assign words[1] = ~source;

  always_comb begin
    direct_sample = words[0];
    mixed_sample = words[0] ^ source;
  end

  initial begin
    source = 8'h12;
    #1;
    $display("ALIAS first leaf=%08b direct=%08b mixed=%08b",
        words[0], direct_sample, mixed_sample);
    source = 8'h34;
    #1;
    $display("ALIAS second leaf=%08b direct=%08b mixed=%08b",
        words[0], direct_sample, mixed_sample);
    $finish;
  end
endmodule
