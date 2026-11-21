-- fsim: top=d8_boolean_literal_overload(test) std=1993
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-1993 4.3.1.1 / 3.1.1 and 10.3: TRUE and FALSE are enumeration
-- literals of BOOLEAN; a local enumeration type may declare literals of the
-- same designators, which overload them (10.3).
entity d8_boolean_literal_overload is
end entity;

architecture test of d8_boolean_literal_overload is
    type enum2 is (true, false);
    subtype btrue is boolean range true to true;
begin
    process
    begin
        assert enum2'pos(true) = 0 and enum2'pos(false) = 1 severity failure;
        assert boolean'pos(true) = 1 severity failure;
        assert btrue'left = true severity failure;
        report "PASS";
        wait;
    end process;
end architecture;
