-- fsim-expect: FSIM-ELAB-VHPHYSICAL-004 std=1993
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 5.2.4.1: a secondary unit is defined by a unit of the same type declared before it.
entity d7_physical_secondary_unknown_unit is
end entity;

architecture a of d7_physical_secondary_unknown_unit is
begin
  process
    type weight is range 0 to 1000
      units
        g;
        kg = 1000 t;
        t = 1000 kg;
      end units;
  begin
    wait;
  end process;
end architecture;
