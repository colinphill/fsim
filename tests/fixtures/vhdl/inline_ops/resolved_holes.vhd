-- SPDX-License-Identifier: Apache-2.0
--
-- std_logic_vector signals with elements that no process drives: such an
-- element has no source, so its driving and effective value is the default
-- value of the signal (IEEE 1076-2019 14.7.3.2 e), first item), whatever the
-- other elements' drivers do; an element one process drives takes that
-- driver's value ('-' too), and one two processes drive the resolution of
-- the two (std_logic_1164 resolution_table). The v4 kernel owns both
-- signals and never stores them through the host (no host:store_host or
-- host:wide_store): the 130-element h, wider than two words, whose drivers
-- overlap (resolved in generated code), and the 8-element d, whose drivers
-- are disjoint (no resolution). The initial value of each element is the
-- resolution of its drivers' initial values, the default (14.7.5.2).
--   h (default all 'H'): p1 drives h(129 downto 70), p2 h(80 downto 10);
--     h(129 downto 81) has one driver (49), h(80 downto 70) two (11),
--     h(69 downto 10) one (60), h(9 downto 0) none (10, always 'H').
--   d (default "UXZ01HLW"): p1 drives d(7 downto 6), p2 d(3 downto 2);
--     d(5 downto 4) and d(1 downto 0) have none ("Z0" and "LW").
-- Report 0 is at 1 ns (initial values); report n at the n-th falling edge
-- of clk shows the assignments at the rising edge with cnt = n - 1.
--   cnt 0  p1 h '1' d "10"   p2 h '0' d "-H"
--          h: 49 '1', 11 'X' (1,0), 60 '0', 10 'H'   d: "10Z0-HLW"
--   cnt 1  p1 h 'Z' d "ZZ"   p2 h 'L' d "XU"
--          h: 49 'Z', 11 'L' (Z,L), 60 'L', 10 'H'   d: "ZZZ0XULW"
--   cnt 2+ p1 h 'W' d "UW"   p2 h 'H' d "0Z"
--          h: 49 'W', 11 'W' (W,H), 60 'H', 10 'H'   d: "UWZ00ZLW"
library ieee;
use ieee.std_logic_1164.all;

entity top is
end entity;

architecture sim of top is
  signal clk : std_logic := '0';
  signal cnt : integer := 0;
  signal h : std_logic_vector(129 downto 0) := (others => 'H');
  signal d : std_logic_vector(7 downto 0) := "UXZ01HLW";
begin
  clkgen : process
  begin
    for k in 1 to 10 loop
      wait for 5 ns;
      clk <= not clk;
    end loop;
    wait;
  end process;

  count : process (clk)
  begin
    if rising_edge(clk) then
      cnt <= cnt + 1;
    end if;
  end process;

  p1 : process (clk)
  begin
    if rising_edge(clk) then
      case cnt is
        when 0 =>
          h(129 downto 70) <= (others => '1');
          d(7 downto 6) <= "10";
        when 1 =>
          h(129 downto 70) <= (others => 'Z');
          d(7 downto 6) <= "ZZ";
        when others =>
          h(129 downto 70) <= (others => 'W');
          d(7 downto 6) <= "UW";
      end case;
    end if;
  end process;

  p2 : process (clk)
  begin
    if rising_edge(clk) then
      case cnt is
        when 0 =>
          h(80 downto 10) <= (others => '0');
          d(3 downto 2) <= "-H";
        when 1 =>
          h(80 downto 10) <= (others => 'L');
          d(3 downto 2) <= "XU";
        when others =>
          h(80 downto 10) <= (others => 'H');
          d(3 downto 2) <= "0Z";
      end case;
    end if;
  end process;

  initial : process
  begin
    wait for 1 ns;
    report "wide 0 h=" & to_string(h) & " d=" & to_string(d) & ";";
    wait;
  end process;

  mon : process (clk)
  begin
    if falling_edge(clk) then
      report "wide " & integer'image(cnt) & " h=" & to_string(h)
        & " d=" & to_string(d) & ";";
    end if;
  end process;
end architecture;
