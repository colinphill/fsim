-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 9.2.9: condition operator "??" applies implicitly to a BIT condition.
-- Expected: success.
-- fsim-standard: 2008
entity t13 is
end entity;
architecture a of t13 is
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
