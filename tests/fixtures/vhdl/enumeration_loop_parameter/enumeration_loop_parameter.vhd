-- SPDX-License-Identifier: Apache-2.0
--
-- A loop parameter is an object whose type is the base type of its discrete
-- range (IEEE 1076-2019 10.10). Over CHARACTER (16.3) or an enumeration type
-- the design declares (5.2.2), it is a value of that type wherever its name
-- is used, as a variable of the type is, not the integer position its loop
-- steps: a case selector (10.9), an operand of a concatenation (9.2.5) and
-- of a comparison (9.2.3), an actual that selects among overloads on the
-- type (4.5, 12.5), the source of an assignment to a variable of the type
-- (10.6), and the argument of 'POS, 'VAL and 'IMAGE (16.2.2), in ascending
-- and descending ranges given by a type mark, a subtype, literals and
-- attributes. Each loop is checked against a variable of the type as the
-- control.

package enumloop is
  type color is (red, green, blue, yellow);
  type shade is (light, dark);
  type tri is ('0', '1', 'Z');
  type color_list is array (natural range <>) of color;
  type tri_vector is array (natural range <>) of tri;
  subtype lower is character range 'a' to 'e';
  subtype cool is color range green to blue;

  function describe (value : color) return string;
  function describe (value : shade) return string;
  function describe (value : tri) return string;
  function describe (value : character) return string;
  function weight (value : color) return integer;
  function weight (value : integer) return integer;
  function weight (value : character) return integer;
end package;

package body enumloop is
  function describe (value : color) return string is
  begin
    return "color:" & color'image(value);
  end function;

  function describe (value : shade) return string is
  begin
    return "shade:" & shade'image(value);
  end function;

  function describe (value : tri) return string is
  begin
    return "tri:" & tri'image(value);
  end function;

  function describe (value : character) return string is
  begin
    return "character:" & value;
  end function;

  function weight (value : color) return integer is
  begin
    return color'pos(value) + 10;
  end function;

  function weight (value : integer) return integer is
  begin
    return -value - 1;
  end function;

  function weight (value : character) return integer is
  begin
    return character'pos(value) + 1000;
  end function;
end package body;

use work.enumloop.all;

entity top is
end entity;

architecture test of top is
begin
  process
    variable c : color;
    variable s : shade;
    variable t : tri;
    variable ch : character;
    variable cl : color_list(1 to 2);
    variable tv : tri_vector(1 to 2);
    variable text2 : string(1 to 2);
    variable sel : character;
    variable count : integer;
  begin
    -- The control: a variable of each type.
    c := blue;
    s := dark;
    t := 'Z';
    ch := 'q';
    cl := c & red;
    tv := t & '1';
    text2 := "<" & ch;
    report "enumloop control " & describe(c) & " " & describe(s) & " "
      & describe(t) & " " & describe(ch) & " " & integer'image(weight(c))
      & " " & integer'image(weight(ch)) & " " & color'image(cl(1)) & ","
      & color'image(cl(2)) & " " & tri'image(tv(1)) & "," & tri'image(tv(2))
      & " " & text2;

    for i in color loop
      c := i;
      cl := i & red;
      case i is
        when red => sel := 'r';
        when green => sel := 'g';
        when blue | yellow => sel := 'b';
      end case;
      report "enumloop color " & color'image(c) & " " & describe(i) & " "
        & integer'image(weight(i)) & " " & integer'image(color'pos(i)) & " "
        & color'image(color'val(color'pos(i))) & " " & color'image(i) & " "
        & boolean'image(i = blue) & " " & boolean'image(i < blue) & " "
        & boolean'image(i >= green) & " " & color'image(cl(1)) & ","
        & color'image(cl(2)) & " " & sel;
    end loop;
    count := 0;
    for i in color'high downto color'low loop
      count := count + 1;
      report "enumloop color_reverse " & color'image(i) & " " & describe(i)
        & " " & integer'image(weight(i)) & " " & integer'image(count);
    end loop;
    for i in cool loop
      c := i;
      report "enumloop color_subtype " & color'image(c) & " " & describe(i)
        & " " & boolean'image(i = cool'high);
    end loop;
    for i in green to blue loop
      report "enumloop color_literals " & describe(i) & " "
        & integer'image(color'pos(i));
    end loop;
    for i in shade'high downto shade'low loop
      s := i;
      report "enumloop shade_reverse " & shade'image(s) & " " & describe(i)
        & " " & boolean'image(i = dark);
    end loop;
    for i in tri loop
      t := i;
      tv := i & '1';
      case i is
        when 'Z' => sel := 'z';
        when others => sel := 'v';
      end case;
      report "enumloop tri " & tri'image(t) & " " & describe(i) & " "
        & integer'image(tri'pos(i)) & " " & boolean'image(i = 'Z') & " "
        & boolean'image(i > '0') & " " & tri'image(tv(1)) & ","
        & tri'image(tv(2)) & " " & sel;
    end loop;
    for i in character range 'a' to 'c' loop
      ch := i;
      text2 := "<" & i;
      case i is
        when 'a' => sel := 'A';
        when 'b' => sel := 'B';
        when others => sel := '?';
      end case;
      report "enumloop character " & character'image(ch) & " " & text2 & " "
        & i & " " & describe(i) & " " & integer'image(weight(i)) & " " & sel
        & " " & boolean'image(i > 'a') & " " & boolean'image(i = 'b') & " "
        & character'image(character'val(character'pos(i) + 1)) & " "
        & integer'image(character'pos(i));
    end loop;
    for i in character range 'c' downto 'a' loop
      report "enumloop character_reverse " & i & " " & describe(i);
    end loop;
    for i in lower loop
      ch := i;
      report "enumloop character_subtype " & ch & " " & describe(i) & " "
        & integer'image(weight(i));
    end loop;
    for i in lower'high downto lower'low loop
      text2 := i & '!';
      report "enumloop character_attributes " & text2 & " " & describe(i);
    end loop;
    wait;
  end process;
end architecture;
