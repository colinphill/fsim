-- SPDX-License-Identifier: Apache-2.0
--
-- Conversion functions between REAL and INTEGER on an inout port of type
-- REAL (IEEE 1076-2019 6.5.7.1, 14.7.3.2, 14.7.3.3): `f_out(p) => f_in(w)`.
-- f_out(x) = integer(x * 10.0) takes the port's driving value to the actual;
-- f_in(x) = real(x) / 10.0 takes the actual's effective value back to the
-- port. The driving value is the value of the port's own driver.
--   w (integer, default 0): its one source is f_out of the port's driving
--     value, which starts at the port's default 0.0 (14.7.5.2), so w starts at
--     f_out(0.0) = 0 and the port at f_in(0) = 0.0.
--   The leaf drives its port with 2.6 at 1 ns and -1.5 at 3 ns:
--     0.5 ns: w = 0,   p = 0.0
--     1.5 ns: w = integer(26.0) = 26, p = 26 / 10.0 = 2.6
--     2.5 ns: w = 26,  p = 2.6
--     3.5 ns: w = integer(-15.0) = -15, p = -1.5
--   (p is reported as integer(p * 10.0).) w changes once, in the cycle the
--   port's driver does: at 1 ns and 3 ns.

package conv_real_function_pkg is
  function f_out (x : real) return integer;
  function f_in (x : integer) return real;
end package;

package body conv_real_function_pkg is
  function f_out (x : real) return integer is
  begin
    return integer(x * 10.0);
  end function;
  function f_in (x : integer) return real is
  begin
    return real(x) / 10.0;
  end function;
end package body;

use work.conv_real_function_pkg.all;

entity conv_real_function_leaf is
  port (p : inout real := 0.0);
end entity;

architecture rtl of conv_real_function_leaf is
begin
  p <= 2.6 after 1 ns, -1.5 after 3 ns;
end architecture;

use work.conv_real_function_pkg.all;

entity top is
end entity;

architecture sim of top is
  signal w : integer := 0;
  alias pv is << signal .top.u.p : real >>;
begin
  u : entity work.conv_real_function_leaf port map (f_out(p) => f_in(w));

  events : process (w)
  begin
    report "wide event w=" & integer'image(w) & " at "
      & integer'image(now / 1 ns);
  end process;

  check : process
  begin
    wait for 500 ps;
    for step in 0 to 3 loop
      report "wide w=" & integer'image(w) & " p="
        & integer'image(integer(pv * 10.0)) & " at " & integer'image(step)
        & ".5";
      wait for 1 ns;
    end loop;
    wait;
  end process;
end architecture;
