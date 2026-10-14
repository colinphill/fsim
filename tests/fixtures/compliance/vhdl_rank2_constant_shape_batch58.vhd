-- fsim: top=vhdl_rank2_constant_shape_batch58(test) std=1993
-- SPDX-License-Identifier: Apache-2.0
entity vhdl_rank2_constant_shape_batch58 is
end entity;

architecture test of vhdl_rank2_constant_shape_batch58 is
    type integer_matrix is array (natural range <>, natural range <>) of integer;
    constant values : integer_matrix := (
        (0, 3, 4, 5),
        (6, 7, 8, 9));
begin
    process is
    begin
        assert values'length(1) = 2;
        assert values'length(2) = 4;
        assert values(0, 0) = 0;
        assert values(0, 1) = 3;
        assert values(0, 3) = 5;
        assert values(1, 0) = 6;
        assert values(1, 1) = 7;
        assert values(1, 2) = 8;
        report "PASS";
        wait;
    end process;
end architecture;
