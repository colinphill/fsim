-- SPDX-License-Identifier: Apache-2.0
--
-- A name denotes a type, subtype or literal of STD.STANDARD by the
-- visibility rules where it is written (IEEE 1076-2019 12.3, 12.4, 13.2,
-- 16.3), never by its spelling:
-- * USER_NAMES declares types named BOOLEAN and CHARACTER that no unit of
--   the design uses, so BOOLEAN and CHARACTER below are STANDARD's: an array
--   indexed by BOOLEAN takes BOOLEAN indices, a BOOLEAN variable takes TRUE.
-- * The expanded names STD.STANDARD.T and, after `use std.standard;`,
--   STANDARD.T denote T of STANDARD as type marks and as attribute prefixes
--   (8.3, 16.2.2).
-- * A CHARACTER literal is a value of CHARACTER, its position the character
--   code, also where elaboration evaluates it: the conditions of generate
--   statements comparing CHARACTER generics with literals, and the
--   choices of a case-generate on a CHARACTER generic (9.3.2, 10.9, 11.8,
--   16.3).
package user_names is
  type boolean is (no, maybe, yes);
  type character is (alpha, beta);
end package;

-- A generic actual has the type of its formal (6.5.7.1): '1' and '$'
-- associated with CHARACTER generics are CHARACTER literals, through a
-- component binding and an entity instantiation alike.
entity stdid_leaf is
  generic (dollar : character := 'a'; one : character := 'b');
end entity;

architecture rtl of stdid_leaf is
begin
  process
    constant c : character := '$';
  begin
    -- After the reports of top's process.
    wait for 2 ns;
    report "stdid leaf " & boolean'image(dollar = '$') & " "
      & boolean'image(dollar = c) & " " & boolean'image(one = '1') & " "
      & integer'image(character'pos(one)) & " " & character'image(one);
    wait;
  end process;
end architecture;

use std.standard;
entity top is
  generic (
    one : character := '1';
    seven : character := '7');
end entity;

architecture test of top is
  type by_boolean is array (boolean) of integer;
  subtype digit is std.standard.character range '0' to '9';
  signal conditions : string(1 to 5) := "unset";
  signal chosen : integer := -1;
  component stdid_component is
    generic (value : character; digit_one : character);
  end component;
  for bound : stdid_component use entity work.stdid_leaf(rtl)
    generic map (dollar => value, one => digit_one);
begin
  bound : stdid_component generic map ('$', '1');
  direct : entity work.stdid_leaf generic map (one => '1', dollar => '$');

  -- Every condition holds, so the first branch is the one elaborated.
  checks : if one = '1' and one > '0' and one < '2' and seven = '7'
      and seven /= '1' and seven > one generate
    conditions <= "true ";
  else generate
    conditions <= "false";
  end generate;

  -- The choices have the type of the selector, CHARACTER (10.9, 11.8).
  selection : case seven generate
    when '1' | '2' => chosen <= 1;
    when '7' => chosen <= 7;
    when others => chosen <= 0;
  end generate;

  process
    variable hits : by_boolean := (others => 0);
    variable b : boolean := true;
    variable i : std.standard.integer := 5;
    variable n : standard.natural := 3;
  begin
    -- After the generated assignments.
    wait for 1 ns;
    report "stdid generate " & conditions & " " & integer'image(chosen);
    hits(true) := 1;
    hits(b) := hits(b) + 1;
    hits(not b) := 7;
    report "stdid hits " & integer'image(hits(true)) & " "
      & integer'image(hits(false)) & " " & integer'image(by_boolean'length);
    report "stdid expanded image " & std.standard.integer'image(i) & " "
      & std.standard.boolean'image(b) & " " & standard.natural'image(n);
    report "stdid expanded bounds "
      & integer'image(std.standard.natural'low) & " "
      & integer'image(std.standard.positive'low) & " "
      & integer'image(std.standard.integer'high) & " "
      & integer'image(standard.natural'high);
    report "stdid expanded positions "
      & integer'image(std.standard.character'pos('a')) & " "
      & integer'image(standard.boolean'pos(true)) & " "
      & boolean'image(std.standard.boolean'val(1)) & " "
      & character'image(std.standard.character'val(66));
    report "stdid digit " & integer'image(digit'pos(digit'high)) & " "
      & integer'image(character'pos(one)) & " "
      & character'image(seven);
    wait;
  end process;
end architecture;
