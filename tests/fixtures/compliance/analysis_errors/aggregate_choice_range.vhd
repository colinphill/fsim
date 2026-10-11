-- fsim-expect: FSIM-ELAB-VHARRAYAGG-003 std=1993
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2008 9.3.3.3: a named aggregate choice lies in the index range.
entity aggregate_choice_range is
end entity;

architecture a of aggregate_choice_range is
  subtype pair is bit_vector(2 downto 1);
  constant c : pair := (3 => '1', others => '0');
begin
end architecture;
