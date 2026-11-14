-- SPDX-License-Identifier: Apache-2.0
--
-- The history attributes of whole signals (IEEE 1076-2019 16.2.4) read the
-- same values wherever the signals live: 'EVENT and 'ACTIVE are true in the
-- simulation cycle after the update phase that changed or assigned the
-- signal (and only in it), 'LAST_EVENT and 'LAST_ACTIVE count the time since
-- the latest event and transaction (TIME'HIGH for none), and 'LAST_VALUE is
-- the value before the latest event.
--   s (std_logic_vector, 0000): "0101" at 10 ns, assigned the same value at
--     15 ns (a transaction without an event), "1111" at 20 ns.
--   n (integer, 0): 5 at 10 ns, assigned 5 again at 20 ns, 7 at 25 ns.
--   q (integer, 3): never assigned.
--   c (std_logic, 'U'): '0' 41, '1' 42, 'H' 43, 'L' 44, 'W' 45, '1' 46,
--     '0' 47 and 'H' 48 ns. RISING_EDGE is an event to '1' or 'H' from '0'
--     or 'L' (16.7, to_x01): at 42 and 48 ns; FALLING_EDGE at 44 ('H' to
--     'L') and 47 ns. 'U', 'X' and 'W' on either side are neither.
-- The watcher resumes in the delta cycle of an event: at 10 and 20 ns the
-- first report sees event and active true, last_event and last_active 0 and
-- the value before; after `wait for 0 ns` (the next cycle) both are false
-- and last_event still counts 0 ns.
library ieee;
use ieee.std_logic_1164.all;

entity top is
end entity;

architecture rtl of top is
  signal s : std_logic_vector(3 downto 0) := "0000";
  signal n : integer := 0;
  signal q : integer := 3;
  signal c : std_logic := 'U';

begin
  drive: process
  begin
    wait for 10 ns;
    s <= "0101";
    n <= 5;
    wait for 5 ns;
    s <= "0101";
    wait for 5 ns;
    s <= "1111";
    n <= 5;
    wait for 5 ns;
    n <= 7;
    wait;
  end process;

  watch: process
  begin
    wait on s;
    report "wide w1 at " & time'image(now) & " event=" & boolean'image(s'event)
      & " active=" & boolean'image(s'active) & " last_value=" & to_string(s'last_value)
      & " last_event=" & time'image(s'last_event) & " last_active=" & time'image(s'last_active);
    wait for 0 ns;
    report "wide w2 at " & time'image(now) & " event=" & boolean'image(s'event)
      & " active=" & boolean'image(s'active) & " last_event=" & time'image(s'last_event);
  end process;

  probe: process
  begin
    wait for 12 ns;
    report "wide p1 at " & time'image(now) & " s last_event=" & time'image(s'last_event)
      & " last_active=" & time'image(s'last_active) & " last_value=" & to_string(s'last_value)
      & " n last_event=" & time'image(n'last_event) & " last_active=" & time'image(n'last_active)
      & " last_value=" & integer'image(n'last_value);
    wait for 5 ns;
    report "wide p2 at " & time'image(now) & " s last_event=" & time'image(s'last_event)
      & " last_active=" & time'image(s'last_active) & " last_value=" & to_string(s'last_value)
      & " event=" & boolean'image(s'event) & " active=" & boolean'image(s'active);
    wait for 5 ns;
    report "wide p3 at " & time'image(now) & " s last_event=" & time'image(s'last_event)
      & " last_active=" & time'image(s'last_active) & " last_value=" & to_string(s'last_value)
      & " n event=" & boolean'image(n'event) & " active=" & boolean'image(n'active)
      & " last_value=" & integer'image(n'last_value)
      & " last_event=" & time'image(n'last_event) & " last_active=" & time'image(n'last_active);
    wait for 5 ns;
    report "wide p4 at " & time'image(now) & " n last_value=" & integer'image(n'last_value)
      & " last_event=" & time'image(n'last_event) & " last_active=" & time'image(n'last_active)
      & " s last_event=" & time'image(s'last_event);
    report "wide p5 q last_event=" & time'image(q'last_event) & " last_active=" & time'image(q'last_active)
      & " last_value=" & integer'image(q'last_value)
      & " event=" & boolean'image(q'event) & " active=" & boolean'image(q'active);
    wait;
  end process;

  clk_drive: process
  begin
    wait for 41 ns;
    c <= '0';
    wait for 1 ns;
    c <= '1';
    wait for 1 ns;
    c <= 'H';
    wait for 1 ns;
    c <= 'L';
    wait for 1 ns;
    c <= 'W';
    wait for 1 ns;
    c <= '1';
    wait for 1 ns;
    c <= '0';
    wait for 1 ns;
    c <= 'H';
    wait;
  end process;

  edges: process
  begin
    wait on c;
    if rising_edge(c) then
      report "wide rise at " & time'image(now);
    end if;
    if falling_edge(c) then
      report "wide fall at " & time'image(now);
    end if;
  end process;
end architecture;
