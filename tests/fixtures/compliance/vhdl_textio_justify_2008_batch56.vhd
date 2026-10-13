-- fsim: top=vhdl_textio_justify_2008_batch56(sim) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2008 16.4 JUSTIFY pads strings on the selected side without
-- truncating values whose field width is smaller than their length.
use std.textio.all;

entity vhdl_textio_justify_2008_batch56 is
end entity;

architecture sim of vhdl_textio_justify_2008_batch56 is
begin
  test : process
  begin
    assert justify("hello", right, 7) = "  hello"
      report "RIGHT JUSTIFY did not pad to field width"
      severity failure;
    assert justify("hello", left, 0) = "hello"
      report "zero-width LEFT JUSTIFY changed the string"
      severity failure;
    assert justify("hello", left, 6) = "hello "
      report "LEFT JUSTIFY did not append one space"
      severity failure;
    assert justify("hello", right, 3) = "hello"
      report "JUSTIFY truncated a value wider than the field"
      severity failure;
    assert justify("", left, 3) = "   "
      report "JUSTIFY did not pad an empty string"
      severity failure;
    assert justify("hello") = "hello"
      report "default JUSTIFY parameters changed the value"
      severity failure;
    report "PASS";
    wait;
  end process;
  shadowed_side_test : process
    constant left : side := right;
  begin
    assert std.textio.justify(
               value => "hello", justified => left, field => 7)
        = "  hello"
      report "selected/named JUSTIFY used a local name as the enum literal"
      severity failure;
    wait;
  end process;
end architecture;
