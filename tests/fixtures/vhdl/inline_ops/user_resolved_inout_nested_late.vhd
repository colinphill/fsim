-- SPDX-License-Identifier: Apache-2.0
--
-- As user_resolved_inout_nested, with the element's chain a nanosecond later:
-- inout ports of a resolved subtype (the sum of the sources) nested two
-- deep: the inner port is associated with the middle port, which is
-- associated with a signal or with an element of one (IEEE 1076-2019 4.6,
-- 6.5.7.1, 14.7.3.2, 14.7.3.3). The inner port is one source of the middle
-- port's signal, whose driving value is computed in the same cycle, and
-- that is one source of the top's signal, whichever the actual: the signal
-- changes once in each cycle, never seeing the inner port's new driving
-- value a cycle after the top's own.
--   w (default 0): the top's driver (5, then 6 at 1 ns) and the middle
--     port a.m (its driver 4 and the inner port's: 3, then 10 at 1 ns):
--     0 + (0 + 0) = 0 initially, 5 + (4 + 3) = 12, and at 1 ns
--     6 + (4 + 10) = 20.
--   ws (an array of resolved elements): ws(0) has the top's driver 7 only.
--     ws(1) has the top's 8, then 9 at 1 ns, and the middle port b.m:
--     8 + (4 + 3) = 15 (see below for the later steps).
--   The element's chain changes one nanosecond after the signal's (its own
--   entities, whose inner driver changes at 2 ns), so that the two chains
--   never update in one cycle: ws(1) = 9 + (4 + 3) = 16 at 1 ns, and
--   9 + (4 + 10) = 23 at 2 ns.
-- The monitor reports each cycle in which any of them changes.

package user_resolved_inout_nested_pkg is
  type int_array is array (natural range <>) of integer;
  function sum_sources (s : int_array) return integer;
  subtype summed is sum_sources integer;
  type summed_pair is array (0 to 1) of summed;
end package;

package body user_resolved_inout_nested_pkg is
  function sum_sources (s : int_array) return integer is
    variable total : integer := 0;
  begin
    for i in s'range loop
      total := total + s(i);
    end loop;
    return total;
  end function;
end package body;

use work.user_resolved_inout_nested_pkg.all;

entity nested_inner is
  port (l : inout summed := 0);
end entity;

architecture rtl of nested_inner is
begin
  l <= 3, 10 after 1 ns;
end architecture;

use work.user_resolved_inout_nested_pkg.all;

entity nested_middle is
  port (m : inout summed := 0);
end entity;

architecture rtl of nested_middle is
begin
  u : entity work.nested_inner port map (l => m);
  m <= 4;
end architecture;

use work.user_resolved_inout_nested_pkg.all;

entity nested_inner_late is
  port (l : inout summed := 0);
end entity;

architecture rtl of nested_inner_late is
begin
  l <= 3, 10 after 2 ns;
end architecture;

use work.user_resolved_inout_nested_pkg.all;

entity nested_middle_late is
  port (m : inout summed := 0);
end entity;

architecture rtl of nested_middle_late is
begin
  u : entity work.nested_inner_late port map (l => m);
  m <= 4;
end architecture;

use work.user_resolved_inout_nested_pkg.all;

entity top is
end entity;

architecture sim of top is
  signal w : summed := 0;
  signal ws : summed_pair := (0, 0);
begin
  a : entity work.nested_middle port map (m => w);
  b : entity work.nested_middle_late port map (m => ws(1));
  w <= 5, 6 after 1 ns;
  ws(0) <= 7;
  ws(1) <= 8, 9 after 1 ns;

  events : process (w, ws)
  begin
    report "wide w=" & integer'image(w) & " ws="
      & integer'image(ws(0)) & "," & integer'image(ws(1)) & " at "
      & integer'image(now / 1 ns) & ";";
  end process;
end architecture;
