-- fsim: top=vhdl_block_boolean_concat_static_batch58(sim) std=1993
-- SPDX-License-Identifier: Apache-2.0
-- Constrained Boolean-array generic actuals accept pure-function and constant concatenation.
entity vhdl_block_boolean_concat_static_batch58 is
end entity;

architecture sim of vhdl_block_boolean_concat_static_batch58 is
    type boolean_v is array (integer range <>) of boolean;
    subtype boolean_4 is boolean_v(1 to 4);
    subtype boolean_8 is boolean_v(1 to 8);

    constant left_value : boolean_4 := (true, false, true, false);
    constant right_value : boolean_4 := (false, false, true, true);

    function return_array return boolean_4 is
        constant value : boolean_4 := (true, false, true, false);
    begin
        return value;
    end function;
begin
    function_concat : block
        generic (info : boolean_8);
        generic map (return_array & return_array);
    begin
        assert info = (true, false, true, false, true, false, true, false)
            report "pure-function Boolean-array concatenation changed value"
            severity failure;
    end block;

    constant_concat : block
        generic (info : boolean_8);
        generic map (left_value & right_value);
    begin
        assert info = (true, false, true, false, false, false, true, true)
            report "constant Boolean-array concatenation changed value"
            severity failure;
    end block;

    done : process
    begin
        wait for 1 ns;
        report "PASS" severity note;
        wait;
    end process;
end architecture;
