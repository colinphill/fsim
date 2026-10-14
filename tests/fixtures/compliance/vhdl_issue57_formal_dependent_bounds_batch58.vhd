-- fsim: top=vhdl_issue57_formal_dependent_bounds_batch58(sim) std=1993
-- SPDX-License-Identifier: Apache-2.0
-- A pure BIT_VECTOR function derives a local range from its unconstrained
-- formal and reports the resulting cardinality. A separate wide-element
-- array ensures array length counts elements, not their payload bits.
entity vhdl_issue57_formal_dependent_bounds_batch58 is
end entity;

architecture sim of vhdl_issue57_formal_dependent_bounds_batch58 is
    function copied_length(value : bit_vector) return natural is
        variable copy_value : bit_vector(value'length - 1 downto 0);
    begin
        copy_value := value;
        return copy_value'length;
    end function;

    type wide_element_array_batch58 is array (natural range <>) of integer;
    signal wide_values : wide_element_array_batch58(4 to 5);

    constant short_length : natural := copied_length("101");
    constant byte_length : natural := copied_length(X"1F");
begin
    check : process
    begin
        assert short_length = 3
            report "FAIL dependent BIT_VECTOR range lost actual length 3"
            severity failure;
        assert byte_length = 8
            report "FAIL dependent BIT_VECTOR range lost actual length 8"
            severity failure;
        assert wide_values'length = 2
            report "FAIL wide-element array length counted payload bits"
            severity failure;
        report "PASS" severity note;
        wait;
    end process;
end architecture;
