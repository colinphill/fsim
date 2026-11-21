-- fsim: top=d8_alias_signal_actual(test) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 6.6.1, 6.6.2: an object alias of a signal denotes that
-- signal, so it is a signal name: it may be the actual of the signal formal
-- of RISING_EDGE (16.8).
library ieee;
use ieee.std_logic_1164.all;

entity d8_alias_signal_actual is
end entity;

architecture test of d8_alias_signal_actual is
  signal aclk : std_logic := '0';
  alias clk is aclk;
  signal edges : natural := 0;
begin
  process (clk)
  begin
    if rising_edge(clk) then
      edges <= edges + 1;
    end if;
  end process;

  process
  begin
    aclk <= '1';
    wait for 1 ns;
    assert clk = '1' severity failure;
    assert edges = 1 severity failure;
    aclk <= '0';
    wait for 1 ns;
    assert clk = '0' severity failure;
    report "PASS";
    wait;
  end process;
end architecture;
