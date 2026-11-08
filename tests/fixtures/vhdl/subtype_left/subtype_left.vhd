-- SPDX-License-Identifier: Apache-2.0
--
-- An object of a range-constrained STD_ULOGIC or CHARACTER subtype starts
-- at the subtype's leftmost value, T'LEFT (IEEE 1076-2008 6.4.2.3): X01 and
-- X01Z at 'X', UX01 at 'U' (IEEE 1164), `character range 'q' to 'z'` at
-- 'q'. The scalar attributes of those subtypes are STD_ULOGIC literals, and
-- CHARACTER'IMAGE of a control character is its identifier in lowercase
-- (IEEE 1076-2019 16.2.2, 16.3). A vector's index range is not a range of
-- its elements: std_logic_vector(3 downto 0) objects, alone or in a record,
-- start at all 'U'.
library ieee;
use ieee.std_logic_1164.all;

entity top is
end entity;

architecture sim of top is
  type st is (a, b, c);
  subtype sbc is st range b to c;
  subtype my01 is std_ulogic range 'X' to 'Z';
  subtype lc is character range 'q' to 'z';
  signal s1 : X01;
  signal s2 : my01;
  signal s3 : UX01;
  signal s4 : X01Z;
  signal s5 : lc;
  signal s6 : std_ulogic;
  type rec is record
    lane : std_logic_vector(3 downto 0);
    bit0 : X01;
  end record;
  signal r1 : rec;
  signal sv : std_logic_vector(3 downto 0);
begin
  process
    variable x : sbc;
    variable v1 : X01;
    variable v2 : my01;
    variable v3 : UX01;
    variable v4 : X01Z;
    variable c1 : character range 'q' to 'z';
    variable c2 : lc;
    variable c3 : character;
    variable u : std_ulogic;
    variable t : std_ulogic;
    variable vv : std_logic_vector(3 downto 0);
    variable vx : std_logic_vector(1 to 2);
  begin
    report "subtype sig " & std_ulogic'image(s1) & std_ulogic'image(s2)
      & std_ulogic'image(s3) & std_ulogic'image(s4) & character'image(s5)
      & std_ulogic'image(s6);
    report "subtype var " & st'image(x) & std_ulogic'image(v1)
      & std_ulogic'image(v2) & std_ulogic'image(v3) & std_ulogic'image(v4)
      & std_ulogic'image(u);
    report "subtype chr " & character'image(c1) & character'image(c2)
      & character'image(c3) & character'image(DEL)
      & character'image(character'val(130)) & character'image(LF)
      & character'image('A');
    t := X01'left;
    report "subtype att " & std_ulogic'image(X01'left)
      & std_ulogic'image(X01'right) & std_ulogic'image(UX01'left)
      & std_ulogic'image(my01'high) & std_ulogic'image(t)
      & std_ulogic'image(std_ulogic'left) & std_ulogic'image(std_ulogic'right);
    report "subtype vec " & to_string(sv) & to_string(r1.lane)
      & std_ulogic'image(r1.bit0) & to_string(vv) & to_string(vx);
    wait;
  end process;
end architecture;
