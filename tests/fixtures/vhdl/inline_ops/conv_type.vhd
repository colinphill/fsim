-- SPDX-License-Identifier: Apache-2.0
--
-- A type conversion in the formal part and a conversion function in the
-- actual part of an inout port (IEEE 1076-2019 6.5.7.1, 9.3.6, 14.7.3.2,
-- 14.7.3.3): `t2(p) => f_in(w)`, the port of type T1 and the actual of the
-- distinct type T2. The port's driving value is converted to T2 on its way
-- to the actual (the type conversion of an integer value keeps it), and
-- f_in converts the actual's effective value back to T1, adding one, for the
-- port's effective value.
--   w (T2, default 0): its one source is t2 of the port's driving value,
--     which starts at the port's default 0 (14.7.5.2), so w starts at
--     t2(0) = 0 and the port at f_in(0) = 1.
--   The leaf drives its port with 5 at 1 ns and -7 at 3 ns:
--     0.5 ns: w = 0,  p = 1
--     1.5 ns: w = 5,  p = 6    (t2(5), f_in(5))
--     2.5 ns: w = 5,  p = 6
--     3.5 ns: w = -7, p = -6   (t2(-7), f_in(-7))
--   w changes once, in the cycle the port's driver does: at 1 ns and 3 ns.

package conv_type_pkg is
  type t1 is range -1000 to 1000;
  type t2 is range -100 to 100;
  function f_in (x : t2) return t1;
end package;

package body conv_type_pkg is
  function f_in (x : t2) return t1 is
  begin
    return t1 (x) + 1;
  end function;
end package body;

use work.conv_type_pkg.all;

entity conv_type_leaf is
  port (p : inout t1 := 0);
end entity;

architecture rtl of conv_type_leaf is
begin
  p <= 5 after 1 ns, -7 after 3 ns;
end architecture;

use work.conv_type_pkg.all;

entity top is
end entity;

architecture sim of top is
  signal w : t2 := 0;
  alias pv is << signal .top.u.p : t1 >>;
begin
  u : entity work.conv_type_leaf port map (t2(p) => f_in(w));

  events : process (w)
  begin
    report "wide event w=" & t2'image(w) & " at "
      & integer'image(now / 1 ns);
  end process;

  check : process
  begin
    wait for 500 ps;
    for step in 0 to 3 loop
      report "wide w=" & t2'image(w) & " p=" & t1'image(pv)
        & " at " & integer'image(step) & ".5";
      wait for 1 ns;
    end loop;
    wait;
  end process;
end architecture;
