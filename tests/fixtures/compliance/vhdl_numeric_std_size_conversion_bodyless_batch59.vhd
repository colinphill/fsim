-- fsim: top=vhdl_numeric_std_size_conversion_bodyless_batch59(test) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- Control: exact numeric_std import with omitted callable bodies; signed
-- universal-integer literals use the compiler-owned package contract.
entity vhdl_numeric_std_size_conversion_bodyless_batch59 is
end entity;

library ieee;
use ieee.numeric_std.all;

architecture test of vhdl_numeric_std_size_conversion_bodyless_batch59 is
    constant unsigned_value : integer := 42;
    constant vector_width : positive := 8;
    constant unsigned_bits : unsigned(vector_width - 1 downto 0) :=
        to_unsigned(unsigned_value, vector_width);
    constant signed_bits : signed(7 downto 0) := to_signed(-1, 8);
begin
    check: process
    begin
        assert unsigned_bits = "00101010"
            report "typed unsigned value or named width changed"
            severity failure;
        assert signed_bits = "11111111"
            report "bodyless standard to_signed literal was not folded"
            severity failure;
        report "PASS" severity note;
        wait;
    end process;
end architecture;
