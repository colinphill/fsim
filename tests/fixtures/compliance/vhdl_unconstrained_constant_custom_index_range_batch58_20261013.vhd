-- fsim: top=vhdl_unconstrained_constant_custom_index_range_batch58_20261013(test) std=2008
-- SPDX-License-Identifier: Apache-2.0
entity vhdl_unconstrained_constant_custom_index_range_batch58_20261013 is
end entity;

architecture test of vhdl_unconstrained_constant_custom_index_range_batch58_20261013 is
    type ascending_index_t is range 0 to 7;
    type descending_index_t is range 7 downto 0;
    type ascending_bits_t is array (ascending_index_t range <>) of bit;
    type descending_bits_t is array (descending_index_t range <>) of bit;
    subtype ascending_bits_full_t is ascending_bits_t(ascending_index_t);
    subtype descending_bits_full_t is descending_bits_t(descending_index_t);

    constant ascending_seed : ascending_bits_full_t := (others => '0');
    constant descending_seed : descending_bits_full_t := (others => '1');
    constant ascending_pattern : ascending_bits_full_t :=
        ('0', '0', '1', '1', '0', '1', '0', '0');
    constant descending_pattern : descending_bits_full_t :=
        ('1', '0', '0', '1', '0', '1', '0', '0');

    constant aggregate_ascending : ascending_bits_t :=
        (ascending_seed(1 to 3), ascending_seed(3 to 4));
    constant aggregate_descending : descending_bits_t :=
        (descending_seed(3 downto 1), descending_seed(4 downto 3));
    constant aggregate_null_left_ascending : ascending_bits_t :=
        (ascending_pattern(2 to 1), ascending_pattern(1 to 2));
    constant aggregate_null_right_ascending : ascending_bits_t :=
        (ascending_pattern(1 to 2), ascending_pattern(2 to 1));
    constant aggregate_null_left_descending : descending_bits_t :=
        (descending_pattern(1 downto 2), descending_pattern(2 downto 1));
    constant aggregate_null_right_descending : descending_bits_t :=
        (descending_pattern(2 downto 1), descending_pattern(1 downto 2));
    constant concat_ascending : ascending_bits_t :=
        ascending_seed(1 to 3) & ascending_seed(3 to 4);
    constant concat_descending : descending_bits_t :=
        descending_seed(3 downto 1) & descending_seed(4 downto 3);

    constant concat_null_left_ascending : ascending_bits_t :=
        ascending_seed(2 to 1) & ascending_seed(1 to 2);
    constant concat_null_left_descending : descending_bits_t :=
        descending_seed(1 downto 2) & descending_seed(2 downto 1);
    constant concat_null_right_ascending : ascending_bits_t :=
        ascending_seed(1 to 2) & ascending_seed(4 to 3);
    constant concat_null_right_descending : descending_bits_t :=
        descending_seed(2 downto 1) & descending_seed(1 downto 2);
    function array_left(value : ascending_bits_t) return ascending_index_t is
    begin
        return value'left;
    end function;
    function array_left(value : descending_bits_t) return descending_index_t is
    begin
        return value'left;
    end function;
    function array_right(value : ascending_bits_t) return ascending_index_t is
    begin
        return value'right;
    end function;
    function array_right(value : descending_bits_t) return descending_index_t is
    begin
        return value'right;
    end function;
begin
    check: process is
        type natural is range 11 to 15;
        type shadowed_index_t is array (natural range <>) of bit;
        constant shadowed_natural_index : shadowed_index_t := ('1', '0');
    begin
        assert shadowed_natural_index'left = 11
                and shadowed_natural_index'right = 12
                and shadowed_natural_index'ascending
            report "a local natural type was mistaken for STD.STANDARD.NATURAL"
            severity failure;
        assert aggregate_ascending'left = 0
                and aggregate_ascending'right = 4
                and aggregate_ascending'ascending
            report "aggregate bounds did not use the ascending index subtype"
            severity failure;
        assert aggregate_descending'left = 7
                and aggregate_descending'right = 3
                and not aggregate_descending'ascending
            report "aggregate bounds did not use the descending index subtype"
            severity failure;
        assert aggregate_null_left_ascending'left = 0
                and aggregate_null_left_ascending'right = 1
                and aggregate_null_left_ascending'ascending
                and array_left(aggregate_null_left_ascending) = 0
                and array_right(aggregate_null_left_ascending) = 1
                and aggregate_null_left_ascending(0) = ascending_pattern(1)
                and aggregate_null_left_ascending(1) = ascending_pattern(2)
            report "a null left ascending segment lost its bounds or values"
            severity failure;
        assert aggregate_null_right_ascending'left = 0
                and aggregate_null_right_ascending'right = 1
                and aggregate_null_right_ascending'ascending
                and array_left(aggregate_null_right_ascending) = 0
                and array_right(aggregate_null_right_ascending) = 1
                and aggregate_null_right_ascending(0) = ascending_pattern(1)
                and aggregate_null_right_ascending(1) = ascending_pattern(2)
            report "a null right ascending segment lost its bounds or values"
            severity failure;
        assert aggregate_null_left_descending'left = 7
                and aggregate_null_left_descending'right = 6
                and not aggregate_null_left_descending'ascending
                and array_left(aggregate_null_left_descending) = 7
                and array_right(aggregate_null_left_descending) = 6
                and aggregate_null_left_descending(7) = descending_pattern(2)
                and aggregate_null_left_descending(6) = descending_pattern(1)
            report "a null left descending segment lost its bounds or values"
            severity failure;
        assert aggregate_null_right_descending'left = 7
                and aggregate_null_right_descending'right = 6
                and not aggregate_null_right_descending'ascending
                and array_left(aggregate_null_right_descending) = 7
                and array_right(aggregate_null_right_descending) = 6
                and aggregate_null_right_descending(7) = descending_pattern(2)
                and aggregate_null_right_descending(6) = descending_pattern(1)
            report "a null right descending segment lost its bounds or values"
            severity failure;
        assert concat_ascending'left = 0
                and concat_ascending'right = 4
                and concat_ascending'ascending
            report "concatenation bounds did not use the ascending index subtype"
            severity failure;
        assert concat_descending'left = 7
                and concat_descending'right = 3
                and not concat_descending'ascending
            report "concatenation bounds did not use the descending index subtype"
            severity failure;
        assert concat_null_left_ascending'left = 0
                and concat_null_left_ascending'right = 1
                and concat_null_left_ascending'ascending
            report "a null left concatenation operand changed ascending bounds"
            severity failure;
        assert concat_null_left_descending'left = 7
                and concat_null_left_descending'right = 6
                and not concat_null_left_descending'ascending
            report "a null left concatenation operand changed descending bounds"
            severity failure;
        assert concat_null_right_ascending'left = 0
                and concat_null_right_ascending'right = 1
                and concat_null_right_ascending'ascending
            report "a null right concatenation operand changed ascending bounds"
            severity failure;
        assert concat_null_right_descending'left = 7
                and concat_null_right_descending'right = 6
                and not concat_null_right_descending'ascending
            report "a null right concatenation operand changed descending bounds"
            severity failure;
        assert array_left(aggregate_ascending) = 0
            and array_right(aggregate_ascending) = 4;
        assert array_left(aggregate_descending) = 7
            and array_right(aggregate_descending) = 3;
        assert array_left(concat_ascending) = 0
            and array_right(concat_ascending) = 4;
        assert array_left(concat_descending) = 7
            and array_right(concat_descending) = 3;
        assert array_left(concat_null_left_ascending) = 0
            and array_right(concat_null_left_ascending) = 1;
        assert array_left(concat_null_left_descending) = 7
            and array_right(concat_null_left_descending) = 6;
        report "PASS";
        wait;
    end process;
end architecture;
