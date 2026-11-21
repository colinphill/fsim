-- fsim: top=d8_aggregate_unconstrained_rows(test) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2008 9.3.3.3: the element subtype of an array aggregate whose
-- element type is unconstrained is taken from its element values; the rows
-- of one aggregate are the subtype of the first row. 5.3.2.1: a width held
-- by an unconstrained subtype mark is its layout, not the mark's placeholder.
library ieee;
use ieee.std_logic_1164.all;

entity d8_aggregate_unconstrained_rows is
end entity;

architecture test of d8_aggregate_unconstrained_rows is
    type Indices_t is array (natural range <>) of std_logic_vector;
    subtype MemoryBaseType is integer_vector;
    type MemBlockType is array (integer range <>) of MemoryBaseType;

    function total(x : Indices_t) return integer is
    begin
        return x'length * x(x'left)'length;
    end function;

    function InitBase(Size : integer) return integer_vector is
        constant BaseU : integer_vector(0 to Size - 1) := (others => -1);
    begin
        return BaseU;
    end function;

    function InitBlock(BlockWidth, BaseWidth : integer) return MemBlockType is
        constant BaseU : MemoryBaseType(BaseWidth - 1 downto 0)
            := InitBase(BaseWidth);
    begin
        return MemBlockType'(0 to 2 ** BlockWidth - 1 => BaseU);
    end function;

    signal s : MemBlockType(1 to 16)(1 to 3) := InitBlock(4, 3);
    signal n : integer := 0;
begin
    p : process is
    begin
        n <= total((0 => std_logic_vector'(x"00"),
                    1 => std_logic_vector'(x"01")));
        wait for 1 ns;
        assert n = 16 report "total /= 16" severity failure;
        for i in s'range loop
            for j in s'element'range loop
                assert s(i)(j) = -1 report "element /= -1" severity failure;
            end loop;
        end loop;
        report "PASS";
        wait;
    end process;
end architecture;
