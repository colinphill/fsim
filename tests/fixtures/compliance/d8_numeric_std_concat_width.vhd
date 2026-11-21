-- fsim: top=d8_numeric_std_concat_width(tb) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 9.2.5, 16.8.5.3: the result of NUMERIC_STD "+" or "/" of a
-- NATURAL and an UNSIGNED operator expression (a concatenation, a logical
-- operator) has the length of the UNSIGNED operand.
library ieee;
use ieee.numeric_std.all;

entity d8_numeric_std_concat_width is
end entity;

architecture tb of d8_numeric_std_concat_width is
begin
    process
        variable u4 : unsigned(3 downto 0) := to_unsigned(3, 4);
        variable u6 : unsigned(5 downto 0) := to_unsigned(1, 6);
        variable u10 : unsigned(9 downto 0);
        variable q : unsigned(9 downto 0);
    begin
        u10 := 7 + (u6 & u4);
        assert u10 = to_unsigned(7 + 19, 10) severity failure;
        -- The result has the 6-element length of its UNSIGNED operand, so
        -- the target is an UNSIGNED(5 downto 0) (10.5.2.1).
        u6 := 7 + (u6 and u6);
        assert u6 = to_unsigned(8, 6) severity failure;
        q := 100 / ("000000" & u4);
        assert q = to_unsigned(33, 10) severity failure;
        report "PASS";
        wait;
    end process;
end architecture;
