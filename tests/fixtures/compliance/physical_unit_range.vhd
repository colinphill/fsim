-- fsim: top=physical_unit_range(a) std=1993
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-1993 3.1.3: the position numbers of unit names need not lie
-- within the range constraint of the physical type.
entity physical_unit_range is
end entity;

architecture a of physical_unit_range is
  type span is range 1 to 6
    units
      prim;
      sec1 = 6 prim;
      sec2 = 36 sec1;
    end units;
begin
  process
  begin
    assert span'pos(1 sec1) = 6 report "unit position" severity failure;
    report "PASS";
    wait;
  end process;
end architecture;
