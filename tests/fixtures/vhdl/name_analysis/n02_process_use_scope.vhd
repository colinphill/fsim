-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 12.4: a use clause in a process declarative part makes names visible only in that process.
-- Expected: FSIM-VHDL-NAME-001 at line 18 (sibling process p2 does not see KP).
package pk is
  constant KP : integer := 3;
end package;
entity n02 is
end entity;
architecture a of n02 is
begin
  p1 : process
    use work.pk.all;
    variable v : integer := KP;
  begin
    wait;
  end process;
  p2 : process
    variable w : integer := KP;
  begin
    wait;
  end process;
end architecture;
