-- fsim: top=vhdl_batch57_null_range_aggregate_20261013(test) std=2008
-- SPDX-License-Identifier: Apache-2.0
entity vhdl_batch57_null_range_aggregate_20261013 is
end entity;

architecture test of vhdl_batch57_null_range_aggregate_20261013 is
    type Empty_String_T is array (1 to 0) of character;
    type Empty_Rows_T is array (1 to 1) of Empty_String_T;
    signal value : integer_vector(2 to 1);
    signal empty_rows : Empty_Rows_T;

    impure function null_element_effect return integer is
    begin
        assert false
            report "a null-range scalar association was evaluated"
            severity failure;
        return 77;
    end function;
begin
    check: process is
        variable local_value : integer_vector(2 to 1);
        variable local_copy : integer_vector(2 to 1);
    begin
        value <= (2 to 1 => 77);
        value <= (2 to 1 => null_element_effect);
        value <= (others => null_element_effect);
        local_value := (others => null_element_effect);
        local_copy := local_value;
        value <= local_copy;
        empty_rows <= (1 => "");
        assert local_value'length = 0 and local_copy'length = 0
            report "null-range variable assignment or copy changed bounds"
            severity failure;
        assert empty_rows'length = 1
            report "zero-storage row lost its constrained outer range"
            severity failure;
        assert value'length = 0
            report "null range aggregate did not produce a null array"
            severity failure;
        report "PASS";
        wait;
    end process;
end architecture;
