-- fsim: top=vhdl_batch57_array_valued_range_choice_20261013(test) std=2008
-- SPDX-License-Identifier: Apache-2.0
entity vhdl_batch57_array_valued_range_choice_20261013 is
end entity;

architecture test of vhdl_batch57_array_valued_range_choice_20261013 is
    type Row_T is array (1 to 2) of character;
    type Rows_T is array (1 to 4) of Row_T;
begin
    check: process is
        variable ascending_left : integer_vector(0 to 1) := (11, 12);
        variable ascending_right : integer_vector(2 to 3) := (21, 22);
        variable ascending_range_result : integer_vector(0 to 3);
        variable ascending_positional_result : integer_vector(0 to 3);
        variable descending_left : integer_vector(4 downto 3) := (31, 32);
        variable descending_right : integer_vector(2 downto 1) := (41, 42);
        variable descending_range_result : integer_vector(4 downto 1);
        variable descending_positional_result : integer_vector(4 downto 1);
        variable source_row : Row_T := ('R', 'S');
        variable nested_result : Rows_T;
    begin
        -- A named range association maps source elements along the range;
        -- its direction must agree with this constrained target context.
        ascending_range_result := (
            0 to 1 => ascending_left,
            2 to 3 => ascending_right
        );
        ascending_positional_result := (ascending_left, ascending_right);
        assert ascending_range_result = ascending_positional_result
            and ascending_range_result(0) = 11
            and ascending_range_result(1) = 12
            and ascending_range_result(2) = 21
            and ascending_range_result(3) = 22
            report "ascending array-valued range associations lost index order"
            severity failure;

        descending_range_result := (
            4 downto 3 => descending_left,
            2 downto 1 => descending_right
        );
        descending_positional_result := (descending_left, descending_right);
        assert descending_range_result = descending_positional_result
            and descending_range_result(4) = 31
            and descending_range_result(3) = 32
            and descending_range_result(2) = 41
            and descending_range_result(1) = 42
            report "descending array-valued range associations lost index order"
            severity failure;

        -- Here the array-valued expression is one array element, not a
        -- segment of the outer Rows_T array. Its descending range is legal
        -- even though the outer array's constrained direction is ascending.
        nested_result := (
            4 downto 3 => source_row,
            others => source_row
        );
        assert nested_result(4) = source_row
            and nested_result(3) = source_row
            and nested_result(2) = source_row
            and nested_result(1) = source_row
            report "descending choice did not repeat the array element"
            severity failure;

        report "PASS";
        wait;
    end process;
end architecture;
