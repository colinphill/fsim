-- SPDX-License-Identifier: Apache-2.0
--
-- TO_HSTRING and TO_OSTRING of run-time vectors, as the bodies of
-- STD_LOGIC_1164 and NUMERIC_STD define them (IEEE 1076-2019 16.7, 5.3.2.4
-- for BIT_VECTOR): the vector is padded on the left to whole groups of 4
-- (3) elements with 'Z' when its leftmost element is 'Z', with '0' otherwise
-- (a SIGNED value with its leftmost element), each group is converted by
-- TO_X01Z ('L' and 'H' are 0 and 1; 'U', 'W' and '-' are 'X'), and a group
-- that is not all 0 and 1 is the digit 'Z' when all its elements are 'Z'
-- and 'X' otherwise. The expected digits are derived from that rule, for
-- example h02: "Z0ZZ" is not all 'Z' and not all 0/1, so X; h09: "1ZZ" pads
-- to "01ZZ", so X; h10: "Z01" pads with 'Z' to "ZZ01", so X; h08: "ZZZ" pads
-- to "ZZZZ", so Z; s02: the SIGNED "101" extends to "1101", D.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity top is
end entity;

architecture sim of top is
begin
  process
    variable h01 : std_logic_vector(7 downto 0);
    variable h02 : std_logic_vector(3 downto 0);
    variable h03 : std_logic_vector(3 downto 0);
    variable h04 : std_logic_vector(3 downto 0);
    variable h05 : std_logic_vector(3 downto 0);
    variable h06 : std_logic_vector(3 downto 0);
    variable h07 : std_logic_vector(3 downto 0);
    variable h08 : std_logic_vector(2 downto 0);
    variable h09 : std_logic_vector(2 downto 0);
    variable h10 : std_logic_vector(2 downto 0);
    variable h11 : std_logic_vector(4 downto 0);
    variable h12 : std_logic_vector(0 downto 0);
    variable h13 : std_logic_vector(0 downto 0);
    variable h14 : std_logic_vector(0 downto 0);
    variable h15 : std_logic_vector(7 downto 0);
    variable h16 : std_logic_vector(7 downto 0);
    variable o01 : std_logic_vector(5 downto 0);
    variable o02 : std_logic_vector(5 downto 0);
    variable o03 : std_logic_vector(3 downto 0);
    variable o04 : std_logic_vector(3 downto 0);
    variable o05 : std_logic_vector(2 downto 0);
    variable o06 : std_logic_vector(2 downto 0);
    variable o07 : std_logic_vector(1 downto 0);
    variable o08 : std_logic_vector(1 downto 0);
    variable s01 : signed(3 downto 0);
    variable s02 : signed(2 downto 0);
    variable s03 : signed(2 downto 0);
    variable s04 : signed(2 downto 0);
    variable s05 : signed(2 downto 0);
    variable s06 : signed(1 downto 0);
    variable s07 : signed(1 downto 0);
    variable s08 : unsigned(2 downto 0);
    variable s09 : signed(2 downto 0);
    variable b01 : bit_vector(2 downto 0);
    variable b02 : bit_vector(4 downto 0);
    variable b03 : bit_vector(3 downto 0);
    variable l1 : std_logic_vector(69 downto 0);
    variable l2 : std_logic_vector(69 downto 0);
  begin
    h01 := "0000ZZZZ";
    report "wide h01 " & to_hstring(h01);
    h02 := "Z0ZZ";
    report "wide h02 " & to_hstring(h02);
    h03 := "XXXX";
    report "wide h03 " & to_hstring(h03);
    h04 := "UWHL";
    report "wide h04 " & to_hstring(h04);
    h05 := "HL10";
    report "wide h05 " & to_hstring(h05);
    h06 := "-1HL";
    report "wide h06 " & to_hstring(h06);
    h07 := "ZZZZ";
    report "wide h07 " & to_hstring(h07);
    h08 := "ZZZ";
    report "wide h08 " & to_hstring(h08);
    h09 := "1ZZ";
    report "wide h09 " & to_hstring(h09);
    h10 := "Z01";
    report "wide h10 " & to_hstring(h10);
    h11 := "10110";
    report "wide h11 " & to_hstring(h11);
    h12 := "H";
    report "wide h12 " & to_hstring(h12);
    h13 := "L";
    report "wide h13 " & to_hstring(h13);
    h14 := "Z";
    report "wide h14 " & to_hstring(h14);
    h15 := "XZZZ0101";
    report "wide h15 " & to_hstring(h15);
    h16 := "ZZZZZZZZ";
    report "wide h16 " & to_hstring(h16);
    o01 := "ZZZZZZ";
    report "wide o01 " & to_ostring(o01);
    o02 := "ZZ0ZZZ";
    report "wide o02 " & to_ostring(o02);
    o03 := "1010";
    report "wide o03 " & to_ostring(o03);
    o04 := "Z101";
    report "wide o04 " & to_ostring(o04);
    o05 := "HHL";
    report "wide o05 " & to_ostring(o05);
    o06 := "UXW";
    report "wide o06 " & to_ostring(o06);
    o07 := "ZZ";
    report "wide o07 " & to_ostring(o07);
    o08 := "01";
    report "wide o08 " & to_ostring(o08);
    s01 := "1010";
    report "wide s01 " & to_hstring(s01);
    s02 := "101";
    report "wide s02 " & to_hstring(s02);
    s03 := "010";
    report "wide s03 " & to_hstring(s03);
    s04 := "X10";
    report "wide s04 " & to_hstring(s04);
    s05 := "Z10";
    report "wide s05 " & to_hstring(s05);
    s06 := "10";
    report "wide s06 " & to_ostring(s06);
    s07 := "01";
    report "wide s07 " & to_ostring(s07);
    s08 := "101";
    report "wide s08 " & to_hstring(s08);
    s09 := "ZZZ";
    report "wide s09 " & to_hstring(s09);
    b01 := "101";
    report "wide b01 " & to_hstring(b01);
    b02 := "11111";
    report "wide b02 " & to_hstring(b02);
    b03 := "1010";
    report "wide b03 " & to_ostring(b03);
    l1 := (others => 'Z');
    report "wide l1 " & to_hstring(l1);
    l2 := (others => 'Z');
    l2(0) := '0';
    report "wide l2 " & to_hstring(l2);
    wait;
  end process;
end architecture;
