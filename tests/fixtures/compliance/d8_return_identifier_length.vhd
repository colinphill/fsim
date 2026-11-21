-- fsim: top=d8_return_identifier_length(tb) std=2019
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 4.2.1: the return identifier of a function denotes the
-- subtype the call's context gives its result, so `return_value'length` is
-- the length of the target the call is assigned to (here 8), and the
-- numeric_std TO_UNSIGNED of it has no length of its own before the call.
library ieee;
use ieee.numeric_std.all;

entity d8_return_identifier_length is
end entity;

architecture tb of d8_return_identifier_length is
begin
  main: process
    function meaning_of_life return return_value of unsigned is
    begin
      return to_unsigned(42, return_value'length);
    end function;

    variable the_answer : unsigned(7 downto 0);
    variable wide : unsigned(11 downto 0);
  begin
    the_answer := meaning_of_life;
    assert the_answer = 42 severity failure;
    wide := meaning_of_life;
    assert wide = 42 severity failure;
    report "PASS";
    wait;
  end process;
end architecture;
