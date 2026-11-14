-- SPDX-License-Identifier: Apache-2.0
--
-- Type conversions on an inout port of type REAL with an actual of type
-- INTEGER (IEEE 1076-2019 6.5.7.1, 9.3.6, 14.7.3.2, 14.7.3.3):
-- `integer(p) => real(w)`. The port's driving value is converted to INTEGER on
-- its way to the actual, rounding to the nearest integer and away from zero
-- for a half (9.3.6); the actual's effective value is converted back to REAL
-- for the port's effective value.
--   w (integer, default 0): its one source is integer of the port's driving
--     value, which starts at the port's default 0.0 (14.7.5.2), so w starts
--     at 0 and the port at real(0) = 0.0.
--   The leaf drives its port with 2.6 at 1 ns and -1.5 at 3 ns:
--     0.5 ns: w = 0,  p = 0
--     1.5 ns: w = integer(2.6) = 3,   p = real(3) = 3.0
--     2.5 ns: w = 3,  p = 3.0
--     3.5 ns: w = integer(-1.5) = -2, p = real(-2) = -2.0
--   (p is reported as integer(p), exact for these values.) w changes once, in
--   the cycle the port's driver does: at 1 ns and 3 ns.

entity conv_real_leaf is
  port (p : inout real := 0.0);
end entity;

architecture rtl of conv_real_leaf is
begin
  p <= 2.6 after 1 ns, -1.5 after 3 ns;
end architecture;

entity top is
end entity;

architecture sim of top is
  signal w : integer := 0;
  alias pv is << signal .top.u.p : real >>;
begin
  u : entity work.conv_real_leaf port map (integer(p) => real(w));

  events : process (w)
  begin
    report "wide event w=" & integer'image(w) & " at "
      & integer'image(now / 1 ns);
  end process;

  check : process
  begin
    wait for 500 ps;
    for step in 0 to 3 loop
      report "wide w=" & integer'image(w) & " p=" & integer'image(integer(pv))
        & " at " & integer'image(step) & ".5";
      wait for 1 ns;
    end loop;
    wait;
  end process;
end architecture;
