-- fsim: top=d8_expanded_operator_names(test) std=1993
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-1993 6.3 / 2.3: an expanded name selects an operator (unary,
-- binary, or implicitly declared) from its package; an alias with a
-- signature (4.3.3) of such an operator denotes it.
package d8_ops is
    type state is (idle, busy);
    function "-" (a, b : bit_vector) return bit_vector;
    function "-" (a : bit_vector) return bit_vector;
end package;

package body d8_ops is
    function "-" (a, b : bit_vector) return bit_vector is
    begin
        return a;
    end function;
    function "-" (a : bit_vector) return bit_vector is
    begin
        return not a;
    end function;
end package body;

entity d8_expanded_operator_names is
end entity;

architecture test of d8_expanded_operator_names is
    alias "=" is work.d8_ops."=" [work.d8_ops.state, work.d8_ops.state return boolean];
begin
    process
        variable v : bit_vector(1 downto 0) := "01";
        variable st : work.d8_ops.state := work.d8_ops.busy;
        variable r : bit_vector(1 downto 0);
    begin
        r := work.d8_ops."-"(v);
        assert r = "10" severity failure;
        r := work.d8_ops."-"(v, v);
        assert r = "01" severity failure;
        assert work.d8_ops."="(st, work.d8_ops.busy) severity failure;
        assert st = work.d8_ops.busy severity failure;
        report "PASS";
        wait;
    end process;
end architecture;
