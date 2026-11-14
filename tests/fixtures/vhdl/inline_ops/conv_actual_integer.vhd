-- SPDX-License-Identifier: Apache-2.0
--
-- A conversion function in the actual part only of an inout port of an
-- unresolved subtype (IEEE 1076-2019 6.5.7.1, 14.7.3.2, 14.7.3.3):
-- `p => f_in(w)`. Information flows from the formal to the actual unchanged
-- (the types are the same): the port's driving value, the value of its own
-- driver, is w's one source. The port's effective value is f_in of w's
-- (f_in adds one), in the same simulation cycle.
--   w (default 0): the port's driver starts at the port's default 0
--     (14.7.5.2), so w starts at 0 and the port at f_in(0) = 1.
--   The leaf drives its port with 5 at 1 ns and 7 at 3 ns:
--     0.5 ns: w = 0, p = 1
--     1.5 ns: w = 5, p = 6
--     2.5 ns: w = 5, p = 6
--     3.5 ns: w = 7, p = 8
--   w changes once, in the cycle the port's driver does: at 1 ns and 3 ns.

package conv_actual_integer_pkg is
  function f_in (x : integer) return integer;
end package;

package body conv_actual_integer_pkg is
  function f_in (x : integer) return integer is
  begin
    return x + 1;
  end function;
end package body;

use work.conv_actual_integer_pkg.all;

entity conv_actual_integer_leaf is
  port (p : inout integer := 0);
end entity;

architecture rtl of conv_actual_integer_leaf is
begin
  p <= 5 after 1 ns, 7 after 3 ns;
end architecture;

use work.conv_actual_integer_pkg.all;

entity top is
end entity;

architecture sim of top is
  signal w : integer := 0;
  alias pv is << signal .top.u.p : integer >>;
begin
  u : entity work.conv_actual_integer_leaf port map (p => f_in(w));

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
