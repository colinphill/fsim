-- fsim-expect: FSIM-ELAB-068 std=1993
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 8.5: a non-null slice has the direction of its prefix and bounds in its index range.
entity d7_slice_wrong_direction is
end entity;

architecture a of d7_slice_wrong_direction is
begin
  process
    type bits is array (integer range <>) of bit;
    variable b : bits(1 to 8);
    variable c : bits(0 to 2);
  begin
    c := b(4 downto 2);
    wait;
  end process;
end architecture;
