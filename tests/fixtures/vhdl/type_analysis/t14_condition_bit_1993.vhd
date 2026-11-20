-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-1993 8.7 (if statement): an if condition must be BOOLEAN; no implicit "??" before VHDL-2008.
-- Expected: FSIM-VHDL-TYPE-001.
-- fsim-standard: 1993
entity t14 is
end entity;
architecture a of t14 is
  signal s : bit;
  signal o : bit;
begin
  process (s)
  begin
    if s then
      o <= '1';
    end if;
  end process;
end architecture;
