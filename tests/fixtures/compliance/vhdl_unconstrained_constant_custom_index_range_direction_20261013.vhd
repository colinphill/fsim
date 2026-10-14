-- fsim: top=vhdl_unconstrained_constant_custom_index_range_direction_20261013(sim) std=2008
-- SPDX-License-Identifier: Apache-2.0
entity vhdl_unconstrained_constant_custom_index_range_direction_20261013 is
end entity;

architecture sim of vhdl_unconstrained_constant_custom_index_range_direction_20261013 is
    constant ascending_source : bit_vector(5 to 5) := "1";
    constant descending_source : bit_vector(5 downto 5) := "1";
    type descending_matrix_t is array (0 to 1, 5 downto 5) of bit;
    constant descending_matrix_source : descending_matrix_t :=
        (0 => (5 => '0'), 1 => (5 => '1'));
    subtype ascending_index_t is integer range ascending_source'range;
    subtype descending_index_t is integer range descending_source'range;
    subtype descending_second_index_t is integer range
        descending_matrix_source'range(2);
    subtype descending_type_index_t is integer range
        descending_matrix_t'range(2);
    type ascending_values_t is array (ascending_index_t range <>) of bit;
    type descending_values_t is array (descending_index_t range <>) of bit;
    type descending_second_values_t is array
        (descending_second_index_t range <>) of bit;
    type descending_type_values_t is array
        (descending_type_index_t range <>) of bit;
    type singleton_ascending_index_t is range 5 to 5;
    type singleton_descending_index_t is range 5 downto 5;
    type singleton_ascending_values_t is array
        (singleton_ascending_index_t range <>) of bit;
    type singleton_descending_values_t is array
        (singleton_descending_index_t range <>) of bit;
    constant ascending_value : ascending_values_t := "0";
    constant descending_value : descending_values_t := "1";
    constant descending_second_value : descending_second_values_t := "1";
    constant descending_type_value : descending_type_values_t := "0";
    constant singleton_ascending_value : singleton_ascending_values_t := "0";
    constant singleton_descending_value : singleton_descending_values_t := "1";
begin
    check : process is
    begin
        assert ascending_value'left = 5
            and ascending_value'right = 5
            and ascending_value'ascending
            and ascending_value(5) = '0'
            report "a one-element ascending custom index range lost direction"
            severity failure;
        assert descending_value'left = 5
            and descending_value'right = 5
            and not descending_value'ascending
            and descending_value(5) = '1'
            report "a one-element descending custom index range lost direction"
            severity failure;
        assert descending_second_value'left = 5
                and descending_second_value'right = 5
                and not descending_second_value'ascending
                and descending_second_value(5) = '1'
            report "a dimensioned descending custom index range lost direction"
            severity failure;
        assert descending_type_value'left = 5
                and descending_type_value'right = 5
                and not descending_type_value'ascending
                and descending_type_value(5) = '0'
            report "a dimensioned type-prefix range lost direction"
            severity failure;
        assert singleton_ascending_value'left = 5
                and singleton_ascending_value'right = 5
                and singleton_ascending_value'ascending
                and singleton_ascending_value(5) = '0'
            report "an explicit singleton ascending subtype changed direction"
            severity failure;
        assert singleton_descending_value'left = 5
                and singleton_descending_value'right = 5
                and not singleton_descending_value'ascending
                and singleton_descending_value(5) = '1'
            report "an explicit singleton descending subtype changed direction"
            severity failure;
        report "PASS";
        wait;
    end process;
end architecture;
