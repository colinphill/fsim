-- SPDX-License-Identifier: Apache-2.0
--
-- A conversion function in the actual part only of an inout port of a
-- resolved subtype whose function sums its sources (IEEE 1076-2019 4.6,
-- 6.5.7.1, 14.7.3.2, 14.7.3.3): `p => bump(w)`. The port's driving value,
-- the function over its own drivers, is one source of w unchanged; the
-- port's effective value is bump of w's (bump adds one) and no source of
-- the port.
--   w (summed, default 0): the top's driver (4, then 5 at 1 ns) and the
--     port. The leaf drives its port with two concurrent assignments, so
--     with two drivers: 3 at once, and 0 until 10 at 2 ns. The top's driver
--     and the port's start at 0 (14.7.5.2): w starts at 0, the port at
--     bump(0) = 1.
--     0.5 ns: port 3 + 0 = 3;  w = 4 + 3 = 7,  p = bump(7) = 8
--     1.5 ns: w = 5 + 3 = 8,  p = 9
--     2.5 ns: port 3 + 10 = 13; w = 5 + 13 = 18, p = 19
--   w changes once in each cycle in which a source does: at 0 ns (delta 1),
--   1 ns and 2 ns.

package conv_actual_user_pkg is
  type int_array is array (natural range <>) of integer;
  function sum_sources (s : int_array) return integer;
  subtype summed is sum_sources integer;
  function bump (x : summed) return summed;
end package;

package body conv_actual_user_pkg is
  function sum_sources (s : int_array) return integer is
    variable total : integer := 0;
  begin
    for i in s'range loop
      total := total + s(i);
    end loop;
    return total;
  end function;
  function bump (x : summed) return summed is
  begin
    return x + 1;
  end function;
end package body;

use work.conv_actual_user_pkg.all;

entity conv_actual_user_leaf is
  port (p : inout summed := 0);
end entity;

architecture rtl of conv_actual_user_leaf is
begin
  p <= 3;
  p <= 10 after 2 ns;
end architecture;

use work.conv_actual_user_pkg.all;

entity top is
end entity;

architecture sim of top is
  signal w : summed := 0;
  alias pv is << signal .top.u.p : summed >>;
begin
  u : entity work.conv_actual_user_leaf port map (p => bump(w));
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
      report "wide w=" & integer'image(w) & " p=" & integer'image(pv)
        & " at " & integer'image(step) & ".5";
      wait for 1 ns;
    end loop;
    wait;
  end process;
end architecture;
