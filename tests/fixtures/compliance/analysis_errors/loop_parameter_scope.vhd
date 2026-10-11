-- fsim-expect: FSIM-ELAB-VHNAME-002 std=1993
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2008 12.2: a loop parameter is visible only within its loop.
entity loop_parameter_scope is
end entity;

architecture a of loop_parameter_scope is
begin
  process
    variable q : integer;
  begin
    for i in 1 to 3 loop
      q := i;
    end loop;
    q := i;
    wait;
  end process;
end architecture;
