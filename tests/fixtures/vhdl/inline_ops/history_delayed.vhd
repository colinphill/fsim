-- SPDX-License-Identifier: Apache-2.0
--
-- S'DELAYED(T) is driven as `S'DELAYED(T) <= transport S after T` (IEEE
-- 1076-2019 16.2.4): after T it takes the value S had T before, and for
-- T = 0 it changes in the simulation cycle after S does. clk is '0' and
-- changes to '1' at 1 ns, '0' at 2 ns and '1' at 3 ns.
--   clk'delayed(1 ns): '0' until 2 ns, then '1' (2 ns), '0' (3 ns), '1'
--     (4 ns): read at 1.5, 2.5, 3.5 and 4.5 ns.
--   clk'delayed(0 ns): the process that wakes on a change of clk reads the
--     old value and no event in its cycle, and after `wait for 0 ns`, the
--     next cycle, the new value with an event.
library ieee;
use ieee.std_logic_1164.all;

entity top is
end entity;

architecture rtl of top is
  signal clk : std_logic := '0';
begin
  stim: process
  begin
    wait for 1 ns;
    clk <= '1';
    wait for 1 ns;
    clk <= '0';
    wait for 1 ns;
    clk <= '1';
    wait;
  end process;

  zero: process
  begin
    wait on clk;
    report "wide d0 a at " & time'image(now) & " clk=" & std_logic'image(clk)
      & " delayed=" & std_logic'image(clk'delayed(0 ns))
      & " event=" & boolean'image(clk'delayed(0 ns)'event);
    wait for 0 ns;
    report "wide d0 b at " & time'image(now) & " clk=" & std_logic'image(clk)
      & " delayed=" & std_logic'image(clk'delayed(0 ns))
      & " event=" & boolean'image(clk'delayed(0 ns)'event);
  end process;

  one: process
  begin
    wait for 1500 ps;
    report "wide d1 at " & time'image(now) & " delayed=" & std_logic'image(clk'delayed(1 ns));
    wait for 1 ns;
    report "wide d1 at " & time'image(now) & " delayed=" & std_logic'image(clk'delayed(1 ns));
    wait for 1 ns;
    report "wide d1 at " & time'image(now) & " delayed=" & std_logic'image(clk'delayed(1 ns));
    wait for 1 ns;
    report "wide d1 at " & time'image(now) & " delayed=" & std_logic'image(clk'delayed(1 ns));
    wait;
  end process;
end architecture;
