// fsim: top=sv_sformat_packed_output_batch58 std=2017
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2017 21.2.1.2, 21.3.3, 21.3.4.3, and 11.10.
module sv_sformat_packed_output_batch58;
  typedef struct packed {
    int signed_value;
    int unsigned unsigned_value;
  } packed_values_t;

  packed_values_t packed_values;
  typedef logic [31:0] packed_word_t;
  int signed_value;
  int unsigned unsigned_value;
  logic [16*8-1:0] signed_text;
  logic [16*8-1:0] signed_member_text;
  logic [16*8-1:0] unsigned_text;
  logic [16*8-1:0] unsigned_member_text;
  string empty_text;

  logic [63:0] scan_text;
  string raw_text;
  packed_word_t input_bits;
  logic [31:0] output_bits;
  int matched;

  initial begin
    signed_value = -20;
    unsigned_value = 32'hfffffff6;
    packed_values.signed_value = signed_value;
    packed_values.unsigned_value = unsigned_value;

    // With no format literal, these integral arguments use default decimal
    // formatting and the automatically selected field width.
    $swrite(signed_text, signed_value);
    $swrite(signed_member_text, packed_values.signed_value);
    if (signed_text !== signed_member_text
        || signed_text[127:88] !== '0
        || signed_text[87:0] !== 88'h20202020202020202d3230)
      $fatal(1, "$swrite packed target and signed packed-member value");

    $swrite(unsigned_text, unsigned_value);
    $swrite(unsigned_member_text, packed_values.unsigned_value);
    if (unsigned_text !== unsigned_member_text
        || unsigned_text[127:80] !== '0
        || unsigned_text[79:0] !== 80'h34323934393637323836)
      $fatal(1, "$swrite packed target and unsigned packed-member value");

    // A one-byte string literal occupies one 32-bit %u output word. The
    // runtime unit test checks its exact native-endian byte stream; this
    // round trip independently checks the value and four-byte length.
    $sformat(raw_text, "%u", "A");
    if (raw_text.len() != 4)
      $fatal(1, "$sformat string literal occupies a complete word");
    matched = $sscanf(raw_text, "%u", output_bits);
    if (matched != 1 || output_bits !== 32'h41)
      $fatal(1, "$sformat packed string-literal round trip");

    // No value arguments produce an empty string.
    $swrite(empty_text);
    if (empty_text.len() != 0)
      $fatal(1, "$swrite without values produces empty output");

    // $sformat assigns its generated string to an integral variable using
    // the string-literal rules; %u zeroes X/Z, while %z retains four-state
    // aval/bval bytes. The chosen bytes avoid embedded NUL characters.
    input_bits = 32'b000x100z_001z000x_101xxxzz_100z111x;
    $sformat(scan_text, "%u", input_bits);
    if (scan_text[63:32] !== '0)
      $fatal(1, "sformat 2-value output padding");
    matched = $sscanf(scan_text, "%u", output_bits);
    if (matched != 1
        || output_bits !== 32'b00001000_00100000_10100000_10001110)
      $fatal(1, "sscanf 2-value result clears X/Z");

    $sformat(raw_text, "%u", input_bits);
    if (raw_text.len() != 4)
      $fatal(1, "$sformat %u also accepts a string destination");
    matched = $sscanf(raw_text, "%u", output_bits);
    if (matched != 1
        || output_bits !== 32'b00001000_00100000_10100000_10001110)
      $fatal(1, "$sformat %u string destination round trip");

    $sformat(scan_text, "%z", input_bits);
    matched = $sscanf(scan_text, "%z", output_bits);
    if (matched != 1 || output_bits !== input_bits)
      $fatal(1, "sscanf 4-value result preserves the packed value");

    $display("PASS");
    $finish;
  end
endmodule
