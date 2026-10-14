-- fsim: top=vhdl_rank2_constrained_shadow_natural_batch58(test) std=2008
-- SPDX-License-Identifier: Apache-2.0
entity vhdl_rank2_constrained_shadow_natural_batch58 is
end entity;

architecture test of vhdl_rank2_constrained_shadow_natural_batch58 is
begin
    process is
        type natural is range 11 to 15;
        type local_matrix is array (natural range 11 to 12, natural range 11 to 12) of integer;
        constant values : local_matrix := ((1, 2), (3, 4));
    begin
        assert values'left(1) = 11 and values'right(1) = 12;
        assert values'left(2) = 11 and values'right(2) = 12;
        assert values(11, 11) = 1 and values(12, 12) = 4;
        report "PASS";
        wait;
    end process;
end architecture;
