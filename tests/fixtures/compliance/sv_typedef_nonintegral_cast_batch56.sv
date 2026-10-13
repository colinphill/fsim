// fsim: top=sv_typedef_nonintegral_cast_batch56 std=2017
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2017 6.24.1: typedef names preserve cast target semantics.
package cast_types;
  typedef string text_t;
  typedef real real_t;
  typedef shortreal short_t;
endpackage
module sv_typedef_nonintegral_cast_batch56;
  typedef cast_types::text_t text_alias;
  typedef cast_types::real_t real_alias;
  typedef logic [15:0] word_t;
  int calls;
  function automatic logic [15:0] letters();
    calls++;
    return "Hi";
  endfunction
  initial begin
    string text_value;
    real real_value;
    shortreal short_value;
    word_t packed_value;
    text_value = text_alias'(letters());
    if (text_value != "Hi" || calls != 1) $fatal(1, "string alias cast");
    text_value = cast_types::text_t'(text_value);
    if (text_value != "Hi") $fatal(1, "string identity cast");
    real_value = real_alias'(-123);
    if (real_value != -123.0) $fatal(1, "real alias cast");
    short_value = cast_types::short_t'(1.5);
    real_value = real_alias'(short_value);
    if (real_value != 1.5) $fatal(1, "real precision cast");
    packed_value = word_t'(real_value);
    if (packed_value != 2) $fatal(1, "integral cast rounds real");
    $display("PASS");
  end
endmodule
