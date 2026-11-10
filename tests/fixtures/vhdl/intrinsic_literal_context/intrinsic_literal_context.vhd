-- SPDX-License-Identifier: Apache-2.0
--
-- A character literal of BOOLEAN, BIT, CHARACTER or STD_ULOGIC denotes the
-- literal of the type its context gives it (IEEE 1076-2019 9.3.2, 12.5): its
-- position is the one in that type's literal list (16.3, 16.7), 'U' = 0 to
-- '-' = 8 for STD_ULOGIC, never its CHARACTER code nor a BIT value. The
-- context is the prefix of 'POS, 'VAL and 'IMAGE (16.2.2), the type mark of
-- a range constraint, the index subtype of an array type (5.3.2.2) and the
-- discrete range of a loop parameter (10.10). A loop parameter is a constant
-- of the base type of its discrete range (10.10): a STD_ULOGIC, BIT or
-- BOOLEAN one is a value of that type in a comparison, an assignment, an
-- actual parameter, a case selector, an index and a concatenation, with
-- CHARACTER as a control. A STD_ULOGIC index, a variable or a loop
-- parameter, selects the element at its position (8.4). The array types
-- indexed by STD_LOGIC_1164's own subtypes, in a package the entity uses,
-- fold the same wherever the workspace lives. A character literal index of
-- such an array selects by its position too, whatever subtype of
-- STD_ULOGIC indexes it. STD.STANDARD's types keep their identity when
-- named through the package, `standard.character` after `use std.standard;`
-- or `use std.all;` (8.3, 12.4).

library ieee;
use ieee.std_logic_1164.all;

package intlit is
  type by_x01z is array (X01Z) of integer;
  type by_ux01 is array (UX01) of integer;
  subtype xz is std_ulogic range 'X' to 'Z';
  type by_xz is array (xz) of integer;
  type by_char is array (character range 'a' to 'e') of integer;
  constant one_pos : integer := std_ulogic'pos('1');
  constant z_pos : integer := std_ulogic'pos('Z');
  constant dash_pos : integer := std_ulogic'pos('-');
  constant a_pos : integer := character'pos('a');
  constant bit_pos : integer := bit'pos('1');
  constant true_pos : integer := boolean'pos(true);
  constant x01z_right_pos : integer := std_ulogic'pos(X01Z'right);
  constant x01z_len : integer := by_x01z'length;
  constant ux01_len : integer := by_ux01'length;
  constant xz_len : integer := by_xz'length;
  constant char_len : integer := by_char'length;
  function x01z_length return integer;
  function ux01_length return integer;
end package;

package body intlit is
  function x01z_length return integer is
  begin
    return by_x01z'length;
  end function;

  function ux01_length return integer is
  begin
    return by_ux01'length;
  end function;
end package body;

use std.standard;

package intstd is
  type by_std_char is array (standard.character range 'a' to 'e') of integer;
  subtype std_lower is standard.character range 'a' to 'z';
  constant std_q : standard.character := 'q';
  constant std_q_pos : integer := character'pos(std_q);
end package;

use std.all;

package intall is
  subtype all_lower is standard.character range 'a' to 'z';
  type by_all_char is array (standard.character range 'p' to 'r') of integer;
  constant all_r : standard.character := 'r';
  function all_pos (value : standard.character) return integer;
end package;

package body intall is
  function all_pos (value : standard.character) return integer is
  begin
    return character'pos(value);
  end function;
end package body;

library ieee;
use ieee.std_logic_1164.all;
use std.standard;
use work.intlit.all;
use work.intstd.all;
use work.intall.all;

entity top is
end entity;

architecture test of top is
  function same_logic (value : std_ulogic) return std_ulogic is
  begin
    return value;
  end function;

  function same_bit (value : bit) return bit is
  begin
    return value;
  end function;

  function same_boolean (value : boolean) return boolean is
  begin
    return value;
  end function;
begin
  process
    variable count : integer;
    type by_logic is array (std_ulogic) of integer;
    type by_bit is array (bit) of integer;
    variable logic_hits : by_logic := (others => 0);
    variable bit_hits : by_bit := (others => 0);
    variable char_hits : by_char := (others => 0);
    variable x01z_hits : by_x01z := (others => 0);
    variable ux01_hits : by_ux01 := (others => 0);
    variable xz_hits : by_xz := (others => 0);
    variable logic_slice : by_logic := (others => 0);
    variable std_char_hits : by_std_char := (others => 0);
    variable std_char : standard.character := 'b';
    variable std_bit : standard.bit := '1';
    variable std_flag : standard.boolean := true;
    variable std_count : integer := 0;
    variable all_hits : by_all_char := (others => 0);
    variable logic_value : std_ulogic;
    variable bit_value : bit;
    variable flag : boolean;
    variable char_value : character;
    variable logic_pair : std_ulogic_vector(1 to 2);
    variable bit_pair : bit_vector(1 to 2);
    variable selected : character;
  begin
    report "intlit length " & integer'image(x01z_length) & " "
      & integer'image(ux01_length) & " " & integer'image(by_x01z'length)
      & " " & integer'image(by_ux01'length) & " "
      & integer'image(by_xz'length) & " " & integer'image(by_char'length);
    report "intlit constant_length " & integer'image(x01z_len) & " "
      & integer'image(ux01_len) & " " & integer'image(xz_len) & " "
      & integer'image(char_len);
    report "intlit constant_pos " & integer'image(one_pos) & " "
      & integer'image(z_pos) & " " & integer'image(dash_pos) & " "
      & integer'image(a_pos) & " " & integer'image(bit_pos) & " "
      & integer'image(true_pos) & " " & integer'image(x01z_right_pos);
    report "intlit pos " & integer'image(std_ulogic'pos('U')) & " "
      & integer'image(std_ulogic'pos('1')) & " "
      & integer'image(std_ulogic'pos('Z')) & " "
      & integer'image(std_ulogic'pos('-')) & " "
      & integer'image(character'pos('a')) & " "
      & integer'image(bit'pos('1'));
    report "intlit val " & std_ulogic'image(std_ulogic'val(4)) & " "
      & std_ulogic'image(std_ulogic'val(8)) & " "
      & character'image(character'val(98));
    report "intlit x01 " & std_ulogic'image(X01'left)
      & std_ulogic'image(X01'right) & std_ulogic'image(X01'low)
      & std_ulogic'image(X01'high);
    report "intlit x01z " & std_ulogic'image(X01Z'left)
      & std_ulogic'image(X01Z'right) & std_ulogic'image(X01Z'low)
      & std_ulogic'image(X01Z'high);
    report "intlit ux01 " & std_ulogic'image(UX01'left)
      & std_ulogic'image(UX01'right) & std_ulogic'image(UX01'low)
      & std_ulogic'image(UX01'high);
    report "intlit ux01z " & std_ulogic'image(UX01Z'left)
      & std_ulogic'image(UX01Z'right) & std_ulogic'image(UX01Z'low)
      & std_ulogic'image(UX01Z'high);
    report "intlit image " & std_ulogic'image('Z') & " "
      & std_ulogic'image('-') & " " & character'image('a') & " "
      & character'image('Z');
    report "intlit value " & std_ulogic'image(std_ulogic'value("'H'")) & " "
      & integer'image(std_ulogic'pos(std_ulogic'value("'W'"))) & " "
      & character'image(character'value("'q'"));
    count := 0;
    for i in X01Z loop
      report "intlit x01z_loop " & std_ulogic'image(i) & " "
        & integer'image(std_ulogic'pos(i));
      count := count + 1;
    end loop;
    report "intlit x01z_count " & integer'image(count);
    for i in std_ulogic loop
      report "intlit std_ulogic_loop " & std_ulogic'image(i) & " "
        & integer'image(std_ulogic'pos(i));
    end loop;
    for i in X01Z loop
      logic_value := i;
      logic_pair := i & '1';
      logic_hits(i) := logic_hits(i) + 1;
      case i is
        when 'X' => selected := 'x';
        when 'Z' => selected := 'z';
        when others => selected := 'o';
      end case;
      report "intlit x01z_uses " & std_ulogic'image(logic_value) & " "
        & boolean'image(i = 'Z') & " " & boolean'image(i /= 'X') & " "
        & std_ulogic'image(same_logic(i)) & " "
        & std_ulogic'image(logic_pair(1)) & std_ulogic'image(logic_pair(2))
        & " " & selected;
    end loop;
    report "intlit x01z_hits " & integer'image(logic_hits('U')) & " "
      & integer'image(logic_hits('X')) & " " & integer'image(logic_hits('0'))
      & " " & integer'image(logic_hits('1')) & " "
      & integer'image(logic_hits('Z')) & " " & integer'image(logic_hits('W'))
      & " " & integer'image(logic_hits('L')) & " "
      & integer'image(logic_hits('H')) & " " & integer'image(logic_hits('-'));
    -- A STD_ULOGIC index selects by its position (8.4, 16.7), whether it
    -- is a variable or a loop parameter, and in an array indexed by a
    -- subtype of STD_ULOGIC.
    logic_value := 'Z';
    logic_hits(logic_value) := logic_hits(logic_value) + 10;
    logic_value := 'W';
    logic_hits(logic_value) := logic_hits(logic_value) + 20;
    report "intlit logic_index " & integer'image(logic_hits('Z')) & " "
      & integer'image(logic_hits('W')) & " "
      & integer'image(logic_hits(logic_value));
    for i in X01Z loop
      x01z_hits(i) := x01z_hits(i) + std_ulogic'pos(i);
    end loop;
    report "intlit x01z_index " & integer'image(x01z_hits('X')) & " "
      & integer'image(x01z_hits('0')) & " " & integer'image(x01z_hits('1'))
      & " " & integer'image(x01z_hits('Z'));
    ux01_hits('U') := 1;
    ux01_hits('X') := 2;
    ux01_hits('0') := 3;
    ux01_hits('1') := 4;
    xz_hits('1') := 30;
    xz_hits('Z') := 40;
    xz_hits('X') := 10;
    xz_hits('0') := 20;
    report "intlit literal_index " & integer'image(ux01_hits('1')) & " "
      & integer'image(ux01_hits('U')) & " " & integer'image(ux01_hits('X'))
      & " " & integer'image(ux01_hits('0')) & " "
      & integer'image(xz_hits('1')) & " " & integer'image(xz_hits('X')) & " "
      & integer'image(xz_hits('0')) & " " & integer'image(xz_hits('Z'));
    -- So do the bounds of a slice's discrete range (8.5).
    ux01_hits('0' to '1') := (50, 60);
    logic_slice('0' to '1') := (7, 8);
    report "intlit literal_slice " & integer'image(ux01_hits('X')) & " "
      & integer'image(ux01_hits('0')) & " " & integer'image(ux01_hits('1'))
      & " " & integer'image(logic_slice('U')) & " "
      & integer'image(logic_slice('X')) & " "
      & integer'image(logic_slice('0')) & " "
      & integer'image(logic_slice('1'));
    for i in std_ulogic range 'X' to 'Z' loop
      logic_value := i;
      report "intlit range_uses " & std_ulogic'image(logic_value) & " "
        & boolean'image(i = 'Z') & " " & integer'image(std_ulogic'pos(i));
    end loop;
    for i in bit loop
      bit_value := i;
      bit_pair := i & '1';
      bit_hits(i) := bit_hits(i) + 1;
      case i is
        when '0' => selected := 'l';
        when '1' => selected := 'h';
      end case;
      report "intlit bit_uses " & bit'image(bit_value) & " "
        & boolean'image(i = '1') & " " & bit'image(same_bit(i)) & " "
        & bit'image(bit_pair(1)) & bit'image(bit_pair(2)) & " " & selected
        & " " & bit'image(i);
    end loop;
    report "intlit bit_hits " & integer'image(bit_hits('0')) & " "
      & integer'image(bit_hits('1'));
    for i in boolean loop
      flag := i;
      report "intlit boolean_uses " & boolean'image(flag) & " "
        & boolean'image(i = true) & " " & boolean'image(not i) & " "
        & boolean'image(same_boolean(i)) & " " & boolean'image(i);
    end loop;
    for i in character range 'a' to 'c' loop
      char_value := i;
      char_hits(i) := char_hits(i) + 1;
      report "intlit character_uses " & character'image(char_value) & " "
        & boolean'image(i = 'b') & " " & character'image(i) & " "
        & integer'image(character'pos(i));
    end loop;
    report "intlit character_hits " & integer'image(char_hits('a')) & " "
      & integer'image(char_hits('b')) & " " & integer'image(char_hits('c'))
      & " " & integer'image(char_hits('d')) & " "
      & integer'image(char_hits('e'));
    std_char_hits(std_char) := 5;
    std_char_hits('c') := character'pos('c');
    for i in std_lower range 'a' to 'c' loop
      std_count := std_count + character'pos(i);
    end loop;
    report "intlit standard_expanded " & integer'image(std_char_hits('b'))
      & " " & integer'image(std_char_hits('c')) & " "
      & character'image(std_q) & " " & integer'image(std_q_pos) & " "
      & integer'image(std_count) & " " & boolean'image(std_flag) & " "
      & bit'image(std_bit) & " " & character'image(std_lower'high) & " "
      & integer'image(by_std_char'length);
    all_hits(all_r) := 7;
    report "intlit standard_all " & character'image(all_lower'high) & " "
      & integer'image(by_all_char'length) & " " & integer'image(all_pos(all_r))
      & " " & integer'image(all_hits('r')) & " " & integer'image(all_hits('p'));
    wait;
  end process;
end architecture;
