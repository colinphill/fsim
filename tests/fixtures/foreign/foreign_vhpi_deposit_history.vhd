-- SPDX-License-Identifier: Apache-2.0
-- A deposit makes a signal active in the update phase it is applied in, also
-- when it leaves the value (IEEE 1076-2019 14.7.3.1: a signal is active if
-- a deposit is scheduled for it; 22.5.3). s starts '0'; the foreign library
-- deposits '1' at 5 ns (an event and a transaction) and '1' again at 8 ns
-- (a transaction without an event).
--   6 ns: 1 ns since the event and the transaction.
--   9 ns: 4 ns since the event, 1 ns since the transaction.
-- The ages in nanoseconds are signals the library prints at 10 ns.
library ieee;
use ieee.std_logic_1164.all;

entity top is
end entity;

architecture rtl of top is
  signal s : std_logic := '0';
  signal event6, active6, event9, active9 : integer := -1;
begin
  probe: process
  begin
    wait for 6 ns;
    event6 <= s'last_event / 1 ns;
    active6 <= s'last_active / 1 ns;
    wait for 3 ns;
    event9 <= s'last_event / 1 ns;
    active9 <= s'last_active / 1 ns;
    wait;
  end process;
end architecture;
