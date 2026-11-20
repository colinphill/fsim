-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 9.3.6, 12.5: a universal_integer literal converts implicitly
-- to the declared integer type; conversion to INTEGER is explicit (9.3.6).
-- fsim-standard: 2008
entity t34 is
end entity;
architecture a of t34 is
  type level is range 0 to 7;
begin
  process
    variable l : level := 3;
    variable k : integer;
  begin
    l := l + 1;
    k := integer(l);
    wait;
  end process;
end architecture;
