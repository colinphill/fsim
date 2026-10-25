-- fsim: top=vhdl_defaulted_call_index_20261025(test) std=1993
-- SPDX-License-Identifier: Apache-2.0
-- When every formal of F has a default, F(i, j, k) whose context takes one
-- element of the array result is F()(i, j, k) (IEEE 1076-2008 8.4,
-- 4.2.2.1), also after the call's subtype was asked in another context.
entity vhdl_defaulted_call_index_20261025 is
end entity;

architecture test of vhdl_defaulted_call_index_20261025 is
    type cube is array (natural range <>, natural range <>, natural range <>)
        of integer;
    subtype cube3 is cube (1 to 3, 1 to 3, 1 to 3);

    function f (a, b : integer := 3) return cube3 is
    begin
        if a = 3 and b = 3 then
            return (others => (others => (1, 2, 3)));
        end if;
        return (others => (others => (10, 20, 30)));
    end function;
begin
    process
        type row is array (1 to 4) of integer;
        variable q : row;
    begin
        q(1) := f(3, 0)(1, 1, 2);
        q(2) := f(1, 1, 1);
        q(3) := f(2, 2, 3);
        q(4) := f(3, 3, 2);
        assert q = (20, 1, 3, 2) report "defaulted call index" severity failure;
        report "PASS" severity note;
        wait;
    end process;
end architecture;
