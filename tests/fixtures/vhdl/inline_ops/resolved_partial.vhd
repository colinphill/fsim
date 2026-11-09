-- SPDX-License-Identifier: Apache-2.0
--
-- A std_logic_vector whose drivers cover different elements: the v4 kernel
-- resolves, in generated code, the elements two processes drive, and an
-- element only one process drives keeps that process's value (IEEE
-- 1076-2019 14.7.3.2: the resolution function is called with the drivers
-- of the element; the one driver of a '-' element gives '-'). Only the
-- drivers of an element take part in its resolution (14.7.2: a process
-- that assigns a slice has a driver for each element of the slice).
-- Cases for the 130-element b, wider than two words: p1 drives all of it,
-- p2 its elements 100 downto 0, so b(129 downto 101) has one driver and
-- b(100 downto 0) two. For the 8-element m, p1 drives m(7 downto 3) and p2
-- m(5 downto 0): m(7 downto 6) one driver, m(5 downto 3) two, m(2 downto 0)
-- one. Report n shows the assignments at the rising edge with cnt = n - 1.
--   cnt 0  p1 '-'   p2 '0'   b: 29 '-' then 101 'X' ('-',0 = X)
--                            m: "--" "XXX" "000"
--   cnt 1  p1 '1'   p2 'H'   b: '1's ('1',H = 1)       m: "11" "111" "HHH"
--   cnt 2  p1 'Z'   p2 'Z'   b: 'Z's                   m: "ZZ" "ZZZ" "ZZZ"
--   cnt 3+ p1 'L'   p2 'H'   b: 29 'L' then 101 'W'    m: "LL" "WWW" "HHH"
library ieee;
use ieee.std_logic_1164.all;

entity top is
end entity;

architecture sim of top is
  signal clk : std_logic := '0';
  signal cnt : integer := 0;
  signal b : std_logic_vector(129 downto 0);
  signal m : std_logic_vector(7 downto 0);
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
          b <= (others => '-');
          m(7 downto 3) <= (others => '-');
        when 1 =>
          b <= (others => '1');
          m(7 downto 3) <= (others => '1');
        when 2 =>
          b <= (others => 'Z');
          m(7 downto 3) <= (others => 'Z');
        when others =>
          b <= (others => 'L');
          m(7 downto 3) <= (others => 'L');
      end case;
    end if;
  end process;

  p2 : process (clk)
  begin
    if rising_edge(clk) then
      case cnt is
        when 0 =>
          b(100 downto 0) <= (others => '0');
          m(5 downto 0) <= (others => '0');
        when 1 =>
          b(100 downto 0) <= (others => 'H');
          m(5 downto 0) <= (others => 'H');
        when 2 =>
          b(100 downto 0) <= (others => 'Z');
          m(5 downto 0) <= (others => 'Z');
        when others =>
          b(100 downto 0) <= (others => 'H');
          m(5 downto 0) <= (others => 'H');
      end case;
    end if;
  end process;

  mon : process (clk)
  begin
    if falling_edge(clk) then
      report "wide " & integer'image(cnt) & " b=" & to_string(b)
        & " m=" & to_string(m) & ";";
    end if;
  end process;
end architecture;
