-- SPDX-License-Identifier: Apache-2.0
--
-- Signals of resolved subtypes the design declares (IEEE 1076-2019 4.6),
-- every driver a process: the kernel holds each driver's value and runs the
-- resolution function once in each update phase that changes a driver
-- (14.7.3.2), over the drivers in the order of their processes.
--   s (sum of p1, p2 and p3's drivers, default 0): 0 at initialization
--     (the resolution of the drivers' initial values, 14.7.5.2), then
--     1 + 2 + 100 = 103 in the first delta; at 1 ns 5 + 10 + 100 = 115;
--     at 2 ns 6 + 9 + 100 = 115 again, which is no event, so mon does not
--     resume (14.7.3.4); at 3 ns 103.
--   w (or of p1 and p2's drivers): '0', '1' at 1 ns, '0' at 2 ns.
--   d (sum of q1 and q2's drivers, default 1): 2 at initialization, then
--     q1's 3 gives 4; q1's projected 5 at 2 ns gives 6; q2's transport
--     10 at 4 ns gives 15; q1's 0 at 11 ns gives 10.
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

entity top is
end entity;

architecture a of top is
  signal s : rint := 0;
  signal w : rbit;
  signal d : rint := 1;
  signal sel : integer := 0;
begin
  p1 : process (sel)
  begin
    case sel is
      when 1 => s <= 5; w <= '1';
      when 2 => s <= 6; w <= '0';
      when others => s <= 1; w <= '0';
    end case;
  end process;

  p2 : process (sel)
  begin
    case sel is
      when 1 => s <= 10;
      when 2 => s <= 9;
      when others => s <= 2;
    end case;
    w <= '0';
  end process;

  p3 : s <= 100;

  q1 : process
  begin
    d <= 3, 5 after 2 ns;
    wait for 10 ns;
    d <= 0 after 1 ns;
    wait;
  end process;

  q2 : process
  begin
    wait for 1 ns;
    d <= transport 10 after 3 ns;
    wait;
  end process;

  mons : process (s)
  begin
    report "wide s=" & integer'image(s) & " at " & integer'image(now / 1 ns)
      & ";";
  end process;

  monw : process (w)
  begin
    report "wide w=" & bit'image(w) & " at " & integer'image(now / 1 ns)
      & ";";
  end process;

  mond : process (d)
  begin
    report "wide d=" & integer'image(d) & " at " & integer'image(now / 1 ns)
      & ";";
  end process;

  stim : process
  begin
    wait for 1 ns;
    sel <= 1;
    wait for 1 ns;
    sel <= 2;
    wait for 1 ns;
    sel <= 0;
    wait;
  end process;
end architecture;
