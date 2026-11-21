-- fsim: top=d8_numeric_std_eq_width(tb) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 12.3, 16.8.5.3: NUMERIC_STD declares "=" and "/=" for
-- UNSIGNED and SIGNED (also against NATURAL / INTEGER); the declaration hides
-- the predefined array equality, so operands of different widths compare as
-- numbers instead of folding to FALSE.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity d8_numeric_std_eq_width is
end entity;

architecture tb of d8_numeric_std_eq_width is
begin
    process
        variable b : unsigned(7 downto 0) := to_unsigned(5, 8);
        variable n : natural := 5;
        variable z : unsigned(8 downto 0) := (others => '0');
        variable s : signed(7 downto 0) := to_signed(-3, 8);
        variable i : integer := -3;
        variable ok : boolean := true;
    begin
        assert b = 5 severity failure;
        assert 5 = b severity failure;
        assert b = n severity failure;
        assert not (b /= 5) severity failure;
        assert b = to_unsigned(5, 4) severity failure;
        assert z = to_unsigned(0, 4) severity failure;
        assert b /= 6 severity failure;
        assert s = -3 severity failure;
        assert -3 = s severity failure;
        assert s = i severity failure;
        assert s = to_signed(-3, 4) severity failure;
        assert not (s /= to_signed(-3, 16)) severity failure;
        assert s /= 3 severity failure;
        -- Values decided at run time, not folded at compile time.
        ok := ok and (b = n) and not (b /= n) and (s = i);
        n := 6;
        ok := ok and (b /= n) and not (b = n);
        assert ok severity failure;
        report "PASS";
        wait;
    end process;
end architecture;
