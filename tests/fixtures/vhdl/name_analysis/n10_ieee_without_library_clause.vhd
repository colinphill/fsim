-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 13.2: only STD and WORK are implicitly visible library logical names; IEEE needs a library clause.
-- Expected: FSIM-VHDL-NAME-001 at line 7 (no visible declaration of "ieee").
entity n10 is
end entity;
architecture a of n10 is
  signal s : ieee.std_logic_1164.std_logic;
begin
end architecture;
