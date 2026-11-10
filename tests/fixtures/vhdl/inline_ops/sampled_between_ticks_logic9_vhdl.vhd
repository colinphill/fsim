-- SPDX-License-Identifier: Apache-2.0
--
-- The VHDL side of sampled_between_ticks_logic9.sv: a top level that drives
-- a std_logic sb and a 70 element std_logic_vector sv into the
-- SystemVerilog module svm: sb '0' from 2 ns, 'H' from 22 ns, 'L' from
-- 32 ns, 'H' from 52 ns, '1' from 58 ns, 'L' from 68 ns and '0' from 78 ns;
-- sv all '0' from 2 ns, element 65 'Z' from 22 ns, 'H' from 32 ns and 'Z'
-- from 52 ns (assigned 'Z' again at 58 ns), element 3 'H' from 52 ns, '1'
-- from 58 ns, 'L' from 68 ns and '0' from 78 ns.
library ieee;
use ieee.std_logic_1164.all;

entity vtop is
end entity;

architecture a of vtop is
  component svm
    port (sb : in std_logic; sv : in std_logic_vector(69 downto 0));
  end component;
  signal sb : std_logic;
  signal sv : std_logic_vector(69 downto 0);
begin
  u : svm port map (sb => sb, sv => sv);
  drive : process
  begin
    wait for 2 ns;
    sb <= '0';
    sv <= (others => '0');
    wait for 20 ns;
    sb <= 'H';
    sv(65) <= 'Z';
    wait for 10 ns;
    sb <= 'L';
    sv(65) <= 'H';
    wait for 20 ns;
    sb <= 'H';
    sv(65) <= 'Z';
    sv(3) <= 'H';
    wait for 6 ns;
    sb <= '1';
    sv(65) <= 'Z';
    sv(3) <= '1';
    wait for 10 ns;
    sb <= 'L';
    sv(3) <= 'L';
    wait for 10 ns;
    sb <= '0';
    sv(3) <= '0';
    wait;
  end process;
end architecture;
