-- fsim: top=d8_extended_type_mark(test) std=1993
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-1993 13.3.2: an extended identifier is distinct from every basic
-- identifier; a type mark written as an extended identifier denotes the type
-- declared with that extended identifier (10.3).
entity d8_extended_type_mark is
end entity;

architecture test of d8_extended_type_mark is
    type \T\ is (a, b);
    subtype \S\ is \T\;
    constant e : \T\ := b;
    constant f : \S\ := a;
begin
    process
    begin
        assert e = b and f = a severity failure;
        assert \T\'pos(e) = 1 severity failure;
        report "PASS";
        wait;
    end process;
end architecture;
