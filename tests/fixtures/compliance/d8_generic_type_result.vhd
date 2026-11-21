-- fsim: top=d8_generic_type_result(test) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2008 4.7, 4.9, 6.5.3: functions whose result is a generic type
-- of a package, and aggregates of such elements, are not typed before the
-- instantiation.
package d8_gen is
    generic (type t; function mk (x : integer) return t);
    function twice (x : integer) return t;
    type tv is array (natural range <>) of t;
    function pair (x : integer) return tv;
end package;

package body d8_gen is
    function twice (x : integer) return t is
    begin
        return mk(x * 2);
    end function;
    function pair (x : integer) return tv is
    begin
        return (mk(x), mk(x + 1));
    end function;
end package body;

entity d8_generic_type_result is
end entity;

architecture test of d8_generic_type_result is
    function id (x : integer) return integer is
    begin
        return x;
    end function;
    package ints is new work.d8_gen generic map (t => integer, mk => id);
    use ints.all;
begin
    process
    begin
        assert twice(3) = 6 severity failure;
        assert pair(1)(1) = 2 severity failure;
        report "PASS";
        wait;
    end process;
end architecture;
