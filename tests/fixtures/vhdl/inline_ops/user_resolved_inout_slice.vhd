-- SPDX-License-Identifier: Apache-2.0
--
-- Inout ports of a resolved subtype whose actuals are a slice and an
-- element of an array of resolved elements (IEEE 1076-2019 4.6, 6.5.7.1,
-- 14.7.3.2, 14.7.3.3). A port associated with part of a signal is one
-- source of each element of that part, and its driving value is the
-- function over its own sources for each of its elements; its effective
-- value is the part's. The function sums its sources.
--   b (three elements, default 0): the top's driver (10, 20, 30, then 11,
--     21, 31 at 1 ns), the vector port of a over b(1 to 2) (a driver of
--     its element 0: 3, then 5 at 1 ns, and of its element 1: 4) and the
--     port of c over b(0) (its driver 6):
--     b(0) = 10 + 6 = 16, b(1) = 20 + 3 = 23, b(2) = 30 + 4 = 34, and at
--     1 ns 11 + 6 = 17, 21 + 5 = 26, 31 + 4 = 35. Initially every
--     element is 0 (the sources' defaults sum to 0, 14.7.5.2).
--   a's port holds b(1 to 2)'s values as its effective value, c's port
--     b(0)'s.

package user_resolved_inout_slice_pkg is
  type int_array is array (natural range <>) of integer;
  function sum_sources (s : int_array) return integer;
  subtype summed is sum_sources integer;
  type summed_vec is array (natural range <>) of summed;
end package;

package body user_resolved_inout_slice_pkg is
  function sum_sources (s : int_array) return integer is
    variable total : integer := 0;
  begin
    for i in s'range loop
      total := total + s(i);
    end loop;
    return total;
  end function;
end package body;

use work.user_resolved_inout_slice_pkg.all;

entity slice_two is
  port (p : inout summed_vec(0 to 1) := (0, 0));
end entity;

architecture rtl of slice_two is
begin
  p(0) <= 3, 5 after 1 ns;
  p(1) <= 4;
end architecture;

use work.user_resolved_inout_slice_pkg.all;

entity slice_one is
  port (q : inout summed := 0);
end entity;

architecture rtl of slice_one is
begin
  q <= 6;
end architecture;

use work.user_resolved_inout_slice_pkg.all;

entity top is
end entity;

architecture sim of top is
  signal b : summed_vec(0 to 2) := (0, 0, 0);
begin
  a : entity work.slice_two port map (p => b(1 to 2));
  c : entity work.slice_one port map (q => b(0));
  b <= (10, 20, 30), (11, 21, 31) after 1 ns;

  events : process (b)
  begin
    report "wide b=" & integer'image(b(0)) & "," & integer'image(b(1)) & ","
      & integer'image(b(2)) & " at " & integer'image(now / 1 ns) & ";";
  end process;
end architecture;
