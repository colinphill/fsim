-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 5.2.3.1: an integer type definition defines a new type,
-- distinct from INTEGER; a value of it is not assignable to INTEGER (10.6.2.1).
-- Expected: FSIM-VHDL-TYPE-001.
-- fsim-standard: 2008
entity t31 is
end entity;
architecture a of t31 is
  type level is range 0 to 7;
begin
  process
    variable l : level := 3;
    variable k : integer;
  begin
    k := l;
    wait;
  end process;
end architecture;
