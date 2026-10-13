-- fsim: top=vhdl_string_local_aggregate_bytes(sim) std=1993
-- SPDX-License-Identifier: Apache-2.0
-- Keep fixed-length CHARACTER aggregates byte exact, including NULs.
entity vhdl_string_local_aggregate_bytes is
end entity;

architecture sim of vhdl_string_local_aggregate_bytes is
begin
  process
    variable bytes : string(9 downto 5) := (NUL, 'A', NUL, ' ', NUL);
  begin
    assert bytes'left = 9 and bytes'right = 5 and bytes'length = 5
      report "FAIL string bounds" severity failure;
    assert bytes(9) = NUL and bytes(8) = 'A' and bytes(7) = NUL
      and bytes(6) = ' ' and bytes(5) = NUL
      report "FAIL NUL or descending CHARACTER aggregate" severity failure;
    report "PASS";
    wait;
  end process;
end architecture;
