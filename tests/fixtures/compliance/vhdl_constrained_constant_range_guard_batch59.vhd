-- fsim: top=vhdl_constrained_constant_range_guard_batch59(test) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- Constrained array declarations retain their declared bounds when a constant
-- value is positional; genuinely unconstrained INTEGER-indexed constants
-- still start at INTEGER'LEFT.
entity vhdl_constrained_constant_range_guard_batch59 is
end entity;

architecture test of vhdl_constrained_constant_range_guard_batch59 is
    type fixed_numbers_t is array (1 to 3) of integer;
    type descending_numbers_t is array (4 downto 2) of integer;
    type unconstrained_numbers_t is array (integer range <>) of integer;
    subtype descending_subtype_t is unconstrained_numbers_t(7 downto 5);
    type empty_numbers_t is array (2 to 1) of integer;

    constant fixed_values : fixed_numbers_t := (11, 12, 13);
    constant descending_values : descending_numbers_t := (21, 22, 23);
    constant descending_subtype_values : descending_subtype_t := (24, 25, 26);
    constant empty_values : empty_numbers_t := (others => 0);
    constant inferred_integer_values : unconstrained_numbers_t := (31, 32, 33);
begin
    check : process is
        variable fixed_sum : integer := 0;
        variable descending_sum : integer := 0;
        variable descending_subtype_sum : integer := 0;
        variable fixed_count : natural := 0;
        variable descending_count : natural := 0;
        variable descending_subtype_count : natural := 0;
        variable empty_count : natural := 0;
    begin
        assert fixed_values'left = 1
                and fixed_values'right = 3
                and fixed_values'ascending
                and fixed_values'length = 3
            report "a fixed declared array lost its ascending range"
            severity failure;
        assert descending_values'left = 4
                and descending_values'right = 2
                and not descending_values'ascending
                and descending_values'length = 3
            report "a fixed declared array lost its descending range"
            severity failure;
        assert descending_subtype_values'left = 7
                and descending_subtype_values'right = 5
                and not descending_subtype_values'ascending
                and descending_subtype_values'length = 3
            report "a constrained array subtype lost its descending range"
            severity failure;
        assert empty_values'left = 2
                and empty_values'right = 1
                and empty_values'ascending
                and empty_values'length = 0
            report "a declared null array lost its range or direction"
            severity failure;

        for index in fixed_values'range loop
            fixed_sum := fixed_sum + fixed_values(index);
            fixed_count := fixed_count + 1;
        end loop;
        for index in descending_values'range loop
            descending_sum := descending_sum + descending_values(index);
            descending_count := descending_count + 1;
        end loop;
        for index in descending_subtype_values'range loop
            descending_subtype_sum
                := descending_subtype_sum + descending_subtype_values(index);
            descending_subtype_count := descending_subtype_count + 1;
        end loop;
        for index in empty_values'range loop
            empty_count := empty_count + 1;
        end loop;

        assert fixed_sum = 36 and fixed_count = 3
            report "the fixed declared range drove the wrong loop bounds"
            severity failure;
        assert descending_sum = 66 and descending_count = 3
            report "the descending subtype drove the wrong loop bounds"
            severity failure;
        assert descending_subtype_sum = 75
                and descending_subtype_count = 3
            report "the constrained subtype drove the wrong loop bounds"
            severity failure;
        assert empty_count = 0
            report "a null declared range executed a loop iteration"
            severity failure;
        assert inferred_integer_values'left = integer'left
                and inferred_integer_values'right = integer'left + 2
                and inferred_integer_values'ascending
                and inferred_integer_values(integer'left) = 31
                and inferred_integer_values(integer'left + 2) = 33
            report "an unconstrained INTEGER-indexed constant changed its default bounds"
            severity failure;

        report "PASS";
        wait;
    end process;
end architecture;
