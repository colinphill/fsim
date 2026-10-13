-- fsim-expect: FSIM-ELAB-VHARRAYSEL-003 std=1993
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2008 8.4: a static index of a constrained STRING lies in its
-- index range.
entity string_index_range is
end entity;

architecture a of string_index_range is
begin
  process
    variable text : string(1 to 4) := "abcd";
  begin
    if text(5) = 'e' then
      report "unreachable";
    end if;
    wait;
  end process;
end architecture;
