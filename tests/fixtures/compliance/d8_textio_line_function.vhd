-- fsim: top=d8_textio_line_function(test) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 16.4 (STD.TEXTIO): a function may return LINE, and the
-- result is assigned to a LINE variable like any access value (5.4).
use std.textio.all;

entity d8_textio_line_function is
end entity;

architecture test of d8_textio_line_function is
  impure function mk return line is
  begin
    return new string'("abc");
  end function;
begin
  process
    variable l : line;
  begin
    l := mk;
    assert l.all = "abc" severity failure;
    assert l'length = 3 severity failure;
    report "PASS";
    wait;
  end process;
end architecture;
