-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 16.5.2.1: S'EVENT requires a signal prefix; a variable prefix has no such attribute.
-- Expected: FSIM-VHDL-TYPE-001.
-- fsim-standard: 2008
entity t28 is
end entity;
architecture a of t28 is
begin
  process
    variable v : bit;
    variable b : boolean;
  begin
    b := v'event;
    wait;
  end process;
end architecture;
