-- SPDX-License-Identifier: Apache-2.0
--
-- Guarded signals of a resolved subtype the design declares (IEEE 1076-2019
-- 4.6, 6.4.2.3), driven by guarded assignments and by null waveform
-- elements of processes (10.5.2.2). A driver a null transaction
-- disconnected is no source until a value transaction reconnects it
-- (14.7.2); the function runs over the connected drivers' values, and with
-- none a bus takes the function's result for a null array and a register
-- keeps its value (14.7.3.2).
--   gr (register, disconnection after 1 ns, 7.4; b1 guarded by g1, b2 by
--     g2): 0 fs: 7 + 7 (the drivers' initial values, 14.7.5.2), then
--     b1's 10 and b2's 7: 17. 1 ns: b2's null: 10. 2 ns: g1 false, b1's
--     null at 3 ns leaves no driver: gr keeps 10. 4 ns: g2 true: 20.
--     5 ns: g2 false, b2's null at 6 ns: gr keeps 20 (b2's 20 a delta
--     cycle after g2's change).
--   gb (bus; p1 and p2): 0 fs: 1 + 1, then p1's 3 and p2's 5: 8. 1 ns:
--     p1's null: 5. 2 ns: p2's null after 2 ns: no driver, the sum of the
--     null array: 0. 3 ns: p1's 4: 4. 4 ns: p1's null after 1 ns: 0.
package guarded_user_register_pkg is
  type int_vec is array (natural range <>) of integer;
  function sum (v : int_vec) return integer;
  subtype rint is sum integer;
end package;

package body guarded_user_register_pkg is
  function sum (v : int_vec) return integer is
    variable r : integer := 0;
  begin
    for i in v'range loop
      r := r + v(i);
    end loop;
    return r;
  end function;
end package body;

use work.guarded_user_register_pkg.all;

entity top is
end entity;

architecture a of top is
  signal gr : rint register := 7;
  disconnect gr : rint after 1 ns;
  signal gb : rint bus := 1;
  signal g1 : boolean := true;
  signal g2 : boolean := false;
begin
  b1 : block (g1)
  begin
    gr <= guarded 10;
  end block;

  b2 : block (g2)
  begin
    gr <= guarded 20;
  end block;

  p1 : process
  begin
    gb <= 3;
    wait for 1 ns;
    gb <= null;
    wait for 2 ns;
    gb <= 4, null after 1 ns;
    wait;
  end process;

  p2 : process
  begin
    gb <= 5, null after 2 ns;
    wait;
  end process;

  mon : process (gr, gb)
  begin
    report "wide gr=" & integer'image(gr) & " gb=" & integer'image(gb)
      & " at " & integer'image(now / 1 ns) & ";";
  end process;

  stim : process
  begin
    wait for 2 ns;
    g1 <= false;
    wait for 2 ns;
    g2 <= true;
    wait for 1 ns;
    g2 <= false;
    wait for 3 ns;
    report "wide end gr=" & integer'image(gr) & " gb=" & integer'image(gb)
      & ";";
    wait;
  end process;
end architecture;
