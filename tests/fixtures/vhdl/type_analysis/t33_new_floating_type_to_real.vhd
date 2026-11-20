-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 5.2.5.1: a floating type definition defines a new type,
-- distinct from REAL.
-- Expected: FSIM-VHDL-TYPE-001.
-- fsim-standard: 2008
entity t33 is
end entity;
architecture a of t33 is
  type ratio is range 0.0 to 1.0;
begin
  process
    variable q : ratio := 0.5;
    variable r : real;
  begin
    r := q;
    wait;
  end process;
end architecture;
