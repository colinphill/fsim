-- SPDX-License-Identifier: Apache-2.0
--
-- A guarded (bus) inout port of a resolved subtype the design declares with
-- ten drivers, the function being the sum (IEEE 1076-2019 4.6, 6.4.2.3,
-- 10.5.2.2, 14.7.3.2): the port's driving value is the function over the
-- connected drivers, whatever their number (no driver count is special),
-- and over a null array with none. The port's driving value is one source
-- of its actual w, whose own driver drives 100.
--   Driver i (0 to 9) drives i + 1 from 0 ns and null from i + 1 ns.
--   0 fs: w = 0 + 0 (every driver's default is 0). 0 ns: 100 + 55 = 155.
--   At k ns (1 to 10) the drivers 0 to k - 1 are off: 100 + 55 - k (k + 1)
--   / 2 = 154, 152, 149, 145, 140, 134, 127, 119, 110, and with all ten off
--   100 + 0 (the sum of a null array) = 100.

package guarded_user_split_many_pkg is
  type int_array is array (natural range <>) of integer;
  function sum_sources (s : int_array) return integer;
  subtype summed is sum_sources integer;
end package;

package body guarded_user_split_many_pkg is
  function sum_sources (s : int_array) return integer is
    variable total : integer := 0;
  begin
    for i in s'range loop
      total := total + s(i);
    end loop;
    return total;
  end function;
end package body;

use work.guarded_user_split_many_pkg.all;

entity split_many is
  port (p : inout summed bus := 0);
end entity;

architecture rtl of split_many is
begin
  drivers : for i in 0 to 9 generate
    process
    begin
      p <= i + 1, null after (i + 1) * 1 ns;
      wait;
    end process;
  end generate;
end architecture;

use work.guarded_user_split_many_pkg.all;

entity top is
end entity;

architecture sim of top is
  signal w : summed bus := 0;
begin
  u : entity work.split_many port map (p => w);

  tw : process
  begin
    w <= 100;
    wait;
  end process;

  events : process (w)
  begin
    report "wide w=" & integer'image(w) & " at "
      & integer'image(now / 1 ns) & ";";
  end process;
end architecture;
