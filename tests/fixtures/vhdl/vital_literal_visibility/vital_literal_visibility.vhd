-- SPDX-License-Identifier: Apache-2.0
--
-- IEEE.VITAL_MEMORY declares VitalMemorySymbolType, which has the literals
-- '1' and 'Z' (and 'X', '0', '-'). A use clause for it makes those literals
-- potentially visible beside STD_ULOGIC's and BIT's (IEEE 1076-2019 12.4);
-- enumeration literals are overloadable (12.3), so the type the context
-- gives a character literal decides which one it is (9.3.2, 12.5). The
-- operands below are STD_ULOGIC or BIT parameters of IEEE.STD_LOGIC_1164
-- functions, folded while elaborating the constants; the type of each
-- actual is the one scalar formal the conversion has for it (12.5).
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use ieee.vital_memory.all;

entity top is
end entity;

architecture test of top is
  constant vec : std_logic_vector(3 downto 0) := to_x01z(std_ulogic_vector'("1ZHL"));
  constant bit_of_one : bit := to_bit('1');
  constant x_of_z : std_ulogic := to_x01('Z');
  constant z_kept : std_ulogic := to_x01z('Z');
  constant u_of_bit : std_ulogic := to_stdulogic(bit_of_one);
  constant one_of_h : std_ulogic := to_x01('H');

  -- A user type with the same literals, typed by its contexts, keeps its own
  -- positions beside STD_ULOGIC's and VITAL's (9.3.2, 12.5).
  type tri is ('0', '1', 'Z');
  constant tri_one : tri := '1';
  constant tri_z : tri := 'Z';
  constant tri_z_pos : integer := tri'pos(tri_z);
  constant tri_one_pos : integer := tri'pos(tri_one);

  constant c_a : unsigned(3 downto 0) := "0110";
  constant c_zero : unsigned(3 downto 0) := "0000";
  constant c_h : unsigned(3 downto 0) := "00H0";
  constant c_x : unsigned(3 downto 0) := "0X10";
  constant c_s : signed(3 downto 0) := "1100";
  signal s : std_logic := '0';
  signal v : std_logic_vector(3 downto 0) := "0000";
  signal u : unsigned(3 downto 0) := "0001";
  signal u_up : unsigned(2 to 5) := "0110";

  function verdict(ok : boolean) return string is
  begin
    if ok then
      return "ok";
    end if;
    return "FAIL";
  end function;
begin
  process
    variable r : std_logic;
    variable b : boolean;
    variable bt : bit;
  begin
    report "vitlit vec3 " & verdict(vec(3) = '1');
    report "vitlit vec2 " & verdict(vec(2) = 'Z');
    report "vitlit vec1 " & verdict(vec(1) = '1');
    report "vitlit vec0 " & verdict(vec(0) = '0');
    report "vitlit to_bit " & verdict(bit_of_one = '1');
    report "vitlit to_x01 Z " & verdict(x_of_z = 'X');
    report "vitlit to_x01z Z " & verdict(z_kept = 'Z');
    report "vitlit to_stdulogic " & verdict(u_of_bit = '1');
    report "vitlit to_x01 H " & verdict(one_of_h = '1');
    report "vitlit tri 1 " & verdict(tri_one_pos = 1);
    report "vitlit tri Z " & verdict(tri_z_pos = 2);
    report "vitlit tri eq " & verdict(tri_z = 'Z' and tri_one /= 'Z');
    -- Character literals in every context of a typed operand or target
    -- (9.3.2, 12.5): operators, matching relations, conditional values,
    -- case choices, aggregates, concatenations, attribute arguments,
    -- assignments and the actuals of compiler-owned subprograms.
    r := s and '1';
    report "vitlit and " & std_logic'image(r);
    r := '1' xor s;
    report "vitlit xor " & std_logic'image(r);
    b := s = 'Z';
    report "vitlit equal " & boolean'image(b);
    b := 'Z' /= s;
    report "vitlit not-equal " & boolean'image(b);
    b := rising_edge(s);
    report "vitlit rising " & boolean'image(b);
    r := not '1';
    report "vitlit not " & std_logic'image(r);
    r := '1' when s = '0' else 'Z';
    report "vitlit conditional " & std_logic'image(r);
    r := to_x01(s and 'Z');
    report "vitlit to_x01 and " & std_logic'image(r);
    r := s ?= 'Z';
    report "vitlit matching " & std_logic'image(r);
    b := is_x('U');
    report "vitlit is_x " & boolean'image(b);
    r := to_01(u)(0);
    report "vitlit to_01 " & std_logic'image(r);
    case s is
      when 'Z' => report "vitlit case z";
      when others => report "vitlit case others";
    end case;
    report "vitlit image " & std_logic'image('Z');
    v <= (others => 'Z');
    wait for 1 ns;
    report "vitlit others " & to_string(v);
    v <= "Z" & "1" & s & 'Z';
    wait for 1 ns;
    report "vitlit concatenation " & to_string(v);
    u <= u + '1';
    wait for 1 ns;
    report "vitlit add " & to_string(u);
    -- TO_X01 has a STD_ULOGIC and a BIT overload, both returning X01 (a
    -- STD_ULOGIC subtype), so the character literal alone does not choose
    -- one (12.5): a qualified expression names the actual's type (9.3.5).
    -- A literal that only one condition operator accepts is not ambiguous.
    r := to_x01(std_ulogic'('1'));
    report "vitlit to_x01 std_ulogic " & std_logic'image(r);
    bt := to_bit(to_x01(bit'('1')));
    report "vitlit to_x01 bit " & bit'image(bt);
    b := ?? 'H';
    report "vitlit condition " & boolean'image(b);
    -- RESOLVED and STD_MATCH as the package bodies define them: resolved
    -- folds resolution_table from 'Z' (one element is itself, none is 'Z');
    -- std_match is MATCH_TABLE per element, false for a null argument or
    -- unequal lengths.
    report "vitlit resolved Z1 " & std_logic'image(resolved(std_ulogic_vector'("Z1")));
    report "vitlit resolved 01 " & std_logic'image(resolved(std_ulogic_vector'("01")));
    report "vitlit resolved HL " & std_logic'image(resolved(std_ulogic_vector'("HL")));
    report "vitlit resolved 0H " & std_logic'image(resolved(std_ulogic_vector'("0H")));
    report "vitlit resolved dash " & std_logic'image(resolved(std_ulogic_vector'("-")));
    report "vitlit resolved U1 " & std_logic'image(resolved(std_ulogic_vector'("U1")));
    report "vitlit resolved dash-Z " & std_logic'image(resolved(std_ulogic_vector'("-Z")));
    report "vitlit resolved null " & std_logic'image(resolved(v(0 downto 1)));
    report "vitlit match dash " & boolean'image(std_match(s, '-'));
    report "vitlit match 1H " & boolean'image(std_match('1', 'H'));
    report "vitlit match 01 " & boolean'image(std_match('0', '1'));
    report "vitlit match UU " & boolean'image(std_match('U', 'U'));
    report "vitlit match ZZ " & boolean'image(std_match('Z', 'Z'));
    report "vitlit match L0 " & boolean'image(std_match('L', '0'));
    report "vitlit match unsigned " & boolean'image(std_match(unsigned'("01-1"), unsigned'("0111")));
    report "vitlit match unsigned H " & boolean'image(std_match(unsigned'("01H0"), unsigned'("0110")));
    report "vitlit match unsigned X " & boolean'image(std_match(unsigned'("0X"), unsigned'("00")));
    report "vitlit match unsigned length " & boolean'image(std_match(unsigned'("01"), unsigned'("011")));
    report "vitlit match signed " & boolean'image(std_match(signed'("1-"), signed'("10")));
    report "vitlit match sulv " & boolean'image(std_match(std_ulogic_vector'("0Z"), std_ulogic_vector'("0Z")));
    report "vitlit match slv " & boolean'image(std_match(std_logic_vector'("1-"), std_logic_vector'("00")));
    -- FIND_LEFTMOST / FIND_RIGHTMOST scan ARG'RANGE / ARG'REVERSE_RANGE for
    -- the first element with `ARG(INDEX) ?= Y` and return its index, or -1.
    report "vitlit find leftmost " & integer'image(find_leftmost(c_a, '1'));
    report "vitlit find rightmost " & integer'image(find_rightmost(c_a, '1'));
    report "vitlit find none " & integer'image(find_leftmost(c_zero, '1'));
    report "vitlit find match " & integer'image(find_leftmost(c_h, '1'));
    report "vitlit find dash " & integer'image(find_rightmost(c_x, '-'));
    report "vitlit find signed " & integer'image(find_rightmost(c_s, '0'));
    report "vitlit find ascending " & integer'image(find_leftmost(u_up, '1'));
    report "vitlit find ascending right " & integer'image(find_rightmost(u_up, '1'));
    s <= 'Z';
    wait for 1 ns;
    report "vitlit assign " & std_logic'image(s);
    wait;
  end process;
end architecture;
