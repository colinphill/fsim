-- fsim: top=d8_alias_of_enumeration_type(test) std=1993
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-1993 4.3.3: an alias of an enumeration type name also declares
-- the enumeration literals of that type.
package d8_alias_types is
    type system_status is (idle, active, overloaded);
end package;

entity d8_alias_of_enumeration_type is
end entity;

architecture test of d8_alias_of_enumeration_type is
    alias status_type is work.d8_alias_types.system_status;
begin
    p1 : process is
        variable status : status_type := idle;
    begin
        assert status = work.d8_alias_types.idle severity failure;
        status := active;
        assert status = work.d8_alias_types.active severity failure;
        report "PASS";
        wait;
    end process;
end architecture;
