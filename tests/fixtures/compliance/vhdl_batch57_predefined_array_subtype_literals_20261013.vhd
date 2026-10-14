-- fsim: top=vhdl_batch57_predefined_array_subtype_literals_20261013(test) std=2008
-- SPDX-License-Identifier: Apache-2.0
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity vhdl_batch57_predefined_array_subtype_literals_20261013 is
end entity;

architecture test of vhdl_batch57_predefined_array_subtype_literals_20261013 is
    subtype str16 is string(1 to 16);
    type text_table_t is array (natural range <>) of str16;
    constant TEXTS : text_table_t := (
        "0123456789ABCDEF",
        "fedcba9876543210");

    subtype byte_t is bit_vector(7 downto 0);
    type byte_table_t is array (natural range <>) of byte_t;
    constant BYTES : byte_table_t := (X"08", X"10", X"20");

    type sub_t is record
        f : unsigned(1 to 2);
    end record;
    type rec_t is record
        x : unsigned(7 downto 0);
        y : integer;
        z : sub_t;
    end record;
    type mem_t is array (natural range <>) of sub_t;
    constant RECORD_VALUE : rec_t := ("10X01U-H", 5, (f => "0U"));
    constant RECORDS : mem_t := ((f => "1U"), (f => "10"));
begin
    check: process is
    begin
        assert TEXTS(0) = "0123456789ABCDEF"
            report "constrained STRING element lost literal length or value"
            severity failure;
        assert BYTES(0) = X"08" and BYTES(1) = X"10"
                and BYTES(2) = X"20"
            report "constrained BIT_VECTOR element lost bit-string literal"
            severity failure;
        assert RECORD_VALUE.x(7) = '1' and RECORD_VALUE.x(0) = 'H'
                and RECORD_VALUE.y = 5 and RECORD_VALUE.z.f(1) = '0'
                and RECORD_VALUE.z.f(2) = 'U'
            report "numeric_std unsigned record literal lost its context"
            severity failure;
        assert RECORDS(0).f(1) = '1' and RECORDS(0).f(2) = 'U'
                and RECORDS(1).f(1) = '1' and RECORDS(1).f(2) = '0'
            report "numeric_std unsigned array element literals lost context"
            severity failure;
        report "PASS";
        wait;
    end process;
end architecture;
