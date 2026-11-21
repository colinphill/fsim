-- fsim: top=d8_formal_part_conversion_inout(test) std=1993
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-1993 4.3.2.2: for an INOUT formal the formal part is a function
-- name applied to the formal and the actual part may apply another function
-- to its designator (`F1(b) => F2(x)`): F1 converts the formal to the type of
-- x, F2 converts x to the type of the formal, so the actual written has the
-- formal's type, not F1's result type.
entity d8_formal_part_conversion_inout is
end entity;

architecture test of d8_formal_part_conversion_inout is
    procedure p1 (a : in integer; b : inout integer) is
    begin
        b := a;
    end procedure;
    function f1 (i1 : in integer) return real is
    begin
        return real(i1) * 2.0;
    end function;
    function f2 (i2 : in real) return integer is
    begin
        return integer(i2);
    end function;
begin
    process
        variable x : real := 1.0;
    begin
        p1 (10, f1(b) => f2(x));
        assert x = 20.0 severity failure;
        report "PASS";
        wait;
    end process;
end architecture;
