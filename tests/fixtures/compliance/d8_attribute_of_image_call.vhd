-- fsim: top=d8_attribute_of_image_call(test) std=1993
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-1993 14.1, 16.1: the prefix of an attribute name may be a
-- function call; T'IMAGE(X) is a function whose value is an array.
entity d8_attribute_of_image_call is
end entity;

architecture test of d8_attribute_of_image_call is
begin
    p1 : process is
        variable i : integer := 12345;
        variable n : natural;
    begin
        n := integer'image(i)'length;
        assert n = 5 severity failure;
        report "PASS";
        wait;
    end process;
end architecture;
