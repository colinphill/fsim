-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 9.2.5: no "&" takes a string and an integer.
-- Expected: FSIM-VHDL-TYPE-001.
-- fsim-standard: 2008
entity t12 is
end entity;
architecture a of t12 is
begin
  process
    variable s : string(1 to 3);
  begin
    s := "ab" & 1;
    wait;
  end process;
end architecture;
