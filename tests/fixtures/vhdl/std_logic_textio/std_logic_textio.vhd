-- SPDX-License-Identifier: Apache-2.0
--
-- The TEXTIO subprograms of IEEE.STD_LOGIC_1164 for STD_ULOGIC and
-- STD_ULOGIC_VECTOR (IEEE 1076-2019 16.7, the package body in
-- third_party/ieee-1076-2019/ieee/std_logic_1164-body.vhdl): READ, OREAD and
-- HREAD with and without GOOD, WRITE, OWRITE and HWRITE with JUSTIFIED and
-- FIELD, and the aliases BREAD, BINARY_READ, HEX_READ, BWRITE, BINARY_WRITE,
-- HEX_WRITE, OCTAL_READ and OCTAL_WRITE. The reports are those of
-- std_logic_textio.expected, derived by hand from the package body:
--
--  * Every read first assigns 'U' to each element of VALUE, so a failed read
--    leaves 'U' (the sentinel '1' shows it) and a read of a part of a vector
--    changes only that part. Leading blanks, tabs and NBSP (character 160) are skipped.
--  * READ of a scalar takes one character, which must be one of
--    U X 0 1 Z W L H - (upper case); the character that fails the read is
--    consumed. Without GOOD the failure reports, severity ERROR,
--    "STD_LOGIC_1164.READ(STD_ULOGIC) End of string encountered" or
--    "... Error: Character 'c' read, expected STD_ULOGIC literal.".
--  * READ of a vector takes as many literals as VALUE has elements. A "_"
--    between two literals is skipped; "_" first ('String begins with an "_"')
--    and two in a row ('Two underscores detected in input string "__"')
--    fail, as does the end of the line. Reading stops with the last element,
--    so a following "_" stays in the line.
--  * HREAD and OREAD read (length+3)/4 and (length+2)/3 digits: 0-9, A-F and
--    a-f (HREAD), 0-7 (OREAD) and the upper case digits Z and X, which stand
--    for ZZZZ/ZZZ and XXXX/XXX. "_" follows the vector rules between digits.
--    The leading bits that pad the vector to a whole number of digits must
--    not hold a '1' ("Vector truncated"): 4F does not fit six bits, 3F does,
--    ZF and XF do (Z and X digits have no '1').
--  * WRITE appends the characters of the elements; FIELD pads on the left
--    (RIGHT) or the right (LEFT) and never truncates. HWRITE and OWRITE
--    append TO_HSTRING and TO_OSTRING: the value is padded on the left with
--    'Z' when its leftmost element is 'Z' and with '0' otherwise; each group
--    maps L and H to 0 and 1 and U, W and - to X; a group of only Z is 'Z',
--    one with another non-binary element 'X' (so "1ZZZ001" is "XX", "ZZ011"
--    is "ZX" and "10LH" is "9").
library ieee;
use ieee.std_logic_1164.all;
use std.textio.all;

entity top is
end entity;

architecture sim of top is
begin
  tester : process
    variable l : line;
    variable ok : boolean;
    variable s : std_logic;
    variable u4 : std_ulogic_vector(3 downto 0);
    variable v6 : std_logic_vector(5 downto 0);
    variable v7 : std_logic_vector(6 downto 0);
    variable v8 : std_logic_vector(7 downto 0);
    variable sw : std_logic := 'H';
    variable w4 : std_logic_vector(3 downto 0) := "1WL-";
    variable u4w : std_ulogic_vector(3 downto 0) := "UX01";
    variable h4 : std_logic_vector(3 downto 0) := "10LH";
    variable x7 : std_logic_vector(6 downto 0) := "1ZZZ001";
    variable x5 : std_logic_vector(4 downto 0) := "ZZ011";
    variable x8 : std_logic_vector(7 downto 0) := "XXXX0101";
    variable o6 : std_logic_vector(5 downto 0) := "1HL001";
    variable o4 : std_logic_vector(3 downto 0) := "1111";
    variable z6 : std_logic_vector(5 downto 0) := "ZZZ-0H";
    -- The characters left in the line (an empty line is a null LINE here).
    impure function rest return string is
    begin
      if l = null then
        return "";
      else
        return l.all;
      end if;
    end function;
  begin
    deallocate(l);
    write(l, string'("  Z"));
    s := '1';
    read(l, s, ok);
    report "weak S1 ok=" & boolean'image(ok) & " v=" & std_ulogic'image(s) & " rest=[" & rest & "];";
    deallocate(l);
    write(l, string'("HL"));
    s := '1';
    read(l, s, ok);
    report "weak S2 ok=" & boolean'image(ok) & " v=" & std_ulogic'image(s) & " rest=[" & rest & "];";
    deallocate(l);
    write(l, string'("x"));
    s := '1';
    read(l, s, ok);
    report "weak S3 ok=" & boolean'image(ok) & " v=" & std_ulogic'image(s) & " rest=[" & rest & "];";
    deallocate(l);
    s := '1';
    read(l, s, ok);
    report "weak S4 ok=" & boolean'image(ok) & " v=" & std_ulogic'image(s) & " rest=[" & rest & "];";
    deallocate(l);
    write(l, string'("   "));
    s := '1';
    read(l, s, ok);
    report "weak S5 ok=" & boolean'image(ok) & " v=" & std_ulogic'image(s) & " rest=[" & rest & "];";
    deallocate(l);
    write(l, string'(HT & character'val(160) & "-"));
    s := '1';
    read(l, s, ok);
    report "weak S6 ok=" & boolean'image(ok) & " v=" & std_ulogic'image(s) & " rest=[" & rest & "];";
    deallocate(l);
    write(l, string'("W 1"));
    s := '1';
    read(l, s, ok);
    report "weak S7 ok=" & boolean'image(ok) & " v=" & std_ulogic'image(s) & " rest=[" & rest & "];";
    deallocate(l);
    write(l, string'("x"));
    s := '1';
    read(l, s);
    report "weak N1 v=" & std_ulogic'image(s) & " rest=[" & rest & "];";
    deallocate(l);
    s := '1';
    read(l, s);
    report "weak N2 v=" & std_ulogic'image(s) & " rest=[" & rest & "];";
    deallocate(l);
    write(l, string'(" 0"));
    s := '1';
    read(l, s);
    report "weak N3 v=" & std_ulogic'image(s) & " rest=[" & rest & "];";
    deallocate(l);
    write(l, string'(" 1_0Z-X"));
    u4 := (others => '1');
    read(l, u4, ok);
    report "weak V1 ok=" & boolean'image(ok) & " v=" & to_string(u4) & " rest=[" & rest & "];";
    deallocate(l);
    write(l, string'("_10"));
    u4 := (others => '1');
    read(l, u4, ok);
    report "weak V2 ok=" & boolean'image(ok) & " v=" & to_string(u4) & " rest=[" & rest & "];";
    deallocate(l);
    write(l, string'("1__0"));
    u4 := (others => '1');
    read(l, u4, ok);
    report "weak V3 ok=" & boolean'image(ok) & " v=" & to_string(u4) & " rest=[" & rest & "];";
    deallocate(l);
    write(l, string'("10"));
    u4 := (others => '1');
    read(l, u4, ok);
    report "weak V4 ok=" & boolean'image(ok) & " v=" & to_string(u4) & " rest=[" & rest & "];";
    deallocate(l);
    write(l, string'("10a1"));
    u4 := (others => '1');
    read(l, u4, ok);
    report "weak V5 ok=" & boolean'image(ok) & " v=" & to_string(u4) & " rest=[" & rest & "];";
    deallocate(l);
    write(l, string'("1100_"));
    u4 := (others => '1');
    read(l, u4, ok);
    report "weak V6 ok=" & boolean'image(ok) & " v=" & to_string(u4) & " rest=[" & rest & "];";
    deallocate(l);
    write(l, string'("UXZW LH"));
    u4 := (others => '1');
    read(l, u4, ok);
    report "weak V7 ok=" & boolean'image(ok) & " v=" & to_string(u4) & " rest=[" & rest & "];";
    deallocate(l);
    write(l, string'("H_L_0_1_Z_W"));
    v6 := (others => '1');
    read(l, v6, ok);
    report "weak V8 ok=" & boolean'image(ok) & " v=" & to_string(v6) & " rest=[" & rest & "];";
    deallocate(l);
    write(l, string'("1 0 1 0"));
    u4 := (others => '1');
    read(l, u4, ok);
    report "weak V9 ok=" & boolean'image(ok) & " v=" & to_string(u4) & " rest=[" & rest & "];";
    deallocate(l);
    write(l, string'("_1"));
    u4 := (others => '1');
    read(l, u4);
    report "weak NV1 v=" & to_string(u4) & " rest=[" & rest & "];";
    deallocate(l);
    write(l, string'("1__"));
    u4 := (others => '1');
    read(l, u4);
    report "weak NV2 v=" & to_string(u4) & " rest=[" & rest & "];";
    deallocate(l);
    write(l, string'("1x"));
    u4 := (others => '1');
    read(l, u4);
    report "weak NV3 v=" & to_string(u4) & " rest=[" & rest & "];";
    deallocate(l);
    write(l, string'("1"));
    u4 := (others => '1');
    read(l, u4);
    report "weak NV4 v=" & to_string(u4) & " rest=[" & rest & "];";
    deallocate(l);
    write(l, string'("-W-WLH"));
    v6 := (others => '1');
    read(l, v6);
    report "weak NV5 v=" & to_string(v6) & " rest=[" & rest & "];";
    deallocate(l);
    write(l, string'(" A5"));
    v8 := (others => '1');
    hread(l, v8, ok);
    report "weak H1 ok=" & boolean'image(ok) & " v=" & to_string(v8) & " rest=[" & rest & "];";
    deallocate(l);
    write(l, string'("3F_"));
    v6 := (others => '1');
    hread(l, v6, ok);
    report "weak H2 ok=" & boolean'image(ok) & " v=" & to_string(v6) & " rest=[" & rest & "];";
    deallocate(l);
    write(l, string'("4F"));
    v6 := (others => '1');
    hread(l, v6, ok);
    report "weak H3 ok=" & boolean'image(ok) & " v=" & to_string(v6) & " rest=[" & rest & "];";
    deallocate(l);
    write(l, string'("ZF"));
    v6 := (others => '1');
    hread(l, v6, ok);
    report "weak H4 ok=" & boolean'image(ok) & " v=" & to_string(v6) & " rest=[" & rest & "];";
    deallocate(l);
    write(l, string'("XF"));
    v6 := (others => '1');
    hread(l, v6, ok);
    report "weak H5 ok=" & boolean'image(ok) & " v=" & to_string(v6) & " rest=[" & rest & "];";
    deallocate(l);
    write(l, string'("af"));
    v8 := (others => '1');
    hread(l, v8, ok);
    report "weak H6 ok=" & boolean'image(ok) & " v=" & to_string(v8) & " rest=[" & rest & "];";
    deallocate(l);
    write(l, string'("zF"));
    v8 := (others => '1');
    hread(l, v8, ok);
    report "weak H7 ok=" & boolean'image(ok) & " v=" & to_string(v8) & " rest=[" & rest & "];";
    deallocate(l);
    write(l, string'("A_5"));
    v8 := (others => '1');
    hread(l, v8, ok);
    report "weak H8 ok=" & boolean'image(ok) & " v=" & to_string(v8) & " rest=[" & rest & "];";
    deallocate(l);
    write(l, string'("A__5"));
    v8 := (others => '1');
    hread(l, v8, ok);
    report "weak H9 ok=" & boolean'image(ok) & " v=" & to_string(v8) & " rest=[" & rest & "];";
    deallocate(l);
    write(l, string'("_A"));
    v8 := (others => '1');
    hread(l, v8, ok);
    report "weak H10 ok=" & boolean'image(ok) & " v=" & to_string(v8) & " rest=[" & rest & "];";
    deallocate(l);
    write(l, string'("A"));
    v8 := (others => '1');
    hread(l, v8, ok);
    report "weak H11 ok=" & boolean'image(ok) & " v=" & to_string(v8) & " rest=[" & rest & "];";
    deallocate(l);
    write(l, string'("G5"));
    v8 := (others => '1');
    hread(l, v8, ok);
    report "weak H12 ok=" & boolean'image(ok) & " v=" & to_string(v8) & " rest=[" & rest & "];";
    deallocate(l);
    write(l, string'("3 F"));
    v6 := (others => '1');
    hread(l, v6, ok);
    report "weak H13 ok=" & boolean'image(ok) & " v=" & to_string(v6) & " rest=[" & rest & "];";
    deallocate(l);
    write(l, string'("G5"));
    v8 := (others => '1');
    hread(l, v8);
    report "weak NH1 v=" & to_string(v8) & " rest=[" & rest & "];";
    deallocate(l);
    write(l, string'("4F"));
    v6 := (others => '1');
    hread(l, v6);
    report "weak NH2 v=" & to_string(v6) & " rest=[" & rest & "];";
    deallocate(l);
    write(l, string'("_A"));
    v8 := (others => '1');
    hread(l, v8);
    report "weak NH3 v=" & to_string(v8) & " rest=[" & rest & "];";
    deallocate(l);
    write(l, string'("A__5"));
    v8 := (others => '1');
    hread(l, v8);
    report "weak NH4 v=" & to_string(v8) & " rest=[" & rest & "];";
    deallocate(l);
    write(l, string'("A"));
    v8 := (others => '1');
    hread(l, v8);
    report "weak NH5 v=" & to_string(v8) & " rest=[" & rest & "];";
    deallocate(l);
    write(l, string'("B7"));
    v8 := (others => '1');
    hread(l, v8);
    report "weak NH6 v=" & to_string(v8) & " rest=[" & rest & "];";
    deallocate(l);
    write(l, string'("1Z1"));
    v7 := (others => '1');
    oread(l, v7, ok);
    report "weak O1 ok=" & boolean'image(ok) & " v=" & to_string(v7) & " rest=[" & rest & "];";
    deallocate(l);
    write(l, string'("XZ0"));
    v7 := (others => '1');
    oread(l, v7, ok);
    report "weak O2 ok=" & boolean'image(ok) & " v=" & to_string(v7) & " rest=[" & rest & "];";
    deallocate(l);
    write(l, string'("3Z1"));
    v7 := (others => '1');
    oread(l, v7, ok);
    report "weak O3 ok=" & boolean'image(ok) & " v=" & to_string(v7) & " rest=[" & rest & "];";
    deallocate(l);
    write(l, string'("75"));
    v6 := (others => '1');
    oread(l, v6, ok);
    report "weak O4 ok=" & boolean'image(ok) & " v=" & to_string(v6) & " rest=[" & rest & "];";
    deallocate(l);
    write(l, string'("78"));
    v6 := (others => '1');
    oread(l, v6, ok);
    report "weak O5 ok=" & boolean'image(ok) & " v=" & to_string(v6) & " rest=[" & rest & "];";
    deallocate(l);
    write(l, string'("7_5"));
    v6 := (others => '1');
    oread(l, v6, ok);
    report "weak O6 ok=" & boolean'image(ok) & " v=" & to_string(v6) & " rest=[" & rest & "];";
    deallocate(l);
    write(l, string'("78"));
    v6 := (others => '1');
    oread(l, v6);
    report "weak NO1 v=" & to_string(v6) & " rest=[" & rest & "];";
    deallocate(l);
    write(l, string'("3Z1"));
    v7 := (others => '1');
    oread(l, v7);
    report "weak NO2 v=" & to_string(v7) & " rest=[" & rest & "];";
    deallocate(l);
    write(l, string'("_7"));
    v6 := (others => '1');
    oread(l, v6);
    report "weak NO3 v=" & to_string(v6) & " rest=[" & rest & "];";
    deallocate(l);
    write(l, string'("7__5"));
    v6 := (others => '1');
    oread(l, v6);
    report "weak NO4 v=" & to_string(v6) & " rest=[" & rest & "];";
    deallocate(l);
    write(l, string'("7"));
    v6 := (others => '1');
    oread(l, v6);
    report "weak NO5 v=" & to_string(v6) & " rest=[" & rest & "];";
    deallocate(l);
    write(l, string'("01HL"));
    u4 := (others => '1');
    bread(l, u4, ok);
    report "weak A1 ok=" & boolean'image(ok) & " v=" & to_string(u4) & " rest=[" & rest & "];";
    deallocate(l);
    write(l, string'("-ZXU"));
    u4 := (others => '1');
    binary_read(l, u4, ok);
    report "weak A2 ok=" & boolean'image(ok) & " v=" & to_string(u4) & " rest=[" & rest & "];";
    deallocate(l);
    write(l, string'("fF"));
    v8 := (others => '1');
    hex_read(l, v8, ok);
    report "weak A3 ok=" & boolean'image(ok) & " v=" & to_string(v8) & " rest=[" & rest & "];";
    deallocate(l);
    write(l, string'("52"));
    v6 := (others => '1');
    octal_read(l, v6, ok);
    report "weak A4 ok=" & boolean'image(ok) & " v=" & to_string(v6) & " rest=[" & rest & "];";
    deallocate(l);
    write(l, string'("1H0L"));
    v8 := (others => '0');
    read(l, v8(3 downto 0), ok);
    report "weak P1 ok=" & boolean'image(ok) & " v=" & to_string(v8) & " rest=[" & rest & "];";
    deallocate(l);
    write(l, string'("1x"));
    v8 := (others => '0');
    read(l, v8(3 downto 0), ok);
    report "weak P2 ok=" & boolean'image(ok) & " v=" & to_string(v8) & " rest=[" & rest & "];";
    deallocate(l);
    write(l, string'("Z"));
    v8 := (others => '0');
    read(l, v8(7), ok);
    report "weak P3 ok=" & boolean'image(ok) & " v=" & to_string(v8) & " rest=[" & rest & "];";
    deallocate(l);
    write(l, string'(" W-1"));
    v8 := (others => '0');
    read(l, v8(6 downto 3), ok);
    report "weak P4 ok=" & boolean'image(ok) & " v=" & to_string(v8) & " rest=[" & rest & "];";
    write(l, sw);
    report "weak W1 [" & l.all & "];";
    deallocate(l);
    write(l, w4);
    report "weak W2 [" & l.all & "];";
    deallocate(l);
    write(l, w4, left, 6);
    report "weak W3 [" & l.all & "];";
    deallocate(l);
    write(l, w4, right, 6);
    report "weak W4 [" & l.all & "];";
    deallocate(l);
    write(l, sw, right, 3);
    write(l, sw, left, 3);
    report "weak W5 [" & l.all & "];";
    deallocate(l);
    write(l, w4, right, 2);
    report "weak W6 [" & l.all & "];";
    deallocate(l);
    bwrite(l, u4w);
    binary_write(l, u4w, left, 6);
    report "weak W7 [" & l.all & "];";
    deallocate(l);
    hwrite(l, h4);
    report "weak X1 [" & l.all & "];";
    deallocate(l);
    hwrite(l, x7);
    report "weak X2 [" & l.all & "];";
    deallocate(l);
    hwrite(l, x5);
    report "weak X3 [" & l.all & "];";
    deallocate(l);
    hwrite(l, x8);
    hwrite(l, x8, right, 4);
    report "weak X4 [" & l.all & "];";
    deallocate(l);
    hex_write(l, x8, left, 5);
    report "weak X5 [" & l.all & "];";
    deallocate(l);
    owrite(l, o6);
    report "weak Y1 [" & l.all & "];";
    deallocate(l);
    owrite(l, o4);
    report "weak Y2 [" & l.all & "];";
    deallocate(l);
    octal_write(l, o6, left, 4);
    report "weak Y3 [" & l.all & "];";
    deallocate(l);
    owrite(l, z6);
    report "weak Y4 [" & l.all & "];";
    deallocate(l);
    std.env.finish;
  end process;
end architecture;
