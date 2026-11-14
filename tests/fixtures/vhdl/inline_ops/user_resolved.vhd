-- SPDX-License-Identifier: Apache-2.0
--
-- Signals of resolved subtypes whose resolution functions the design
-- declares (IEEE 1076-2019 4.6). The update commit calls the function with
-- the values of the signal's connected drivers, first driver leftmost
-- (14.7.3.2); the v4 kernel runs each function as compiled code (no
-- tier:not_compiled:register_width): the program for the declared drivers
-- and a guarded signal's program for k connected ones each take an input
-- of fixed width.
--   s (sum, 3 drivers: p1, p2 and the constant 100), w (wor, 2 drivers).
--   gr (sum register) is guarded: blocks b1 (guard g1, value 20) and b2
--   (guard g2, value 30) drive it. A false guard disconnects the block's
--   driver (a null transaction after 0 ns, 7.4, 11.6); a later value
--   reconnects it (14.7.2). With no connected driver a register keeps its
--   value.
-- The initial value of each signal is its function applied to its drivers'
-- initial values, the default (14.7.5.2): gr = 3 + 3. Both guards start
-- false, so both drivers of gr disconnect in the first delta, together: gr
-- keeps 6 (not the 3 of one driver disconnected before the other).
--   report 0: s = 1 + 2 + 100     w '0'  gr 6
--   report 1 (en '1', g1 true):   s = 5 + 10 + 100, w '1', gr 20
--   report 2 (g2 true):           gr 20 + 30
--   report 3 (g1 false):          gr 30
--   report 4 (g2 false, en '0'):  s 103, w '0', gr keeps 30
package rp is
  type int_vec is array (natural range <>) of integer;
  function sum (v : int_vec) return integer;
  subtype rint is sum integer;
  type bv_vec is array (natural range <>) of bit;
  function wor (v : bv_vec) return bit;
  subtype rbit is wor bit;
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
  function wor (v : bv_vec) return bit is
  begin
    for i in v'range loop
      if v(i) = '1' then
        return '1';
      end if;
    end loop;
    return '0';
  end function;
end package body;
use work.rp.all;
entity top is end entity;
architecture a of top is
  signal s : rint := 0;
  signal w : rbit;
  signal gr : rint register := 3;
  signal en : bit := '0';
  signal g1, g2 : boolean := false;
begin
  p1 : process (en)
  begin
    if en = '1' then
      s <= 5;
      w <= '1';
    else
      s <= 1;
      w <= '0';
    end if;
  end process;
  p2 : process (en)
  begin
    if en = '1' then
      s <= 10;
    else
      s <= 2;
    end if;
    w <= '0';
  end process;
  p3 : s <= 100;
  b1 : block (g1)
  begin
    gr <= guarded 20;
  end block;
  b2 : block (g2)
  begin
    gr <= guarded 30;
  end block;
  stim : process
    procedure show (n : integer) is
    begin
      report "wide user " & integer'image(n) & " s=" & integer'image(s) & " w="
        & bit'image(w) & " gr="
        & integer'image(gr) & ";";
    end procedure;
  begin
    wait for 1 ns;
    show(0);
    en <= '1';
    g1 <= true;
    wait for 1 ns;
    show(1);
    g2 <= true;
    wait for 1 ns;
    show(2);
    g1 <= false;
    wait for 1 ns;
    show(3);
    g2 <= false;
    en <= '0';
    wait for 1 ns;
    show(4);
    wait;
  end process;
end architecture;
