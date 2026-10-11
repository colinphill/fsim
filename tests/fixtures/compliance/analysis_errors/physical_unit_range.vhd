-- fsim-expect: FSIM-ELAB-VHPHYSICAL-004 std=1993
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-1993 3.1.3: each unit's position number lies in its type's range.
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
end architecture;
