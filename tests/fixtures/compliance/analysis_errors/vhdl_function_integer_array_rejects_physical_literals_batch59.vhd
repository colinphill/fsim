-- fsim-expect: FSIM-ELAB-VHAGG-014 std=2008
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2008 9.3.3: physical literals retain their nominal scalar type
-- and cannot supply INTEGER elements in a function-result aggregate.
entity vhdl_function_integer_array_rejects_physical_literals_batch59 is
end entity;

architecture test of vhdl_function_integer_array_rejects_physical_literals_batch59 is
    type integer_array_t is array (integer range <>) of integer;
    type distance_t is range 0 to 1000 units
        mm;
        cm = 10 mm;
    end units;

    function wrong_time_row return integer_array_t is
    begin
        return (1 ns, 2 ns);
    end function;

    function wrong_custom_physical_row return integer_array_t is
    begin
        return (3 mm, 4 cm);
    end function;

    constant bad_time_values : integer_array_t := wrong_time_row;
    constant bad_custom_values : integer_array_t := wrong_custom_physical_row;
begin
end architecture;
