-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 9.3.3.1/12.5: the aggregates' type comes from context alone, and the "=" of every
-- visible composite type fits, so the expression is ambiguous.
-- Expected: FSIM-VHDL-TYPE-002.
-- fsim-standard: 2008
entity t22 is
end entity;
architecture a of t22 is
begin
  process
    variable b : boolean;
  begin
    b := (1, 2) = (1, 2);
    wait;
  end process;
end architecture;
