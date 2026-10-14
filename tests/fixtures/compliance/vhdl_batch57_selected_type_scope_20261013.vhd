-- fsim: top=vhdl_batch57_selected_type_scope_20261013(test) std=2008
-- SPDX-License-Identifier: Apache-2.0
package b57_selected_type_scope_pkg is
    type value_t is array (integer range <>) of integer;
    procedure run;
end package;

package body b57_selected_type_scope_pkg is
    procedure run is
        type value_t is array (integer range <>) of bit;
        variable package_value
            : work.b57_selected_type_scope_pkg.value_t(1 to 2);
        variable local_value : value_t(1 to 2);
    begin
        package_value := (31, 32);
        local_value := ('0', '1');
        assert package_value = (31, 32)
            report "package-selected type bound to a package-body local shadow"
            severity failure;
        assert local_value = ('0', '1')
            report "the package-body local type lost its lexical binding"
            severity failure;
    end procedure;
end package body;

library work;
use work.b57_selected_type_scope_pkg.all;
entity vhdl_batch57_selected_type_scope_20261013 is
end entity;

architecture test of vhdl_batch57_selected_type_scope_20261013 is
begin
    check: process is
    begin
        run;
        report "PASS";
        wait;
    end process;
end architecture;
