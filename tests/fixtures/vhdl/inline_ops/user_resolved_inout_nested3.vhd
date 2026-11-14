-- SPDX-License-Identifier: Apache-2.0
--
-- Inout ports of a resolved subtype (the sum of the sources) three deep:
-- the leaf's port is associated with the port of a middle entity, which is
-- associated with the port of an outer one, which is associated with a
-- signal (IEEE 1076-2019 4.6, 6.5.7.1, 14.7.3.2, 14.7.3.3). Each port's
-- driving value is the function over its own sources, the inner port's
-- driving value among them, and is one source of its actual; the signal
-- takes all of them in one cycle.
--   w (default 0): the top's driver (5, then 6 at 1 ns) and the outer port
--     (its driver 4, and the middle port's driving value: its driver 2 and
--     the leaf port's driver, 3, then 10 at 1 ns):
--     0 + (0 + (0 + 0)) = 0 initially, 5 + (4 + (2 + 3)) = 14, and at 1 ns
--     6 + (4 + (2 + 10)) = 22.

package user_resolved_inout_nested3_pkg is
  type int_array is array (natural range <>) of integer;
  function sum_sources (s : int_array) return integer;
  subtype summed is sum_sources integer;
end package;

package body user_resolved_inout_nested3_pkg is
  function sum_sources (s : int_array) return integer is
    variable total : integer := 0;
  begin
    for i in s'range loop
      total := total + s(i);
    end loop;
    return total;
  end function;
end package body;

use work.user_resolved_inout_nested3_pkg.all;

entity nested3_leaf is
  port (l : inout summed := 0);
end entity;

architecture rtl of nested3_leaf is
begin
  l <= 3, 10 after 1 ns;
end architecture;

use work.user_resolved_inout_nested3_pkg.all;

entity nested3_inner is
  port (m2 : inout summed := 0);
end entity;

architecture rtl of nested3_inner is
begin
  u : entity work.nested3_leaf port map (l => m2);
  m2 <= 2;
end architecture;

use work.user_resolved_inout_nested3_pkg.all;

entity nested3_outer is
  port (m1 : inout summed := 0);
end entity;

architecture rtl of nested3_outer is
begin
  u : entity work.nested3_inner port map (m2 => m1);
  m1 <= 4;
end architecture;

use work.user_resolved_inout_nested3_pkg.all;

entity top is
end entity;

architecture sim of top is
  signal w : summed := 0;
begin
  a : entity work.nested3_outer port map (m1 => w);
  w <= 5, 6 after 1 ns;

  events : process (w)
  begin
    report "wide w=" & integer'image(w) & " at "
      & integer'image(now / 1 ns) & ";";
  end process;
end architecture;
