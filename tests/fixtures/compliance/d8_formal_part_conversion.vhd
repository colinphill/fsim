-- fsim: top=d8_formal_part_conversion(test) std=1993
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-1993 4.3.2.2: a formal part is a formal designator, a function
-- name or type mark applied to one, and a formal designator may be a part
-- (an element) of a formal; the actual has the type of that part or of the
-- conversion's result.
entity d8_formal_part_conversion is
end entity;

architecture test of d8_formal_part_conversion is
    function f1 (i : integer) return real is
    begin
        return real(i) * 2.0;
    end function;
    procedure p1 (a : in integer; b : out integer) is
    begin
        b := a;
    end procedure;
    procedure pv (bv : out bit_vector(1 downto 0)) is
    begin
        bv := "10";
    end procedure;
begin
    process
        variable x : real := 1.0;
        variable s0, s1 : bit;
    begin
        p1 (10, f1(b) => x);
        assert x = 20.0 severity failure;
        pv (bv(1) => s1, bv(0) => s0);
        assert s1 = '1' and s0 = '0' severity failure;
        report "PASS";
        wait;
    end process;
end architecture;
