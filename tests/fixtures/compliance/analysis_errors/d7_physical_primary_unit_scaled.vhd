-- fsim-expect: FSIM-ELAB-VHPHYSICAL-003 std=1993
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 5.2.4.1: the primary unit declaration is an identifier alone.
entity d7_physical_primary_unit_scaled is
end entity;

architecture a of d7_physical_primary_unit_scaled is
begin
  process
    type weight is range 0 to 1000
      units
        g = 1 g;
      end units;
  begin
    wait;
  end process;
end architecture;
