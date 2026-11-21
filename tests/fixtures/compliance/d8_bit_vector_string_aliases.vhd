-- fsim: top=d8_bit_vector_string_aliases(test) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2008 5.3.2.4: STANDARD declares TO_BSTRING, TO_BINARY_STRING,
-- TO_OCTAL_STRING and TO_HEX_STRING as aliases for BIT_VECTOR.
entity d8_bit_vector_string_aliases is
end entity;

architecture test of d8_bit_vector_string_aliases is
begin
    p1 : process is
        constant v : bit_vector(7 downto 0) := x"A5";
    begin
        assert to_bstring(v) = "10100101" severity failure;
        assert to_binary_string(v) = "10100101" severity failure;
        assert to_octal_string(v) = "245" severity failure;
        assert to_hex_string(v) = "A5" severity failure;
        report "PASS";
        wait;
    end process;
end architecture;
