-- SPDX-License-Identifier: Apache-2.0
--
-- The history of a VHDL signal survives a save and a restart (IEEE
-- 1076-2019 16.2.4, 20.7). The run is saved at 20 ns; the reports after it
-- count from the events and transactions before it.
--   s (bit, '0'): '1' at 10 ns, '1' again at 25 ns (a transaction without an
--     event), '0' at 35 ns.
--   12 ns: s='1', its event and its transaction 2 ns ago, last value '0'.
--   22 ns (after the restart): event 12 ns ago, transaction 12 ns ago.
--   27 ns: event 17 ns ago, transaction 2 ns ago (25 ns), last value '0'.
--   36 ns: s='0': event and transaction 1 ns ago, last value '1'.
entity top is
end entity;

architecture rtl of top is
  signal s : bit := '0';
begin
  drive: process
  begin
    wait for 10 ns;
    s <= '1';
    wait for 15 ns;
    s <= '1';
    wait for 10 ns;
    s <= '0';
    wait;
  end process;

  probe: process
  begin
    wait for 12 ns;
    report "hist at " & time'image(now) & " s=" & bit'image(s)
      & " last_event=" & time'image(s'last_event)
      & " last_active=" & time'image(s'last_active)
      & " last_value=" & bit'image(s'last_value);
    wait for 10 ns;
    report "hist at " & time'image(now) & " s=" & bit'image(s)
      & " last_event=" & time'image(s'last_event)
      & " last_active=" & time'image(s'last_active)
      & " last_value=" & bit'image(s'last_value);
    wait for 5 ns;
    report "hist at " & time'image(now) & " s=" & bit'image(s)
      & " last_event=" & time'image(s'last_event)
      & " last_active=" & time'image(s'last_active)
      & " last_value=" & bit'image(s'last_value);
    wait for 9 ns;
    report "hist at " & time'image(now) & " s=" & bit'image(s)
      & " last_event=" & time'image(s'last_event)
      & " last_active=" & time'image(s'last_active)
      & " last_value=" & bit'image(s'last_value);
    wait;
  end process;
end architecture;
