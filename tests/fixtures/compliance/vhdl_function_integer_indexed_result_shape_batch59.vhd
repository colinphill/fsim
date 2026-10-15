-- fsim: top=vhdl_function_integer_indexed_result_shape_batch59(test) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- Function-result bounds follow the declared discrete index subtype.
entity vhdl_function_integer_indexed_result_shape_batch59 is
end entity;

architecture test of vhdl_function_integer_indexed_result_shape_batch59 is
    type integer_array_t is array (integer range <>) of integer;
    type descending_index_t is range 9 downto 1;
    type descending_array_t is array (descending_index_t range <>) of integer;

    function get_integer_array return integer_array_t is
    begin
        return (11, -22, 33);
    end function;

    function get_descending_array return descending_array_t is
    begin
        return (7, 8, 9);
    end function;

    function make_dynamic_array(seed : integer) return integer_array_t is
    begin
        return (seed, seed + 1, seed + 2);
    end function;

    constant integer_values : integer_array_t := get_integer_array;
    constant descending_values : descending_array_t := get_descending_array;
begin
    check : process
        variable mutable_seed : integer := 10;
    begin
        assert integer_values'length = 3
            and integer_values'left = integer'left
            and integer_values'right = integer'left + 2
            and integer_values'ascending
            and integer_values(integer'left) = 11
            and integer_values(integer'left + 1) = -22
            and integer_values(integer'left + 2) = 33
            report "INTEGER-indexed function result lost positional bounds"
            severity failure;

        assert descending_values'length = 3
            and descending_values'left = 9
            and descending_values'right = 7
            and not descending_values'ascending
            and descending_values(9) = 7
            and descending_values(8) = 8
            and descending_values(7) = 9
            report "declared descending index subtype bounds were not preserved"
            severity failure;

        mutable_seed := 30;
        assert make_dynamic_array(mutable_seed)'left = integer'left
            and make_dynamic_array(mutable_seed)'right = integer'left + 2
            and make_dynamic_array(mutable_seed)'length = 3
            report "aggregate result bounds depended on a mutable actual value"
            severity failure;

        report "PASS" severity note;
        wait;
    end process;
end architecture;
