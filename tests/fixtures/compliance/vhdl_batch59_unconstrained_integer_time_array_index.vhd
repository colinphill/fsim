-- fsim: top=vhdl_batch59_unconstrained_integer_time_array_index(test) std=2008
-- SPDX-License-Identifier: Apache-2.0
entity vhdl_batch59_unconstrained_integer_time_array_index is
end entity;

architecture test of vhdl_batch59_unconstrained_integer_time_array_index is
    type integer_array_t is array (integer range <>) of integer;
    type time_array_t is array (integer range <>) of time;
    constant integer_values : integer_array_t := (11, -22, 33);
    constant time_values : time_array_t := (-1 fs, 37582 ns);
begin
    check: process
        variable integer_first : boolean := false;
        variable integer_middle : boolean := false;
        variable integer_last : boolean := false;
        variable time_first : boolean := false;
        variable time_last : boolean := false;
    begin
        for index in integer_values'range loop
            if integer_values(index) = 11 then
                integer_first := true;
            elsif integer_values(index) = -22 then
                integer_middle := true;
            elsif integer_values(index) = 33 then
                integer_last := true;
            end if;
        end loop;
        for index in time_values'range loop
            if time_values(index) = -1 fs then
                time_first := true;
            elsif time_values(index) = 37582 ns then
                time_last := true;
            end if;
        end loop;
        assert integer_first and integer_middle and integer_last
            report "integer constant array selection lost positional values"
            severity failure;
        assert time_first and time_last
            report "TIME constant array selection lost 64-bit positional values"
            severity failure;
        report "PASS";
        wait;
    end process;
end architecture;
