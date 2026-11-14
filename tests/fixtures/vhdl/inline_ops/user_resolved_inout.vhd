-- SPDX-License-Identifier: Apache-2.0
--
-- Inout ports of a resolved subtype whose resolution function sums its
-- sources (IEEE 1076-2019 4.6). A port associated with a signal is one
-- source of the signal (6.4.2.3) whose driving value is the function over
-- the port's own sources, or the port's default when it has none
-- (14.7.3.2); the port's effective value is the signal's (14.7.3.3). The
-- function is not idempotent, so the port's driving value and its
-- effective value differ.
--   w (summed, default 0): the top's driver (4, then 5 at 1 ns), the port
--     of u (its two drivers: 3, and 0 until 10 at 2 ns) and the port of r
--     (no source: its default, 100). At 0.5 ns 4 + 3 + 100 = 107; at 1.5 ns
--     5 + 3 + 100 = 108; at 2.5 ns 5 + (3 + 10) + 100 = 118.
--   Its initial value is the function over the sources' initial driving
--   values (14.7.5.2): 0 + (0 + 0) + 100 = 100. The ports' driving values
--   are computed before w's in each cycle (14.7.3.2, NOTE 1), so w changes
--   once in each: 107 in the first delta cycle, 108 at 1 ns and 118 at
--   2 ns, without a cycle that sees only some of its new sources.
--   u.p and r.q: w's value each time.

package user_resolved_inout_pkg is
  type int_array is array (natural range <>) of integer;
  function sum_sources (s : int_array) return integer;
  subtype summed is sum_sources integer;
end package;

package body user_resolved_inout_pkg is
  function sum_sources (s : int_array) return integer is
    variable total : integer := 0;
  begin
    for i in s'range loop
      total := total + s(i);
    end loop;
    return total;
  end function;
end package body;

use work.user_resolved_inout_pkg.all;

entity user_resolved_inout_two is
  port (p : inout summed := 0);
end entity;

architecture rtl of user_resolved_inout_two is
begin
  p <= 3;
  p <= 10 after 2 ns;
end architecture;

use work.user_resolved_inout_pkg.all;

entity user_resolved_inout_none is
  port (q : inout summed := 100);
end entity;

architecture rtl of user_resolved_inout_none is
begin
end architecture;

use work.user_resolved_inout_pkg.all;

entity top is
end entity;

architecture sim of top is
  signal w : summed := 0;
  alias p is << signal .top.u.p : summed >>;
  alias q is << signal .top.r.q : summed >>;
begin
  u : entity work.user_resolved_inout_two port map (p => w);
  r : entity work.user_resolved_inout_none port map (q => w);
  w <= 4, 5 after 1 ns;

  events : process (w)
  begin
    report "wide event w=" & integer'image(w) & " at "
      & integer'image(now / 1 ns);
  end process;

  check : process
  begin
    wait for 500 ps;
    for step in 0 to 2 loop
      report "wide w=" & integer'image(w) & " p=" & integer'image(p)
        & " q=" & integer'image(q) & " at " & integer'image(step) & ".5";
      wait for 1 ns;
    end loop;
    wait;
  end process;
end architecture;
