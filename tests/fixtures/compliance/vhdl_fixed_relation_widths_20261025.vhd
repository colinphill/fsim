-- fsim: top=vhdl_fixed_relation_widths_20261025(test) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- FIXED_GENERIC_PKG declares the relational operators of UFIXED and SFIXED,
-- which compare values at a common binary point (IEEE 1076-2019 G.4), so
-- operands of different index ranges compare by value, not as arrays.
library ieee;
use ieee.std_logic_1164.all;
use ieee.fixed_pkg.all;

entity vhdl_fixed_relation_widths_20261025 is
end entity;

architecture test of vhdl_fixed_relation_widths_20261025 is
begin
    process
        variable x, y, z : ufixed(7 downto -3) := (others => '0');
        variable s : ufixed(8 downto -3);
    begin
        x := to_ufixed(5, 7, -3);
        y := to_ufixed(6, 7, -3);
        z := to_ufixed(11, 7, -3);
        s := x + y;
        assert x + y = z report "x + y = z compared as arrays" severity failure;
        assert s = z report "s = z compared as arrays" severity failure;
        assert not (x + y /= z) report "x + y /= z" severity failure;
        assert not (x + y < z) report "x + y < z" severity failure;
        assert x + y <= z report "x + y <= z" severity failure;
        assert x < s report "x < s" severity failure;
        report "PASS" severity note;
        wait;
    end process;
end architecture;
