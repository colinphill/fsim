-- SPDX-License-Identifier: Apache-2.0
--
-- Inout ports of the guarded signal kind bus and of a resolved subtype the
-- design declares, associated with a guarded signal (IEEE
-- 1076-2019 4.6, 6.4.2.3, 10.5.2.2, 14.7.3.2, 14.7.3.3). A port's driving
-- value is the function over its own drivers that are connected: a driver
-- whose value is determined by the null transaction is no source, and with
-- none connected a bus has the function's result for a null array (here 0,
-- the sum). The port's driving value is one source of its actual.
--   The two drivers of each port: one drives 3 at 0 ns, null at 1 ns, 9 at
--   2 ns, null at 4 ns, the other 4 at 0 ns and null at 3 ns. The ports'
--   default is 10, the top's drivers' default 0 and each top driver drives
--   5 from 0 ns.
--   w: 0 fs: 0 + (10 + 10) = 20. 0 ns: 5 + (3 + 4) = 12. 1 ns: 5 + 4 = 9.
--   2 ns: 5 + (4 + 9) = 18. 3 ns: 5 + 9 = 14. 4 ns: no connected driver,
--   the port's driving value is the sum of a null array, 0: w is 5 + 0 = 5.

package guarded_user_split_pkg is
  type int_array is array (natural range <>) of integer;
  function sum_sources (s : int_array) return integer;
  subtype summed is sum_sources integer;
end package;

package body guarded_user_split_pkg is
  function sum_sources (s : int_array) return integer is
    variable total : integer := 0;
  begin
    for i in s'range loop
      total := total + s(i);
    end loop;
    return total;
  end function;
end package body;

use work.guarded_user_split_pkg.all;

entity split_bus is
  port (p : inout summed bus := 10);
end entity;

architecture rtl of split_bus is
begin
  q1 : process
  begin
    p <= 3;
    wait for 1 ns;
    p <= null;
    wait for 1 ns;
    p <= 9;
    wait for 2 ns;
    p <= null;
    wait;
  end process;

  q2 : process
  begin
    p <= 4, null after 3 ns;
    wait;
  end process;
end architecture;

use work.guarded_user_split_pkg.all;

entity top is
end entity;

architecture sim of top is
  signal w : summed bus := 0;
begin
  ub : entity work.split_bus port map (p => w);

  tw : process
  begin
    w <= 5;
    wait;
  end process;

  events : process (w)
  begin
    report "wide w=" & integer'image(w) & " at "
      & integer'image(now / 1 ns) & ";";
  end process;
end architecture;
