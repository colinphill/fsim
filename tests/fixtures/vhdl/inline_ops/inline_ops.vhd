-- SPDX-License-Identifier: Apache-2.0
--
-- Wide (over 64 bits) and nine-value operations that the v4 kernel compiles
-- inline: dynamic bit and part selects, part inserts and signal assignments
-- of a 128-element std_logic_vector holding 'H', 'L', 'W' and '-'; shifts and
-- rotates with a signed amount (IEEE 1076-2008 9.2.3: a negative amount
-- shifts the other way); integer "/", "rem", "mod", "**", "-" and "abs"
-- (9.2.7, 9.2.8); numeric_std division; and the matching equality "?="
-- (9.2.3), whose result is '0' for a known mismatch, else 'U' for a 'U'
-- element, else 'X' for another metavalue, '-' matching anything and 'H',
-- 'L' counting as '1', '0'. The values are derived by hand in the comments.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity top is
end entity;

architecture sim of top is
  signal w128 : std_logic_vector(127 downto 0);
  signal w128_copy : std_logic_vector(127 downto 0);
  signal b128 : bit_vector(127 downto 0);
  signal ins : std_logic_vector(127 downto 0);
begin
  w128 <= (127 downto 96 => 'H', 95 downto 64 => 'L', 63 downto 32 => 'W', 31 downto 0 => '-');
  w128_copy <= w128;
  b128 <= to_bitvector(w128, '1');

  stim : process
    variable i : integer;
    variable v : std_logic_vector(127 downto 0);
    variable p : std_logic_vector(3 downto 0);
    variable s : std_logic_vector(7 downto 0) := "HL01W-XZ";
    variable bv : bit_vector(7 downto 0) := "10110010";
    variable k : integer;
    variable m : integer;
    variable n : integer;
    variable u : unsigned(7 downto 0);
    variable g : signed(7 downto 0);
    variable mu : std_logic_vector(3 downto 0);
  begin
    wait for 1 ns;
    i := 100;
    report "wide ext100=" & std_logic'image(w128(i)) & ";";
    i := 70;
    report "wide ext70=" & std_logic'image(w128(i)) & ";";
    i := 40;
    report "wide ext40=" & std_logic'image(w128(i)) & ";";
    i := 5;
    report "wide ext5=" & std_logic'image(w128(i)) & ";";
    i := 62;
    p := w128(i + 3 downto i);
    report "wide part62=" & to_string(p) & ";";
    i := 94;
    p := w128(i + 3 downto i);
    report "wide part94=" & to_string(p) & ";";
    i := 30;
    p := w128(i + 3 downto i);
    report "wide part30=" & to_string(p) & ";";
    v := w128;
    i := 62;
    v(i + 3 downto i) := "01HL";
    report "wide insert62=" & to_string(v(67 downto 60)) & ";";
    ins <= v;
    wait for 1 ns;
    report "wide assign=" & to_string(ins(67 downto 60)) & ";";
    v := w128;
    i := 100;
    v(i) := 'X';
    report "wide insert100=" & to_string(v(101 downto 98)) & ";";
    report "wide bits=" & to_string(b128(67 downto 60)) & ";";
    report "wide copy=" & boolean'image(w128 = w128_copy) & ";";
    report "wide rol1=" & to_string(s rol 1) & ";";
    report "wide sll2=" & to_string(s sll 2) & ";";
    report "wide srl3=" & to_string(s srl 3) & ";";
    report "wide ror1=" & to_string(s ror 1) & ";";
    k := -1;
    report "wide rolneg=" & to_string(s rol k) & ";";
    k := -2;
    report "wide sllneg=" & to_string(s sll k) & ";";
    k := 2;
    report "wide sla2=" & to_string(bv sla k) & ";";
    report "wide sra2=" & to_string(bv sra k) & ";";
    k := 3;
    report "wide rol3=" & to_string(bv rol k) & ";";
    k := -3;
    report "wide rolneg3=" & to_string(bv rol k) & ";";
    k := -2;
    report "wide slaneg2=" & to_string(bv sla k) & ";";
    m := -7;
    n := 2;
    report "wide div=" & integer'image(m / n) & ";";
    report "wide rem=" & integer'image(m rem n) & ";";
    report "wide mod=" & integer'image(m mod n) & ";";
    m := 7;
    n := -2;
    report "wide mod2=" & integer'image(m mod n) & ";";
    report "wide rem2=" & integer'image(m rem n) & ";";
    report "wide div2=" & integer'image(m / n) & ";";
    n := 10;
    m := 2;
    report "wide pow=" & integer'image(m ** n) & ";";
    m := -3;
    n := 3;
    report "wide pow2=" & integer'image(m ** n) & ";";
    report "wide neg=" & integer'image(-m) & ";";
    report "wide abs=" & integer'image(abs m) & ";";
    u := to_unsigned(200, 8);
    report "wide udiv=" & integer'image(to_integer(u / to_unsigned(7, 8))) & ";";
    report "wide umod=" & integer'image(to_integer(u mod to_unsigned(7, 8))) & ";";
    g := to_signed(-100, 8);
    report "wide sdiv=" & integer'image(to_integer(g / to_signed(7, 8))) & ";";
    report "wide srem=" & integer'image(to_integer(g rem to_signed(7, 8))) & ";";
    report "wide smod=" & integer'image(to_integer(g mod to_signed(7, 8))) & ";";
    report "wide match1=" & std_logic'image("10HL" ?= "10-0") & ";";
    report "wide match2=" & std_logic'image(std_logic_vector'("10HL") ?= std_logic_vector'("1H1L")) & ";";
    report "wide match3=" & std_logic'image(std_logic_vector'("10WL") ?= std_logic_vector'("1010")) & ";";
    mu := "1U0L";
    report "wide match4=" & std_logic'image(mu ?= std_logic_vector'("1000")) & ";";
    mu := "1U01";
    report "wide match5=" & std_logic'image(mu ?= std_logic_vector'("1000")) & ";";
    mu := "-U0L";
    report "wide match6=" & std_logic'image(mu ?= std_logic_vector'("1000")) & ";";
    std.env.finish;
  end process;
end architecture;
