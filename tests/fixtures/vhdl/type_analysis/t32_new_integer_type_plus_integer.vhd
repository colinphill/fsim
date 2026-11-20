-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 5.2.3.1, 9.2.6: "+" of a declared integer type takes two
-- operands of that type; LEVEL and INTEGER do not mix.
-- Expected: FSIM-VHDL-TYPE-001.
-- fsim-standard: 2008
entity t32 is
end entity;
architecture a of t32 is
  type level is range 0 to 7;
begin
  process
    variable l : level := 3;
    variable i : integer := 1;
  begin
    l := l + i;
    wait;
  end process;
end architecture;
