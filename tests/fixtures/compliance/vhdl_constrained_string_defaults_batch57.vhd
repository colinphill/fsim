-- fsim: top=vhdl_constrained_string_defaults_batch57(sim) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- Fixed STRING objects retain their bounds and default each CHARACTER to NUL.
entity vhdl_constrained_string_defaults_batch57 is
end entity;

architecture sim of vhdl_constrained_string_defaults_batch57 is
  subtype aliased_text_t is string(10 to 12);
  signal aliased_global : aliased_text_t := "XYZ";
begin
  process
    variable ascending_text : string(4 to 6);
    variable descending_text : string(9 downto 7);
    variable null_text : string(1 to 0);
    variable aliased_local : aliased_text_t;
  begin
    assert ascending_text'left = 4 and ascending_text'right = 6
      and ascending_text'length = 3
      and ascending_text(4) = NUL and ascending_text(5) = NUL
      and ascending_text(6) = NUL
      report "FAIL ascending fixed STRING default" severity failure;
    assert descending_text'left = 9 and descending_text'right = 7
      and descending_text'length = 3
      and descending_text(9) = NUL and descending_text(8) = NUL
      and descending_text(7) = NUL
      report "FAIL descending fixed STRING default" severity failure;
    assert null_text'left = 1 and null_text'right = 0
      and null_text'length = 0
      report "FAIL null fixed STRING bounds" severity failure;
    assert aliased_local'left = 10 and aliased_local'right = 12
      and aliased_local'length = 3
      and aliased_local(10) = NUL and aliased_local(12) = NUL
      report "FAIL aliased fixed STRING default" severity failure;
    assert aliased_global(10) = 'X' and aliased_global(11) = 'Y'
      and aliased_global(12) = 'Z'
      report "FAIL aliased global STRING indexing" severity failure;
    aliased_local := aliased_text_t'("abc");
    assert aliased_local(10) = 'a' and aliased_local(11) = 'b'
      and aliased_local(12) = 'c'
      report "FAIL qualified local STRING bytes" severity failure;
    aliased_global <= aliased_text_t'("ABC");
    wait for 0 ns;
    assert aliased_global(10) = 'A' and aliased_global(11) = 'B'
      and aliased_global(12) = 'C'
      report "FAIL qualified signal STRING bytes" severity failure;
    report "PASS";
    wait;
  end process;
end architecture;
