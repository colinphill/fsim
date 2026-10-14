-- fsim: top=vhdl_nested_unconstrained_actual_batch57(sim) std=1993
-- SPDX-License-Identifier: Apache-2.0
-- An unconstrained BIT_VECTOR formal keeps its actual bounds through a nested
-- function call used by a second unconstrained formal.
package vhdl_nested_unconstrained_actual_batch57_pkg is
    procedure wait_until(signal sig : in bit_vector; val : bit_vector);
end package;

package body vhdl_nested_unconstrained_actual_batch57_pkg is
    function identity(x : bit_vector) return bit_vector is
    begin
        return x;
    end function;

    procedure wait_until(signal sig : in bit_vector; val : bit_vector) is
    begin
        wait until sig = identity(val);
    end procedure;
end package body;

entity vhdl_nested_unconstrained_actual_batch57 is
end entity;

use work.vhdl_nested_unconstrained_actual_batch57_pkg.all;

architecture sim of vhdl_nested_unconstrained_actual_batch57 is
    signal value : bit_vector(7 downto 0);
begin
    value <= X"10" after 1 ns, X"BC" after 2 ns;

    check : process is
    begin
        wait_until(value, X"10");
        assert now = 1 ns severity failure;
        wait_until(value, X"BC");
        assert now = 2 ns severity failure;
        assert false report "PASS" severity note;
        wait;
    end process;
end architecture;
