-- fsim: top=vhdl_character_subtype_default_20261025(test) std=1993
-- SPDX-License-Identifier: Apache-2.0
-- An object of a CHARACTER subtype without an initial value starts at the
-- subtype's leftmost value (IEEE 1076-2008 6.4.2.3), the position of its
-- range's left character literal in CHARACTER (16.3).
entity vhdl_character_subtype_default_20261025 is
end entity;

architecture test of vhdl_character_subtype_default_20261025 is
    subtype upper is character range 'A' to 'Z';
    subtype tail is upper range 'C' to 'Z';
    subtype falling is character range 'z' downto 'a';
    signal s : upper;
begin
    process
        variable v : upper;
        variable t : tail;
        variable f : falling;
        variable c : character;
    begin
        assert v = 'A' and v = upper'left
            report "variable of a CHARACTER subtype did not start at 'A'"
            severity failure;
        assert t = 'C' report "nested CHARACTER subtype did not start at 'C'"
            severity failure;
        assert f = 'z' report "descending CHARACTER subtype did not start at 'z'"
            severity failure;
        assert s = 'A' report "signal of a CHARACTER subtype did not start at 'A'"
            severity failure;
        assert c = nul report "CHARACTER did not start at NUL"
            severity failure;
        report "PASS" severity note;
        wait;
    end process;
end architecture;
