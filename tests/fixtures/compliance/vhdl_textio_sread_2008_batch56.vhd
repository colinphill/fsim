-- fsim: top=vhdl_textio_sread_2008_batch56(sim) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2008 16.4: SREAD skips leading whitespace, leaves a terminating
-- separator unread, and returns the number of characters placed in VALUE.
use std.textio.all;

entity vhdl_textio_sread_2008_batch56 is
end entity;

architecture sim of vhdl_textio_sread_2008_batch56 is
begin
  check : process is
    variable l : line;
    variable value : string(1 to 3);
    variable count : natural := 0;
  begin
    l := new string'("  alphabet beta");
    sread(l, value, count);
    assert count = 3 and value = "alp" and l.all = "habet beta"
      report "SREAD did not skip spaces or stop at VALUE'length"
      severity failure;

    sread(l, value, count);
    assert count = 3 and value = "hab" and l.all = "et beta"
      report "SREAD did not leave the remainder of a long token"
      severity failure;

    sread(l, value, count);
    assert count = 2 and value(1 to 2) = "et" and l.all = " beta"
      report "SREAD consumed the whitespace terminating a token"
      severity failure;

    sread(l, value, count);
    assert count = 3 and value = "bet" and l.all = "a"
      report "SREAD did not skip the preserved leading separator"
      severity failure;

    sread(l, value, count);
    assert count = 1 and value(1) = 'a' and l.all = ""
      report "SREAD did not return a final token at end of LINE"
      severity failure;

    string_read(l, value, count);
    assert count = 0 and l.all = ""
      report "STRING_READ did not return zero on an empty LINE"
      severity failure;
    deallocate(l);

    report "PASS";
    wait;
  end process;
end architecture;
