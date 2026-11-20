-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 9.2.7: no "*" takes two TIME operands.
-- Expected: FSIM-VHDL-TYPE-001.
-- fsim-standard: 2008
entity t10 is
end entity;
architecture a of t10 is
begin
  process
    variable t : time := 1 ns;
  begin
    t := t * t;
    wait;
  end process;
end architecture;
