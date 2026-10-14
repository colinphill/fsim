-- fsim: top=vhdl_batch57_contextual_string_array_direction_20261013(test) std=2008
-- SPDX-License-Identifier: Apache-2.0
entity vhdl_batch57_contextual_string_array_direction_20261013 is
end entity;

architecture test of vhdl_batch57_contextual_string_array_direction_20261013 is
    type Character_Vector_T is array (natural range <>) of character;
    type Row_T is array (1 to 2) of character;
    type Rows_T is array (1 to 2) of Row_T;
begin
    check: process is
        variable bits : bit_vector(0 to 1);
        variable characters : Character_Vector_T(0 to 1);
        variable one_character : Character_Vector_T(5 to 5);
        variable rows : Rows_T;
    begin
        bits := (0 to 1 => "01");
        characters := (0 to 1 => "AB");
        -- This literal has Row_T, the element type of Rows_T. Its descending
        -- choice repeats one row; it does not supply a Rows_T array segment.
        rows := (2 downto 1 => "CD");
        one_character := (5 to 5 => "Z");
        assert bits(0) = '0' and bits(1) = '1'
            report "BIT_VECTOR literal association lost its element values"
            severity failure;
        assert characters(0) = 'A' and characters(1) = 'B'
            report "user CHARACTER-array literal association lost its values"
            severity failure;
        assert one_character'left = 5 and one_character'right = 5
            and one_character(5) = 'Z'
            report "one-element CHARACTER range lost its context"
            severity failure;
        assert rows(1) = "CD" and rows(2) = "CD"
            report "array-element string literal was not repeated"
            severity failure;

        report "PASS";
        wait;
    end process;
end architecture;
