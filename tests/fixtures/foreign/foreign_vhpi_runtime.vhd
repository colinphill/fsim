-- SPDX-License-Identifier: Apache-2.0
library ieee;
use ieee.std_logic_1164.all;

entity top is
end entity;

architecture a of top is
  signal clk : std_logic := '0';
  signal d : std_logic := '0';
  signal q : std_logic := '0';
  signal n : integer := 0;
  signal f : std_logic_vector(3 downto 0) := "0000";
  signal t : std_logic := '0';
  signal p : std_logic := '0';
begin
  clk <= not clk after 5 ns when now < 30 ns;
  process (clk)
  begin
    if rising_edge(clk) then
      q <= d;
      n <= n + 5;
      t <= '0';
    end if;
  end process;
  process
  begin
    wait for 17 ns;
    f <= force "1010";
    wait for 6 ns;
    f <= release;
    wait;
  end process;
end architecture;
