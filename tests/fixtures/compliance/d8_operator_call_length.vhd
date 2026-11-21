-- fsim: top=d8_operator_call_length(test) std=1993
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-1993 6.1, 14.1: the prefix of 'LENGTH may be a function call,
-- including the call of an operator of a package written in functional
-- notation; the length of a null-array result is 0 (NUMERIC_STD "+").
library ieee;
use ieee.numeric_std.all;

entity d8_operator_call_length is
end entity;

architecture test of d8_operator_call_length is
begin
  process
    procedure check (left, right : in unsigned) is
    begin
      assert "+"(left, right)'length = 0 severity failure;
    end procedure;
    variable x : unsigned(1 to 0);
  begin
    check(x, x);
    report "PASS";
    wait;
  end process;
end architecture;
