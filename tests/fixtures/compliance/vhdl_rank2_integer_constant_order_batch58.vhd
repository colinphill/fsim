-- fsim: top=vhdl_rank2_integer_constant_order_batch58(test) std=1993
-- SPDX-License-Identifier: Apache-2.0
entity vhdl_rank2_integer_constant_order_batch58 is
end entity;

architecture test of vhdl_rank2_integer_constant_order_batch58 is
    type integer_matrix is array (integer range <>, integer range <>) of integer;
    constant descending_rows : integer_matrix(7 downto 6, 4 to 5) :=
        ((11, 12), (21, 22));
    constant ascending_rows : integer_matrix(-2 to -1, 9 downto 8) :=
        ((31, 32), (41, 42));
begin
    process is
    begin
        assert descending_rows(7, 4) = 11;
        assert descending_rows(7, 5) = 12;
        assert descending_rows(6, 4) = 21;
        assert descending_rows(6, 5) = 22;
        assert ascending_rows(-2, 9) = 31;
        assert ascending_rows(-2, 8) = 32;
        assert ascending_rows(-1, 9) = 41;
        assert ascending_rows(-1, 8) = 42;
        report "PASS";
        wait;
    end process;
end architecture;
