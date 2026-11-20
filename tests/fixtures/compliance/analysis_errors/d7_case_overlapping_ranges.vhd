-- fsim-expect: FSIM-ELAB-VHDLCASE-004 std=1993
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 10.9: choices and ranges of one case statement do not overlap.
entity d7_case_overlapping_ranges is
end entity;

architecture a of d7_case_overlapping_ranges is
begin
  process
    variable k : integer range 0 to 9 := 0;
  begin
    case k is
      when 0 to 5 => null;
      when 4 to 9 => null;
    end case;
    wait;
  end process;
end architecture;
