-- SPDX-License-Identifier: Apache-2.0
--
-- A guarded bus of a resolved subtype the design declares (IEEE 1076-2019
-- 4.6, 6.4.2.3) with a disconnection specification of 2 ns (7.4). A false
-- guard schedules a null transaction for the block's driver 2 ns later
-- (11.6); a value assigned before then deletes it (10.5.2.2: an inertial
-- assignment deletes the driver's later transactions, null ones too), and
-- a value after a disconnection reconnects the driver (14.7.2). Null
-- transactions of one cycle take effect together: the bus with no connected
-- driver takes sum of the empty array, 0 (14.7.3.2).
--   0 fs: gb = 7 + 7 (the drivers' initial values, 14.7.5.2); then b1
--         drives 20 and b2's guard is false: gb = 20 + 7 = 27.
--   2 ns: b2's null transaction: gb = 20.
--   3 ns: g1 false (b1's null due at 5 ns); 4 ns: g1 true, b1 assigns 20
--         again, which deletes that null: gb stays 20.
--   6 ns: g2 true: gb = 20 + 30.
--   7 ns: both guards false: both nulls at 9 ns, gb = 0.
package rp is
  type int_vec is array (natural range <>) of integer;
  function sum (v : int_vec) return integer;
  subtype rint is sum integer;
end package;

package body rp is
  function sum (v : int_vec) return integer is
    variable r : integer := 0;
  begin
    for i in v'range loop
      r := r + v(i);
    end loop;
    return r;
  end function;
end package body;

use work.rp.all;

entity top is
end entity;

architecture a of top is
  signal gb : rint bus := 7;
  disconnect gb : rint after 2 ns;
  signal g1 : boolean := true;
  signal g2 : boolean := false;
  signal c1 : integer := 20;
begin
  b1 : block (g1)
  begin
    gb <= guarded c1;
  end block;

  b2 : block (g2)
  begin
    gb <= guarded 30;
  end block;

  mon : process (gb)
  begin
    report "wide bus gb=" & integer'image(gb) & " at " & integer'image(now / 1 ns)
      & ";";
  end process;

  stim : process
  begin
    wait for 3 ns;
    g1 <= false;
    wait for 1 ns;
    g1 <= true;
    wait for 2 ns;
    g2 <= true;
    wait for 1 ns;
    g1 <= false;
    g2 <= false;
    wait for 3 ns;
    report "wide bus end gb=" & integer'image(gb) & ";";
    wait;
  end process;
end architecture;
