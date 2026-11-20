-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 9.2.2: "+" does not mix INTEGER and REAL operands.
-- Expected: FSIM-VHDL-TYPE-001.
-- fsim-standard: 2008
entity t08 is
end entity;
architecture a of t08 is
begin
  process
    variable i : integer := 1;
    variable r : real := 1.0;
    variable k : integer;
  begin
    k := i + r;
    wait;
  end process;
end architecture;
