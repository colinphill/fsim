-- fsim: top=d8_type_definition_bounds(test) std=1993
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-1993 3.1.2 / 1076-2019 5.2.3.1: the bounds of an integer type
-- definition need not be of one integer type; universal bounds (also with a
-- unary operator, 12.5) and `-1 to 1` index ranges (5.3.2.2) are typed
-- although user integer types are visible.
entity d8_type_definition_bounds is
end entity;

architecture test of d8_type_definition_bounds is
    type i1 is range 1 to 10;
    type i2 is range 11 to 20;
    constant v1 : i1 := 1;
    constant v2 : i2 := 20;
    type i5 is range v1 to v2;
    type current is range -2147483647 to +2147483647
        units
            na;
            ua = 1000 na;
        end units;
    type small is array (-1 to 1) of bit;
    type wide is array (0 to 2 ** 4 - 1) of bit;
    type arr is access bit_vector;
begin
    process
        variable s : small := (others => '0');
        variable w : wide := (others => '1');
        variable p : arr(0 to 3) := new bit_vector'("1111");
        variable k : i5 := i5'low;
    begin
        assert s'left = -1 and s'right = 1 and s'length = 3 severity failure;
        assert w'length = 16 severity failure;
        assert p(0 to 3) = "1111" severity failure;
        assert i5'pos(k) = 1 severity failure;
        assert ua = 1000 * na severity failure;
        report "PASS";
        wait;
    end process;
end architecture;
