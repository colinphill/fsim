// fsim: top=sv_streaming_builtin_slice_batch57 std=2017
// SPDX-License-Identifier: Apache-2.0
module sv_streaming_builtin_slice_batch57;
  logic [63:0] word = 64'h0123456789abcdef;
  logic [63:0] value;
  logic [7:0] a, b;
  initial begin
    value = {<< byte {word}};
    if (value !== 64'hefcdab8967452301) $fatal(1, "byte slice");
    value = {<< shortint {word}};
    if (value !== 64'hcdef89ab45670123) $fatal(1, "shortint slice");
    value = {<< int {word}};
    if (value !== 64'h89abcdef01234567) $fatal(1, "int slice");
    value = {<< integer {word}};
    if (value !== 64'h89abcdef01234567) $fatal(1, "integer slice");
    value = {<< longint {word}};
    if (value !== word) $fatal(1, "longint slice");
    value = {<< time {word}};
    if (value !== word) $fatal(1, "time slice");
    value = {>> byte {word}};
    if (value !== word) $fatal(1, "rightward slice");
    {<< byte {a, b}} = 16'h1234;
    if (a !== 8'h34 || b !== 8'h12) $fatal(1, "target slice");
    value = {<< 8 {word}};
    if (value !== 64'hefcdab8967452301) $fatal(1, "constant slice");
    $display("PASS");
    $finish;
  end
endmodule
