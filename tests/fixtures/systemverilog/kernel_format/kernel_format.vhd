-- SPDX-License-Identifier: Apache-2.0
--
-- VHDL TextIO WRITE (IEEE 1076-2019 16.4) and TO_STRING / TO_HSTRING /
-- TO_OSTRING (16.7) in the v4 kernel: field widths held in variables,
-- TIME values with and without a unit, NOW, and vectors whose elements are
-- 'U', 'W', 'Z' or '-' (narrow and wider than 64 bits), and 'VALUE of
-- integer, enumeration, physical, character and composite types (16.2.2)
-- and an allocator's STRING fill, in two instances.
-- kernel_format_vhdl_limit fails the run with a field width beyond the
-- string length limit.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use std.textio.all;

entity kernel_format_cell is
  generic (STEP : natural);
end entity;

architecture a of kernel_format_cell is
  type color is (red, green, blue);
  type distance is range 0 to 1000000 units um; mm = 1000 um; end units;
  type pair is array (0 to 1) of integer;
  signal clk : std_logic := '0';
  signal d : unsigned(7 downto 0) := (others => '0');
  signal s : signed(6 downto 0) := (others => '0');
  signal u : std_logic_vector(3 downto 0) := "U1X0";
  signal w : std_logic_vector(99 downto 0) := (others => 'U');
begin
  clk <= not clk after 5 ns when now < 120 ns;
  process (clk)
    variable l : line;
    variable n : integer := 0;
    variable width : natural := 0;
    variable b : bit_vector(5 downto 0) := "101100";
    constant FILL : std_logic_vector(1 to 5) := "-WZ10";
    variable p : pair;
    variable bits : bit_vector(3 downto 0);
    variable fill_line : line;
  begin
    if rising_edge(clk) then
      n := n + 1;
      width := (n * STEP) mod 7;
      d <= d + STEP;
      s <= s - to_signed(STEP, 7);
      u <= u(2 downto 0) & u(3);
      w <= w(98 downto 0) & FILL(n mod 5 + 1);
      write(l, kernel_format_cell'path_name & " " & integer'image(n) & ":");
      write(l, n, right, width);
      write(l, '|');
      write(l, string'("ab"), left, width);
      write(l, '|');
      write(l, now, left, 12, ns);
      write(l, '|');
      write(l, now);
      write(l, '|');
      write(l, now, right, width, ps);
      write(l, '|');
      write(l, to_integer(d), left, n mod 5);
      write(l, ' ');
      write(l, to_integer(s), right, width);
      write(l, ' ' & integer'image(n * 1000) & " " & time'image(now));
      write(l, ' ' & std_logic'image(clk) & " " & boolean'image(n > 3));
      write(l, ' ' & to_hstring(d) & " " & to_ostring(d) & " " & to_hstring(s));
      write(l, ' ' & to_ostring(s) & " " & to_string(u) & " " & to_hstring(u));
      write(l, ' ' & to_string(w(19 downto 0)));
      write(l, ' ' & to_hstring(w) & " " & to_ostring(w));
      write(l, ' ');
      write(l, b, right, width);
      write(l, ' ');
      write(l, n > 2, left, width);
      p := pair'value("(" & integer'image(n * STEP) & ", -2)");
      bits := bit_vector'value(to_string(b(3 downto 0)));
      write(l, ' ' & integer'image(integer'value(" " & integer'image(n * 7) & " ")));
      write(l, ' ' & color'image(color'value(color'image(color'val((n + STEP) mod 3)))));
      write(l, ' ' & distance'image(distance'value(integer'image(n) & " mm")));
      write(l, ' ' & character'image(character'value("'z'")));
      write(l, ' ' & integer'image(p(0)) & integer'image(p(1)) & " " & to_string(bits));
      -- An allocator's STRING of CHARACTER'LEFT (9.3.7, 6.4.2.3).
      fill_line := new string(1 to 3);
      fill_line(2) := 'k';
      write(l, ' ' & integer'image(character'pos(fill_line(1))) & fill_line(2));
      deallocate(fill_line);
      b := b(4 downto 0) & b(5);
      writeline(output, l);
    end if;
  end process;
end architecture;

entity kernel_format_vhdl is
end entity;

architecture a of kernel_format_vhdl is
begin
  first : entity work.kernel_format_cell generic map (STEP => 3);
  second : entity work.kernel_format_cell generic map (STEP => 5);
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use std.textio.all;

entity kernel_format_vhdl_limit is
end entity;

architecture a of kernel_format_vhdl_limit is
  signal clk : std_logic := '0';
begin
  clk <= not clk after 5 ns when now < 40 ns;
  process (clk)
    variable l : line;
    variable n : natural := 0;
    variable width : natural := 4;
  begin
    if rising_edge(clk) then
      n := n + 1;
      if n = 3 then
        width := natural'high;
      end if;
      write(l, n, right, width);
      writeline(output, l);
    end if;
  end process;
end architecture;
