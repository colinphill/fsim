-- fsim-expect: FSIM-ELAB-053 std=1993
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 12.3: two variables of one declarative region are not homographs.
entity d7_duplicate_variable is
end entity;

architecture a of d7_duplicate_variable is
begin
  process
    variable v : integer := 0;
    variable v : integer := 1;
  begin
    wait;
  end process;
end architecture;
