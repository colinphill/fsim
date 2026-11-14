-- SPDX-License-Identifier: Apache-2.0
--
-- Conversion functions on an inout port of an unresolved subtype (IEEE
-- 1076-2019 6.5.7.1, 14.7.3.2, 14.7.3.3): `f_out(p) => f_in(w)`. The port's
-- driving value, the value of its own driver (14.7.3.2), reaches the actual
-- as f_out of it; the port's effective value is f_in of the actual's
-- (14.7.3.3), in the same simulation cycle. f_out doubles and f_in adds one,
-- so neither is the inverse of the other and a port that takes its own
-- driving value back would show.
--   w (default 0): its one source is f_out of the port's driving value. The
--     port's default is 0 and its driver starts there (14.7.5.2), so
--     f_out(0) = 0 is w's initial value and f_in(0) = 1 the port's.
--   The leaf drives its port with 5 at 1 ns and 7 at 3 ns:
--     0.5 ns: w = 0,  p = 1
--     1.5 ns: w = 10, p = 11   (f_out(5), f_in(10))
--     2.5 ns: w = 10, p = 11
--     3.5 ns: w = 14, p = 15   (f_out(7), f_in(14))
--   w changes once, in the cycle the port's driver does: at 1 ns and 3 ns.

package conv_integer_pkg is
  function f_out (x : integer) return integer;
  function f_in (x : integer) return integer;
end package;

package body conv_integer_pkg is
  function f_out (x : integer) return integer is
  begin
    return x * 2;
  end function;
  function f_in (x : integer) return integer is
  begin
    return x + 1;
  end function;
end package body;

use work.conv_integer_pkg.all;

entity conv_integer_leaf is
  port (p : inout integer := 0);
end entity;

architecture rtl of conv_integer_leaf is
begin
  p <= 5 after 1 ns, 7 after 3 ns;
end architecture;

use work.conv_integer_pkg.all;

entity top is
end entity;

architecture sim of top is
  signal w : integer := 0;
  alias pv is << signal .top.u.p : integer >>;
begin
  u : entity work.conv_integer_leaf port map (f_out(p) => f_in(w));

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
