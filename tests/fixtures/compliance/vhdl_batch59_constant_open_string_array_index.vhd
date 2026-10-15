-- fsim: top=vhdl_batch59_constant_open_string_array_index(test) std=2008
-- SPDX-License-Identifier: Apache-2.0
entity vhdl_batch59_constant_open_string_array_index is
end entity;

architecture test of vhdl_batch59_constant_open_string_array_index is
    type string_array_t is array (natural range <>) of string;
    constant values : string_array_t(4 downto 3) := (
        4 => "TEST 0",
        3 => "TEST 1"
    );
begin
    check: process is
    begin
        assert values(4) = "TEST 0"
            report "first nested string value or bounds were lost"
            severity failure;
        assert values(3) = "TEST 1"
            report "second nested string value or bounds were lost"
            severity failure;
        report "PASS";
        wait;
    end process;
end architecture;
