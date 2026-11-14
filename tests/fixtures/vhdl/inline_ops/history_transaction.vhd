-- SPDX-License-Identifier: Apache-2.0
--
-- S'TRANSACTION (IEEE 1076-2019 16.2.4, 14.7.4, 14.7.5.2): in the update
-- phase of a cycle in which S is active, the kernel updates S, then the
-- implicit signals, and S'TRANSACTION takes (not S'TRANSACTION) once, before
-- any process resumes. A process that resumes in the next cycle and reads
-- s'transaction sees the toggled value, a wait on s'transaction resumes in
-- that cycle, and a copy `tr <= s'transaction` changes in the update phase
-- of that cycle, so a process sees the new copy one cycle later.
--   s (bit, '0'): '0' at 1 ns (a transaction without an event), '1' at 2 ns,
--     '1' at 3 ns, '0' at 4 ns. s'transaction starts at '1' (the value of
--     the attribute before any transaction) and so is '0' after 1 ns, '1'
--     after 2 ns, '0' after 3 ns and '1' after 4 ns.
--   w waits on s'transaction and reads it directly: it resumes at 1, 2, 3
--     and 4 ns in the cycle after the update and sees the new value.
--   cp waits on the copy tr: it resumes one cycle after w, and sees tr
--     equal to the toggled value.
entity top is
end entity;

architecture rtl of top is
  signal s : bit := '0';
  signal tr : bit := '1';
begin
  tr <= s'transaction;

  drive: process
  begin
    wait for 1 ns;
    s <= '0';
    wait for 1 ns;
    s <= '1';
    wait for 1 ns;
    s <= '1';
    wait for 1 ns;
    s <= '0';
    wait;
  end process;

  w: process
  begin
    wait on s'transaction;
    report "wide w " & bit'image(s'transaction) & " tr=" & bit'image(tr)
      & " at " & time'image(now);
  end process;

  cp: process
  begin
    wait on tr;
    report "wide cp " & bit'image(tr) & " direct=" & bit'image(s'transaction)
      & " at " & time'image(now);
  end process;
end architecture;
