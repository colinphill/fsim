-- fsim: top=d8_generic_access_deallocate(test) std=2019
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 6.5.3, 5.4.3: an interface access type has the operations
-- of an access type, among them DEALLOCATE.
package d8_gp is
    generic ( type dt; type access_type is access dt );
    procedure free ( p : inout access_type );
end package;

package body d8_gp is
    procedure free ( p : inout access_type ) is
    begin
        deallocate(p);
    end procedure;
end package body;

entity d8_generic_access_deallocate is
end entity;

architecture test of d8_generic_access_deallocate is
    type int_ptr is access integer;
    package i is new work.d8_gp generic map ( dt => integer, access_type => int_ptr );
begin
    p1 : process is
        variable v : int_ptr := new integer'(3);
    begin
        i.free(v);
        assert v = null severity failure;
        report "PASS";
        wait;
    end process;
end architecture;
