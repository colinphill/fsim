-- SPDX-License-Identifier: Apache-2.0
-- Drivers, basic signals, contributors and loads (IEEE 1076-2019 19.12).
library ieee;
use ieee.std_logic_1164.all;

entity top is
end entity;

architecture a of top is
  signal a : std_logic := '0';
  signal v : std_logic_vector(1 downto 0) := "00";
  signal c : std_logic := '0';
  -- Slice and non-static targets, an out signal parameter, a component
  -- instance with port associations, and a driver that updates over time.
  signal w : std_logic_vector(3 downto 0);
  signal q : std_logic := '0';
  signal x : std_logic_vector(1 downto 0) := "00";
  signal y : std_logic;
  signal s1 : std_logic := '0';

  procedure setp (signal s : out std_logic; v : in std_logic) is
  begin
    s <= v;
  end procedure;
begin
  -- Drives a, and the element v(0).
  p1 : process is
  begin
    a <= '1';
    v(0) <= '1';
    wait;
  end process p1;

  -- Reads a; drives every element of v.
  p2 : process (a) is
  begin
    v <= (others => a);
  end process p2;

  -- A concurrent statement equivalent to a process: the label is _P0.
  c <= a;

  -- Drives w(3) and w(2).
  p3 : process is
  begin
    w(3 downto 2) <= "10";
    wait;
  end process p3;

  -- A non-static index: drives every element of w.
  p4 : process is
    variable k : integer := 1;
  begin
    w(k) <= '1';
    wait;
  end process p4;

  -- Drives q through an out signal parameter.
  p5 : process is
  begin
    setp(q, '1');
    wait;
  end process p5;

  px : process is
  begin
    x <= "11";
    wait;
  end process px;

  -- A transaction without an event at 2 ns.
  pd : process is
  begin
    s1 <= '1' after 1 ns;
    wait for 2 ns;
    s1 <= '1';
    wait for 2 ns;
    s1 <= '0';
    wait;
  end process pd;

  u : entity work.leaf port map (i => x, o => y, n => open);
end architecture;

library ieee;
use ieee.std_logic_1164.all;

entity leaf is
  port (
    i : in std_logic_vector(1 downto 0);
    o : out std_logic;
    n : in std_logic := '1'
  );
end entity;

architecture b of leaf is
begin
  o <= i(0) and i(1);
end architecture;
