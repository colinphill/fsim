-- SPDX-License-Identifier: Apache-2.0
-- No 'H', 'L', 'W' or '-' anywhere in the design: a VHPI library deposits
-- 'H' into s at 5 ns. r copies it, e compares it with the enumeration value
-- '1' (the predefined "=" on std_ulogic: 'H' is not '1', so e becomes '1'),
-- and q is
-- r and '1' (IEEE 1164 "and": 'H' acts as '1').
library ieee;
use ieee.std_logic_1164.all;

entity top is
end entity;

architecture a of top is
  signal s : std_logic := '0';
  signal r : std_logic := '0';
  signal e : std_logic := '0';
  signal q : std_logic := '0';
  signal one : std_logic := '1';
begin
  process (s) begin
    r <= s;
  end process;
  process (r) begin
    if r = '1' then
      e <= '0';
    else
      e <= '1';
    end if;
  end process;
  process (r, one) begin
    q <= r and one;
  end process;
  process begin
    wait for 20 ns;
    std.env.finish;
  end process;
end architecture;
