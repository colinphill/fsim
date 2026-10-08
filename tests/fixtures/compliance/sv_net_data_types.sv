// fsim: top=sv_net_data_types std=2017
// IEEE 1800-2017 6.7.1: a net declaration with an explicit 4-state data
// type (wire logic, wire integer), and the vectored/scalared keywords.
module sv_net_data_types;
  logic [7:0] source = 8'h5a;
  wire logic [7:0] copy = source;
  wire integer negative = -5;
  wire scalared [3:0] low = source[3:0];
  tri vectored [3:0] high = source[7:4];
  wire logic bit0;
  assign bit0 = source[1];

  initial begin
    int failures = 0;
    #1;
    if (copy !== 8'h5a) failures++;
    if (negative != -5 || $bits(negative) != 32) failures++;
    if (low !== 4'ha || high !== 4'h5) failures++;
    if (bit0 !== 1'b1) failures++;
    if (failures == 0) $display("PASS");
    else $display("FAIL %0d", failures);
  end
endmodule
