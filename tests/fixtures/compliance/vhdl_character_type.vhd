-- fsim: top=character_type(sim) std=2008
-- IEEE 1076-2008 5.2.2.1 and 16.3: STD.STANDARD CHARACTER values, from
-- graphic literals, non-graphic names (NUL, DEL, C128), and logic-looking
-- literals ('0') whose context is CHARACTER; 9.2.5: concatenation with a
-- null array operand.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity character_type is
end entity;

architecture sim of character_type is
  type pair_t is record
    tag   : character;
    count : integer;
  end record;
  type chars_t is array (1 to 4) of character;
  type booleans_t is array (integer range <>) of boolean;
  subtype booleans_2_t is booleans_t(1 to 2);
  subtype booleans_4_t is booleans_t(1 to 4);
  subtype booleans_null_t is booleans_t(1 to 0);
  type integers_t is array (integer range <>) of integer;
  subtype integers_1_t is integers_t(1 to 1);
  subtype integers_null_t is integers_t(4 to 3);

  constant letter_c : character := 'C';
  signal pair : pair_t;
begin
  process
    variable failures : natural := 0;
    procedure check(condition : boolean; name : string) is
    begin
      if not condition then
        report "FAIL " & name severity error;
        failures := failures + 1;
      end if;
    end procedure;

    variable ch : character;
    variable digit : character := '7';
    variable chars : chars_t;
    variable low : booleans_2_t := (true, false);
    variable high : booleans_2_t := (false, true);
    variable none : booleans_null_t;
    variable all4 : booleans_4_t;
    variable one : integers_1_t;
    variable no_integers : integers_null_t;
    variable count : integer := 41;
    variable u : unsigned(7 downto 0) := x"10";
  begin
    ch := 'A';
    check(ch = 'A' and ch /= 'a' and ch < 'B', "graphic literal");
    check(character'pos(ch) = 65, "graphic position");
    check(character'pos(letter_c) = 67, "constant initializer");
    check(character'pos(digit) = 55, "digit initializer");
    check(digit = '7' and digit > '0', "digit comparison");
    ch := '0';
    check(character'pos(ch) = 48, "logic-looking literal");
    check(character'pos('1') = 49, "attribute argument");
    chars(1) := NUL;
    chars(2) := DEL;
    chars(3) := C128;
    chars(4) := 'z';
    check(character'pos(chars(1)) = 0, "NUL");
    check(character'pos(chars(2)) = 127, "DEL");
    check(character'pos(chars(3)) = 128, "C128");
    check(chars(4) = 'z', "array element");
    check(character'pos(character'high) = 255, "CHARACTER'HIGH");
    check(character'image(chars(4)) = "'z'", "image");
    case ch is
      when 'A' => check(false, "case 'A'");
      when '0' => null;
      when others => check(false, "case others");
    end case;
    pair.tag <= 'Q';
    wait for 1 ns;
    check(pair.tag = 'Q', "record signal element");

    all4 := (low & high) & none;
    check(all4 = (true, false, false, true), "right null operand");
    all4 := none & (low & high);
    check(all4 = (true, false, false, true), "left null operand");
    one := no_integers & count;
    check(one(1) = 41, "null array and element");

    -- numeric_std widens a STD_ULOGIC operand; it is not a CHARACTER.
    u := u + '1';
    check(to_integer(u) = 17, "numeric_std std_ulogic operand");

    if failures = 0 then
      report "PASS";
    else
      report "FAIL count " & integer'image(failures) severity error;
    end if;
    wait;
  end process;
end architecture;
