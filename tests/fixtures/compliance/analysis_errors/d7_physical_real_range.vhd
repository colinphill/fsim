-- fsim-expect: FSIM-ELAB-VHPHYSICAL-011 std=1993
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 5.2.4.1: the range of a physical type has bounds of an integer type.
entity d7_physical_real_range is
end entity;

architecture a of d7_physical_real_range is
begin
  process
    type weight is range 0.0 to 1000.0
      units
        g;
      end units;
  begin
    wait;
  end process;
end architecture;
