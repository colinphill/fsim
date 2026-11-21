-- fsim: top=d8_conditional_universal(test) std=2019
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 9.1, 9.3.6: a conditional expression whose values are
-- universal literals takes the type of its context.
entity d8_conditional_universal is
end entity;

architecture test of d8_conditional_universal is
    constant cond : boolean := true;
    constant c0 : integer := 10 when cond else 5;
    constant c1 : real := 1.5 when not cond else 2.5;
begin
    process
    begin
        assert c0 = 10 severity failure;
        assert c1 = 2.5 severity failure;
        report "PASS";
        wait;
    end process;
end architecture;
