-- fsim: top=d8_numeric_bit_eq_width(tb) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 12.3, 16.8.5.3: NUMERIC_BIT declares "=" and "/=" for its
-- UNSIGNED and SIGNED (also against NATURAL / INTEGER), hiding the predefined
-- array equality: operands of different widths compare as numbers.
library ieee;
use ieee.numeric_bit.all;

entity d8_numeric_bit_eq_width is
end entity;

architecture tb of d8_numeric_bit_eq_width is
begin
    process
        variable b : unsigned(7 downto 0) := to_unsigned(5, 8);
        variable s : signed(7 downto 0) := to_signed(-3, 8);
        variable n : natural := 5;
    begin
        assert b = 5 severity failure;
        assert b = n severity failure;
        assert b = to_unsigned(5, 4) severity failure;
        assert not (b /= 5) severity failure;
        assert s = -3 severity failure;
        assert s = to_signed(-3, 16) severity failure;
        assert s /= 3 severity failure;
        report "PASS";
        wait;
    end process;
end architecture;
