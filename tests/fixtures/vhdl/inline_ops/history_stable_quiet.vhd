-- SPDX-License-Identifier: Apache-2.0
--
-- S'STABLE(T) and S'QUIET(T) (IEEE 1076-2019 16.2.4, 14.7.4): S'STABLE(T) is
-- FALSE from an event on S until T has passed without another, S'QUIET(T)
-- the same for a transaction (an assignment that leaves the value counts);
-- both change in the update phase of the cycle of the event or transaction,
-- and for T = 0 return to TRUE one cycle later.
--   s (bit, '0'): '1' at 10 ns (an event), '1' again at 20 ns (a
--     transaction without an event), '0' at 30 ns (unused). Reads, each after
--     `wait for 0 ns` steps, are the cycles after the update at 10 and 20 ns:
--     at 10 ns (first cycle) all four are FALSE; one cycle later the T = 0
--     ones are TRUE and the T = 3 ns ones FALSE until 13 ns. At 20 ns the
--     transaction leaves S'STABLE TRUE and makes S'QUIET FALSE: S'QUIET(0)
--     is TRUE again one cycle later and S'QUIET(3 ns) at 23 ns.
entity top is end entity;
architecture a of top is
  signal s : bit := '0';
begin
  drive: process begin
    wait for 10 ns; s <= '1';
    wait for 10 ns; s <= '1';
    wait for 10 ns; s <= '0';
    wait;
  end process;
  probe: process begin
    wait for 5 ns;
    report "wide A5 stable0=" & boolean'image(s'stable(0 ns)) & " stable3=" & boolean'image(s'stable(3 ns)) & " quiet0=" & boolean'image(s'quiet(0 ns)) & " quiet3=" & boolean'image(s'quiet(3 ns));
    wait for 5 ns;
    wait for 0 ns;
    report "wide 10+0 stable0=" & boolean'image(s'stable(0 ns)) & " stable3=" & boolean'image(s'stable(3 ns)) & " quiet0=" & boolean'image(s'quiet(0 ns)) & " quiet3=" & boolean'image(s'quiet(3 ns));
    wait for 0 ns;
    report "wide 10+0b stable0=" & boolean'image(s'stable(0 ns)) & " stable3=" & boolean'image(s'stable(3 ns)) & " quiet0=" & boolean'image(s'quiet(0 ns)) & " quiet3=" & boolean'image(s'quiet(3 ns));
    wait for 0 ns;
    report "wide 10+0c stable0=" & boolean'image(s'stable(0 ns)) & " stable3=" & boolean'image(s'stable(3 ns)) & " quiet0=" & boolean'image(s'quiet(0 ns)) & " quiet3=" & boolean'image(s'quiet(3 ns));
    wait for 2 ns;
    report "wide 12 stable3=" & boolean'image(s'stable(3 ns)) & " quiet3=" & boolean'image(s'quiet(3 ns));
    wait for 2 ns;
    report "wide 14 stable3=" & boolean'image(s'stable(3 ns)) & " quiet3=" & boolean'image(s'quiet(3 ns));
    wait for 6 ns;
    wait for 0 ns;
    report "wide 20+0 stable0=" & boolean'image(s'stable(0 ns)) & " stable3=" & boolean'image(s'stable(3 ns)) & " quiet0=" & boolean'image(s'quiet(0 ns)) & " quiet3=" & boolean'image(s'quiet(3 ns));
    wait for 0 ns;
    report "wide 20+0b stable0=" & boolean'image(s'stable(0 ns)) & " stable3=" & boolean'image(s'stable(3 ns)) & " quiet0=" & boolean'image(s'quiet(0 ns)) & " quiet3=" & boolean'image(s'quiet(3 ns));
    wait for 0 ns;
    report "wide 20+0c stable0=" & boolean'image(s'stable(0 ns)) & " stable3=" & boolean'image(s'stable(3 ns)) & " quiet0=" & boolean'image(s'quiet(0 ns)) & " quiet3=" & boolean'image(s'quiet(3 ns));
    wait for 4 ns;
    report "wide 24 stable3=" & boolean'image(s'stable(3 ns)) & " quiet3=" & boolean'image(s'quiet(3 ns));
    wait;
  end process;
end architecture;
