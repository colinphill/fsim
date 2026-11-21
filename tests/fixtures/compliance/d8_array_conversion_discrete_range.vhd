-- fsim: top=d8_array_conversion_discrete_range(test) std=1993
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-1993 7.3.5: array types with the same element type and index
-- types that are the same or closely related are closely related; an index
-- written as a discrete range has the type of its bounds (3.2.1).
entity d8_array_conversion_discrete_range is
end entity;

architecture test of d8_array_conversion_discrete_range is
    type a_t is array (1 to 3) of integer;
    type b_t is array (1 to 3) of integer;
begin
    p1 : process is
        variable a : a_t := (1, 2, 3);
        variable b : b_t;
    begin
        b := b_t(a);
        assert b(2) = 2 severity failure;
        report "PASS";
        wait;
    end process;
end architecture;
