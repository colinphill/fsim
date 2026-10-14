// fsim: top=sv_sscanf_unknown_format_batch58 std=2017
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2017 21.3.4.3: an unknown bit in a packed integral format
// causes $sscanf to return EOF without assigning a scan target.
module sv_sscanf_unknown_format_batch58;
  typedef reg [80*8:1] packed_text_t;
  reg [80*8:1] str;
  packed_text_t packed_alias;
  integer a;
  integer matched;

  initial begin
    str = "A";
    packed_alias = "A";
    a = 17;
    matched = $sscanf(str, 'bx, a);
    if (matched !== -1 || a !== 17)
      $fatal(1,
        "unknown x in integral format must return EOF without assignment");

    a = -23;
    matched = $sscanf(str, 'bz, a);
    if (matched !== -1 || a !== -23)
      $fatal(1,
        "unknown z in integral format must return EOF without assignment");

    a = 31;
    matched = $sscanf(packed_alias, 'bx, a);
    if (matched !== -1 || a !== 31)
      $fatal(1,
        "a typedef of packed integral source must retain EOF behavior");

    $display("PASS");
    $finish;
  end
endmodule
