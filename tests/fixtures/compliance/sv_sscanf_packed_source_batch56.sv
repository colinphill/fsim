// fsim: top=sv_sscanf_packed_source_batch56 std=2017
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2017 21.3.4.3: sscanf accepts a packed string source.
module sv_sscanf_packed_source_batch56;
  logic [8*64-1:0] source_text;
  logic [31:0] hex_value;
  int integer_value, matched, calls;
  real real_value;
  function automatic logic [63:0] next_text();
    calls++;
    return "42";
  endfunction
  initial begin
    source_text = "2a -17 1e1";
    matched = $sscanf(source_text, "%h %d %f", hex_value, integer_value, real_value);
    if (matched != 3 || hex_value != 'h2a || integer_value != -17 || real_value != 10.0)
      $fatal(1, "packed string fields");
    matched = $sscanf(next_text(), "%d", integer_value);
    if (matched != 1 || integer_value != 42 || calls != 1)
      $fatal(1, "packed source evaluated once");
    source_text = "12";
    matched = $sscanf(source_text, "%h", source_text);
    if (matched != 1 || source_text != 'h12)
      $fatal(1, "packed source snapshot before destination write");
    $display("PASS");
  end
endmodule
