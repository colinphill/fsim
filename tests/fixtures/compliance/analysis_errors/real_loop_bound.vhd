-- fsim-expect: FSIM-ELAB-VHLOOP-001 std=1993
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2008 10.10: a loop parameter range is discrete; a real
-- conversion or a physical unit is not.
entity real_loop_bound is
end entity;

architecture a of real_loop_bound is
begin
  process
    variable i2 : integer := 11;
  begin
    for c in 1 to real(i2) / 3 loop
      null;
    end loop;
    wait;
  end process;
end architecture;
