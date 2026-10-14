-- fsim: top=vhdl_block_boolean_package_body_batch58(sim) std=1993
-- SPDX-License-Identifier: Apache-2.0
-- A package-declared pure function with a separate body supplies a constrained block generic.
package b58_boolean_package_function is
    type boolean_v is array (integer range <>) of boolean;
    subtype boolean_4 is boolean_v(1 to 4);
    subtype boolean_8 is boolean_v(1 to 8);

    function package_value return boolean_4;
end package;

package body b58_boolean_package_function is
    function package_value return boolean_4 is
    begin
        return (true, false, true, false);
    end function;
end package body;

use work.b58_boolean_package_function.all;
entity vhdl_block_boolean_package_body_batch58 is
end entity;

use work.b58_boolean_package_function.all;
architecture sim of vhdl_block_boolean_package_body_batch58 is
begin
    package_body_concat : block
        generic (info : boolean_8);
        generic map (package_value & package_value);
    begin
        assert info = (true, false, true, false, true, false, true, false)
            report "separate package function body concatenation changed value"
            severity failure;
    end block;

    done : process
    begin
        wait for 1 ns;
        report "PASS" severity note;
        wait;
    end process;
end architecture;
