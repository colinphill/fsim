-- fsim: top=vhdl_string_slice_assignment_batch57(sim) std=1993
-- SPDX-License-Identifier: Apache-2.0
-- STRING slice writes preserve bytes outside ascending, descending, and
-- overlapping selected ranges.
entity vhdl_string_slice_assignment_batch57 is
end entity;

architecture sim of vhdl_string_slice_assignment_batch57 is
begin
    check : process is
        variable ascending : string(1 to 6) := "ABCDEF";
        variable descending : string(8 downto 1) := "ABCDEFGH";
        variable overlapping : string(1 to 8) := "ABCDEFGH";
    begin
        ascending(3 to 4) := "Hi";
        assert ascending = "ABHiEF"
            report "ascending STRING slice did not preserve the base value"
            severity failure;

        descending(6 downto 4) := "xyz";
        assert descending = "ABxyzFGH"
            report "descending STRING slice did not preserve the base value"
            severity failure;

        overlapping(3 to 6) := overlapping(1 to 4);
        assert overlapping = "ABABCDGH"
            report "overlapping STRING slice used bytes after the update"
            severity failure;
        assert false report "PASS" severity note;
        wait;
    end process;
end architecture;
