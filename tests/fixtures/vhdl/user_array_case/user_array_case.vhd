-- SPDX-License-Identifier: Apache-2.0
--
-- A case statement whose expression is of a user-declared one-dimensional
-- array type of a character type (IEEE 1076-2019 10.9): the choices are
-- locally static strings, a string literal's type comes from its context
-- (9.3.2), so each character is a literal of the element type, and the first
-- character is the leftmost element whatever the direction of the index
-- range. Arrays of BIT, of STD_ULOGIC, of an enumeration of character
-- literals, and of an enumeration that also has an identifier literal.
library ieee;
use ieee.std_logic_1164.all;

entity top is
end entity;

architecture a of top is
  type bits is array (natural range <>) of bit;
  type uls is array (natural range <>) of std_ulogic;
  type lvl is ('a', 'b', 'c');
  type lvls is array (natural range <>) of lvl;
  type tone is ('x', 'y', 'z', mute);
  type tones is array (natural range <>) of tone;

  subtype b2 is bits(1 downto 0);
  subtype a2 is bits(0 to 1);
  subtype u2 is uls(0 to 1);
  subtype l2 is lvls(1 to 2);
  subtype d2 is lvls(2 downto 1);
  subtype t2 is tones(0 to 1);

  -- Every value is a choice: no OTHERS is needed.
  procedure pb(x : b2) is
  begin
    case x is
      when "00" => report "uacase b2 00";
      when "01" | "10" => report "uacase b2 01|10";
      when "11" => report "uacase b2 11";
    end case;
  end procedure;

  procedure pa(x : a2) is
  begin
    case x is
      when "00" | "11" => report "uacase a2 equal";
      when others => report "uacase a2 differ";
    end case;
  end procedure;

  procedure pu(x : u2) is
  begin
    case x is
      when "0Z" => report "uacase u2 0Z";
      when "1-" => report "uacase u2 1-";
      when others => report "uacase u2 other";
    end case;
  end procedure;

  procedure pl(x : l2) is
  begin
    case x is
      when "ab" => report "uacase l2 ab";
      when "ba" | "cc" => report "uacase l2 ba|cc";
      when others => report "uacase l2 other";
    end case;
  end procedure;

  procedure pd(x : d2) is
  begin
    case x is
      when "ca" => report "uacase d2 ca";
      when "ac" => report "uacase d2 ac";
      when others => report "uacase d2 other";
    end case;
  end procedure;

  procedure pt(x : t2) is
  begin
    case x is
      when "xy" => report "uacase t2 xy";
      when "yz" => report "uacase t2 yz";
      when others => report "uacase t2 other";
    end case;
  end procedure;
begin
  process
    variable v1 : l2;
    variable v2 : d2;
  begin
    pb("00");
    pb("10");
    pb("11");
    pa("11");
    pa("01");
    pu("0Z");
    pu("1-");
    pu("1H");
    v1 := (1 => 'a', 2 => 'b');
    pl(v1);
    pl("cc");
    pl("ca");
    v2 := (2 => 'c', 1 => 'a');
    pd(v2);
    v2 := (2 => 'a', 1 => 'c');
    pd(v2);
    pd("bb");
    pt("xy");
    pt("yz");
    pt("zy");
    wait;
  end process;
end architecture;
