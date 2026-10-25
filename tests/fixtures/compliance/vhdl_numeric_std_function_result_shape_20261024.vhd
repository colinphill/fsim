-- fsim: top=vhdl_numeric_std_function_result_shape_20261024(test) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- A function returning NUMERIC_STD UNSIGNED or SIGNED has the shape of the
-- value it returns (IEEE 1076-2008 10.13), so a type conversion of its call
-- keeps the operand's length (9.3.6) rather than losing it.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity vhdl_numeric_std_function_result_shape_20261024 is
end entity;

architecture test of vhdl_numeric_std_function_result_shape_20261024 is
    function low_bits(width : integer) return unsigned is
        variable wide : unsigned(width downto 0) := to_unsigned(8, width + 1);
    begin
        return wide(width - 1 downto 0);
    end function;

    function low_signed(width : integer) return signed is
        variable wide : signed(width downto 0) := to_signed(-3, width + 1);
    begin
        return wide(width - 1 downto 0);
    end function;

    function slice_of(value : unsigned) return unsigned is
    begin
        return value(3 downto 0);
    end function;

    -- A pass through a second function keeps the result shape.
    function relay(width : integer) return unsigned is
    begin
        return low_bits(width);
    end function;

    function gf_pow_like(exponent : integer; width : integer; poly : integer)
        return std_logic_vector is
        variable value : unsigned(width downto 0) := to_unsigned(1, width + 1);
    begin
        for step in 0 to exponent - 1 loop
            value := value sll 1;
            if value(width) = '1' then
                value := value xor to_unsigned(poly, width + 1);
            end if;
        end loop;
        return std_logic_vector(value(width - 1 downto 0));
    end function;

    constant POW3 : std_logic_vector(3 downto 0) := gf_pow_like(3, 4, 16#13#);
    constant POW14 : std_logic_vector(3 downto 0) := gf_pow_like(14, 4, 16#13#);
begin
    check : process
        variable wide_value : unsigned(4 downto 0) := to_unsigned(24, 5);
        variable as_vector : std_logic_vector(3 downto 0);
        variable as_signed : std_logic_vector(3 downto 0);
    begin
        as_vector := std_logic_vector(low_bits(4));
        assert as_vector = "1000"
            report "conversion of an UNSIGNED slice result lost its length"
            severity failure;

        as_vector := std_logic_vector(relay(4));
        assert as_vector = "1000"
            report "conversion of a relayed UNSIGNED result lost its length"
            severity failure;

        as_signed := std_logic_vector(low_signed(4));
        assert as_signed = "1101"
            report "conversion of a SIGNED slice result lost its length"
            severity failure;

        as_vector := std_logic_vector(slice_of(wide_value));
        assert as_vector = "1000"
            report "conversion of a slice of an UNSIGNED formal lost its length"
            severity failure;

        assert POW3 = "1000"
            report "alpha**3 in GF(16) was not computed"
            severity failure;
        assert POW14 = "1001"
            report "alpha**14 in GF(16) was not computed"
            severity failure;

        report "PASS" severity note;
        wait;
    end process;
end architecture;
