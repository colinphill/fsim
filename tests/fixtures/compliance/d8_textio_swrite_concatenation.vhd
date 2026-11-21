-- fsim: top=d8_textio_swrite_concatenation(a) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 16.4, 6.6.3, 9.2.5: STD.TEXTIO's SWRITE is an alias of
-- WRITE [LINE, STRING, SIDE, WIDTH]; a concatenation actual takes STRING
-- from that signature.
library ieee;
use ieee.std_logic_1164.all;
use std.textio.all;
entity d8_textio_swrite_concatenation is
end entity;
architecture a of d8_textio_swrite_concatenation is
  signal clk : std_ulogic := '1';
begin
  process
    variable l : line;
  begin
    wait for 1 ns;
    swrite(l, "clk: " & "x");
    assert l.all = "clk: x" severity failure;
    deallocate(l);
    swrite(l, "clk: " & to_string(clk));
    assert l.all = "clk: 1" severity failure;
    deallocate(l);
    report "PASS";
    wait;
  end process;
end architecture;
