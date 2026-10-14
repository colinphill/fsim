-- fsim: top=vhdl_deferred_constant_bounds_batch58(sim) std=1993
-- SPDX-License-Identifier: Apache-2.0
-- A deferred unconstrained array constant takes its bounds from its completion.
package b58_deferred_constant_bounds_pkg is
    constant c : bit_vector;
    pure function f (
        i : bit_vector;
        l : integer range 1 to integer'high
    ) return bit_vector;
end package;

package body b58_deferred_constant_bounds_pkg is
    pure function f (
        i : bit_vector;
        l : integer range 1 to integer'high
    ) return bit_vector is
        variable v : bit_vector(i'length-1 downto 0);
    begin
        v := i;
        return v(l-1 downto 0);
    end function f;
    constant c : bit_vector := f(X"1F", 5);
end package body;

use work.b58_deferred_constant_bounds_pkg.all;
entity vhdl_deferred_constant_bounds_batch58 is
end entity;

use work.b58_deferred_constant_bounds_pkg.all;
architecture sim of vhdl_deferred_constant_bounds_batch58 is
begin
    check: process is
    begin
        assert not c'ascending
                and c'left = 4
                and c'right = 0
                and c'length = 5
                and c = "11111"
            report "deferred constant did not retain its completion bounds/value"
            severity failure;
        report "PASS";
        wait;
    end process;
end architecture;
