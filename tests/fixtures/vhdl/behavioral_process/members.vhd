-- SPDX-License-Identifier: Apache-2.0
--
-- Delayed signal assignments of one element in processes with a sensitivity
-- list (IEEE 1076-2019 10.5.2.2, 11.3): a registered process with an
-- inertial `after`, and two combinational ones with transport and with
-- `reject ... inertial`. They are combinational and sequential kernel
-- members that edit the kernel's projected output waveforms. The expected
-- events (members.expected) are derived by hand:
--   clk rises at 5 and 15 ns; d is 0, then 1 from 5 ns, then 2 from 15 ns;
--   q takes d + 1 two ns after each edge (1 at 7 ns, 2 at 17 ns);
--   qt follows d transport-delayed by 3 ns (1 at 8, 2 at 18 ns);
--   qr follows d through an inertial delay of 4 ns with a 1 ns reject limit
--   (1 at 9, 2 at 19 ns).
library ieee;
use ieee.std_logic_1164.all;

entity members is
end entity members;

architecture tb of members is
  signal clk : std_logic := '0';
  signal d : integer := 0;
  signal q : integer := 0;
  signal qt : integer := 0;
  signal qr : integer := 0;
begin
  clkgen : process
  begin
    for i in 1 to 4 loop
      wait for 5 ns;
      clk <= not clk;
    end loop;
    wait;
  end process clkgen;

  reg : process (clk)
  begin
    if rising_edge(clk) then
      q <= d + 1 after 2 ns;
      d <= d + 1;
    end if;
  end process reg;

  trn : process (d)
  begin
    qt <= transport d after 3 ns;
  end process trn;

  rej : process (d)
  begin
    qr <= reject 1 ns inertial d after 4 ns;
  end process rej;

  mon_q : process
  begin
    wait on q;
    if q = 1 and now = 7 ns then
      report "q=1 @7 ns";
    else
      report "q UNEXPECTED";
    end if;
    wait on q;
    if q = 2 and now = 17 ns then
      report "q=2 @17 ns";
    else
      report "q UNEXPECTED";
    end if;
    loop
      wait on q;
      report "q UNEXPECTED";
    end loop;
  end process mon_q;

  mon_qt : process
  begin
    wait on qt;
    if qt = 1 and now = 8 ns then
      report "qt=1 @8 ns";
    else
      report "qt UNEXPECTED";
    end if;
    wait on qt;
    if qt = 2 and now = 18 ns then
      report "qt=2 @18 ns";
    else
      report "qt UNEXPECTED";
    end if;
    loop
      wait on qt;
      report "qt UNEXPECTED";
    end loop;
  end process mon_qt;

  mon_qr : process
  begin
    wait on qr;
    if qr = 1 and now = 9 ns then
      report "qr=1 @9 ns";
    else
      report "qr UNEXPECTED";
    end if;
    wait on qr;
    if qr = 2 and now = 19 ns then
      report "qr=2 @19 ns";
    else
      report "qr UNEXPECTED";
    end if;
    loop
      wait on qr;
      report "qr UNEXPECTED";
    end loop;
  end process mon_qr;
end architecture tb;
