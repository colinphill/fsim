-- fsim: top=d8_protected_body_region(test) std=2019
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 12.1, 5.6.2: a protected type declaration and its body form
-- one declarative region, so the body sees the private variables of the
-- declaration.
entity d8_protected_body_region is
end entity;

architecture test of d8_protected_body_region is
    type counter_t is protected
        private variable count : natural := 0;
        procedure bump;
        impure function get return natural;
    end protected;

    type counter_t is protected body
        procedure bump is
        begin
            count := count + 1;
        end procedure;
        impure function get return natural is
        begin
            return count;
        end function;
    end protected body;

    shared variable c : counter_t;
begin
    p1 : process is
    begin
        c.bump;
        c.bump;
        assert c.get = 2 severity failure;
        report "PASS";
        wait;
    end process;
end architecture;
