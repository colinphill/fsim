-- SPDX-License-Identifier: Apache-2.0
--
-- S'TRANSACTION in a sensitivity list and in a wait on (IEEE 1076-2019
-- 16.2.4, 14.7.4, 14.7.5.2, 11.3). S'TRANSACTION toggles in the update phase
-- of every cycle in which s is active, including an assignment of an
-- unchanged value, and both processes resume in that cycle. The checks
-- compare the attribute with the value read before the resumption, so they
-- do not depend on its initial value (16.2.4).
--   s (bit, '0'): '0' at 1 ns (unchanged), '1' at 2 ns (event), '1' at 3 ns
--   (unchanged).
--   sl has s'transaction in its sensitivity list; it runs once at
--   initialization and then at 1, 2 and 3 ns.
--   wp waits on s'transaction, notes s'event, and reports one delta later
--   (wait for 0 ns) so that its line follows the line of sl.
entity top is
end entity;

architecture rtl of top is
  signal s : bit := '0';
begin
  drive: process
  begin
    wait for 1 ns;
    s <= '0';
    wait for 1 ns;
    s <= '1';
    wait for 1 ns;
    s <= '1';
    wait;
  end process;

  sl: process (s'transaction)
    variable first : boolean := true;
    variable seen : bit;
  begin
    if first then
      first := false;
    else
      report "wide sl toggled=" & boolean'image(s'transaction /= seen)
        & " at " & time'image(now);
    end if;
    seen := s'transaction;
  end process;

  wp: process
    variable p : bit;
    variable ev : boolean;
  begin
    loop
      p := s'transaction;
      wait on s'transaction;
      ev := s'event;
      wait for 0 ns;
      report "wide wp toggled=" & boolean'image(s'transaction /= p)
        & " event=" & boolean'image(ev) & " at " & time'image(now);
    end loop;
  end process;
end architecture;
