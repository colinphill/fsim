-- fsim: top=vhdl_predefined_min_max_to_string(a) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2008 5.2.6, 5.3.2.4 and 9.2: the predefined MINIMUM, MAXIMUM
-- and TO_STRING operations, string literals of character enumerations,
-- matching equality with metavalues, and logical operators with one
-- scalar and one array operand.
library ieee;
use ieee.std_logic_1164.all;

entity vhdl_predefined_min_max_to_string is
end entity;

architecture a of vhdl_predefined_min_max_to_string is
  type my_char is ('a', 'b', 'c');
  type char_vector is array (natural range <>) of my_char;
  type int_vector is array (natural range <>) of integer;
  type freq is range 0 to 1000000
    units
      hz;
      khz = 1000 hz;
    end units;
  type freq_vector is array (natural range <>) of freq;
begin
  process
    variable i : integer;
    variable r : real := 1.23456;
    variable v : int_vector(1 to 3) := (1, 9, 4);
    variable c : char_vector(1 to 3) := "bca";
    variable f : freq_vector(1 to 2) := (3 khz, 20 hz);
    variable sa : bit := '1';
    variable sv : bit_vector(1 to 3) := "101";
    variable sl : std_ulogic := '1';
  begin
    assert maximum(3, 7) = 7 report "maximum integer" severity error;
    assert minimum(-3, 2) = -3 report "minimum integer" severity error;
    assert maximum(2.5, 1.0) = 2.5 report "maximum real" severity error;
    assert maximum(v) = 9 and minimum(v) = 1 report "integer array" severity error;
    assert minimum(int_vector'(5, -1, 2)) = -1 report "qualified aggregate" severity error;
    assert minimum(f) = 20 hz and maximum(f) = 3 khz report "physical array" severity error;
    assert to_string(c) = "bca" report "character enumeration string" severity error;
    assert to_string(char_vector'("abc")) = "abc" report "qualified string" severity error;
    assert to_string(f(1)) = "3000 hz" report "physical image" severity error;
    assert to_string(r, 2) = "1.23" report "real digits" severity error;
    assert to_string(r, "%1.1f") = "1.2" report "real format" severity error;
    assert to_string(50 ns, unit => us) = "0.05 us" report "time unit" severity error;
    assert to_string(character'('X')) = "X" report "character" severity error;
    assert (sa and sv) = "101" and (sa xnor sv) = "101" report "scalar and array" severity error;
    assert (sv nor sa) = "000" report "array nor scalar" severity error;
    assert (sl ?= 'Z') = 'X' and (sl ?= '-') = '1' report "matching metavalue" severity error;
    report "PASS";
    wait;
  end process;
end architecture;
