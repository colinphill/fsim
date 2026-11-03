-- SPDX-License-Identifier: Apache-2.0
library ieee;
use ieee.std_logic_1164.all;

entity top is
end entity;

architecture a of top is
  signal clk : std_logic := '0';
  signal a : integer := 0;
  signal b : integer := 0;
  signal s : integer := 0;
  signal t : integer := 0;
  signal u : integer := 0;
  signal v : std_logic_vector(3 downto 0) := "0000";
begin
  -- A labeled process with a sensitivity list and declarations.
  clock : process (clk) is
    variable count : integer := 0;
    constant limit : integer := 3;
  begin
    if clk = '1' then
      count := count + 1;
      a <= count;
    elsif count = 0 then
      a <= 0;
    else
      null;
    end if;
    case count is
      when 0 => b <= 1;
      when 1 | 2 => b <= 2;
      when others => b <= 3 after 1 ns;
    end case;
    for i in 0 to 3 loop
      v(i) <= '1';
      exit when i = 2;
    end loop;
    lp : while count < limit loop
      count := count + 1;
      next lp when count = 2;
    end loop lp;
  end process clock;

  -- Unlabeled concurrent statements have the implicit labels _P0, _P1, ...
  s <= a;

  process is
  begin
    wait;
  end process;

  t <= 1 when a = 0 else 2;

  with a select u <= 1 when 0, 2 when others;

  chk : assert a >= 0 report "negative" severity error;

  -- A process with wait statements.
  stimulus : process is
  begin
    wait for 5 ns;
    clk <= '1';
    wait on clk until clk = '0' for 10 ns;
    assert a >= 0 report "negative" severity warning;
    report "done";
    wait;
  end process stimulus;

  blk : block is
    signal w : integer := 0;
  begin
    w <= s;
  end block blk;

  gen : for i in 0 to 1 generate
    p : process (clk) is
    begin
      null;
    end process p;
  end generate gen;
end architecture;
