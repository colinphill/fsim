-- SPDX-License-Identifier: Apache-2.0
--
-- Conversion functions on an inout port of a resolved subtype whose
-- resolution function sums its sources (IEEE 1076-2019 4.6, 6.5.7.1,
-- 14.7.3.2, 14.7.3.3): `twice(p) => bump(w)`. The port's driving value is
-- the function over the port's own drivers, and `twice` of it is one source
-- of w; the conversion of w's effective value, `bump(w)`, is the port's
-- effective value and no source of the port. The conversions are not
-- inverses, and the function is not idempotent, so a port that counted its
-- own effective value among its sources would show.
--   w (summed, default 0): the top's driver (4, then 5 at 1 ns) and the
--     conversion of the port. The top's driver starts at w's default 0 and
--     the port's at its own default 0 (14.7.5.2), so the port's driving
--     value is 0 and w starts at 0 + twice(0) = 0, the port at bump(0) = 1.
--   The leaf drives its port with two concurrent assignments, so with two
--   drivers: 3 at once, and 0 until 10 at 2 ns. The port's driving value is
--   the function over both:
--     0.5 ns: 3 + 0 = 3 -> twice 6;  w = 4 + 6 = 10, p = bump(10) = 11
--     1.5 ns: w = 5 + 6 = 11, p = 12
--     2.5 ns: 3 + 10 = 13 -> twice 26; w = 5 + 26 = 31, p = 32
--     3.5 ns: w = 31, p = 32
--   w changes once in each cycle in which a source does (14.7.3.2, NOTE 1:
--   the port's driving value is computed before w's): at 0 ns (delta 1),
--   1 ns and 2 ns.

package conv_user_resolved_pkg is
  type int_array is array (natural range <>) of integer;
  function sum_sources (s : int_array) return integer;
  subtype summed is sum_sources integer;
  function twice (x : summed) return summed;
  function bump (x : summed) return summed;
end package;

package body conv_user_resolved_pkg is
  function sum_sources (s : int_array) return integer is
    variable total : integer := 0;
  begin
    for i in s'range loop
      total := total + s(i);
    end loop;
    return total;
  end function;
  function twice (x : summed) return summed is
  begin
    return x * 2;
  end function;
  function bump (x : summed) return summed is
  begin
    return x + 1;
  end function;
end package body;

use work.conv_user_resolved_pkg.all;

entity conv_user_resolved_leaf is
  port (p : inout summed := 0);
end entity;

architecture rtl of conv_user_resolved_leaf is
begin
  p <= 3;
  p <= 10 after 2 ns;
end architecture;

use work.conv_user_resolved_pkg.all;

entity top is
end entity;

architecture sim of top is
  signal w : summed := 0;
  alias pv is << signal .top.u.p : summed >>;
begin
  u : entity work.conv_user_resolved_leaf port map (twice(p) => bump(w));
  w <= 4, 5 after 1 ns;

  events : process (w)
  begin
    report "wide event w=" & integer'image(w) & " at "
      & integer'image(now / 1 ns);
  end process;

  check : process
  begin
    wait for 500 ps;
    for step in 0 to 3 loop
      report "wide w=" & integer'image(w) & " p=" & integer'image(pv)
        & " at " & integer'image(step) & ".5";
      wait for 1 ns;
    end loop;
    wait;
  end process;
end architecture;
