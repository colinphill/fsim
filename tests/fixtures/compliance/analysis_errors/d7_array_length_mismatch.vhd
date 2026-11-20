-- fsim-expect: FSIM-ELAB-VHARRAY-006 std=1993
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 10.6.2.1: the value of a variable assignment has the length of its target (both locally static).
entity d7_array_length_mismatch is
end entity;

architecture a of d7_array_length_mismatch is
begin
  process
    type bits is array (integer range <>) of bit;
    variable short : bits(1 to 4);
    variable long : bits(1 to 8);
  begin
    short := long;
    wait;
  end process;
end architecture;
