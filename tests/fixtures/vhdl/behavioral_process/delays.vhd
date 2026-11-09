-- SPDX-License-Identifier: Apache-2.0
--
-- VHDL signal assignment with delay in behavioral processes (IEEE 1076-2019
-- 10.5.2.2): the projected output waveform edit rules of 14.7.2 for
-- inertial delay (default pulse rejection limit, a stated limit below and
-- above the pulse), transport delay, multi-element waveforms and an
-- assignment that preempts later transactions. One driver process assigns at
-- 0, 2, 5 and 7 ns; one monitor per signal reports every event. The expected
-- events (delays.expected) are derived by hand from rules a), b) and 1) to 5)
-- of 10.5.2.2; the runner sorts the reports (their order within a delta cycle
-- is unspecified, 14.7.5.2).
library ieee;
use ieee.std_logic_1164.all;

entity delays is
end entity delays;

architecture tb of delays is
  signal a : integer := 0;
  signal a2 : integer := 0;
  signal b : integer := 0;
  signal c : integer := 0;
  signal d : integer := 0;
  signal d2 : integer := 0;
  signal e : integer := 0;
  signal e2 : integer := 0;
  signal e3 : integer := 0;
  signal f : integer := 0;
  signal g : integer := 0;
  signal j : integer := 0;
  signal k : integer := 0;
  signal v : std_logic_vector(3 downto 0) := "0000";
  signal m : std_logic := '0';
begin
  drv : process
  begin
    -- 0 ns
    -- a: default limit 10 ns; a <= 2 at 5 ns rejects the pulse of 1.
    a <= 1 after 10 ns;
    -- a2: the same value 1 is kept (step 3).
    a2 <= 1 after 10 ns;
    -- b: transport keeps both.
    b <= transport 1 after 10 ns;
    -- c: the transport assignment at 5 ns deletes the transaction at 20 ns.
    c <= transport 1 after 20 ns;
    -- d: limit 3 ns keeps the old transaction (10 < 15 - 3); d2: limit 8 ns
    -- does not (10 >= 15 - 8).
    d <= reject 3 ns inertial 1 after 10 ns;
    d2 <= reject 8 ns inertial 1 after 10 ns;
    e <= 1 after 5 ns, 2 after 10 ns, 3 after 15 ns;
    e2 <= transport 1 after 5 ns, 2 after 10 ns, 3 after 15 ns;
    e3 <= 1 after 5 ns, 2 after 10 ns;
    f <= 5;
    -- g: the second assignment at the same time deletes the first.
    g <= 1 after 10 ns;
    g <= 2 after 10 ns;
    -- j: the zero-delay assignment at 5 ns deletes the transaction at 10 ns.
    j <= 7 after 10 ns;
    -- k: matures at 10 ns, in the update phase before the reader resumes.
    k <= 4 after 10 ns;
    -- v: the slice assignment at 2 ns replaces the two low elements' pending
    -- transactions at 6 ns and leaves the two high ones.
    v <= "1010" after 6 ns;
    -- m: a std_logic waveform.
    m <= '1' after 4 ns, 'Z' after 8 ns;
    wait for 2 ns;
    -- 2 ns
    v(1 downto 0) <= "11" after 3 ns;
    wait for 3 ns;
    -- 5 ns
    j <= 3;
    a <= 2 after 10 ns;
    a2 <= 1 after 10 ns;
    b <= transport 2 after 10 ns;
    c <= transport 2 after 10 ns;
    d <= reject 3 ns inertial 2 after 10 ns;
    d2 <= reject 8 ns inertial 2 after 10 ns;
    wait for 2 ns;
    -- 7 ns: e2 keeps 2 at 10 ns and loses 3 at 15 ns; e3 (limit 4 ns) loses
    -- 2 at 10 ns (10 >= 11 - 4, a different value).
    e2 <= transport 9 after 4 ns;
    e3 <= 7 after 4 ns;
    wait;
  end process drv;

  -- The process that waits for 10 ns sees the value that matures at 10 ns.
  reader : process
  begin
    wait for 10 ns;
    if k = 4 and now = 10 ns then
      report "k seen 4 @10 ns";
    else
      report "k seen WRONG";
    end if;
    wait;
  end process reader;

  mon_a : process
  begin
    wait on a;
    if a = 2 and now = 15 ns then
      report "a=2 @15 ns";
    else
      report "a UNEXPECTED";
    end if;
    loop
      wait on a;
      report "a UNEXPECTED";
    end loop;
  end process mon_a;

  mon_a2 : process
  begin
    wait on a2;
    if a2 = 1 and now = 10 ns then
      report "a2=1 @10 ns";
    else
      report "a2 UNEXPECTED";
    end if;
    loop
      wait on a2;
      report "a2 UNEXPECTED";
    end loop;
  end process mon_a2;

  mon_b : process
  begin
    wait on b;
    if b = 1 and now = 10 ns then
      report "b=1 @10 ns";
    else
      report "b UNEXPECTED";
    end if;
    wait on b;
    if b = 2 and now = 15 ns then
      report "b=2 @15 ns";
    else
      report "b UNEXPECTED";
    end if;
    loop
      wait on b;
      report "b UNEXPECTED";
    end loop;
  end process mon_b;

  mon_c : process
  begin
    wait on c;
    if c = 2 and now = 15 ns then
      report "c=2 @15 ns";
    else
      report "c UNEXPECTED";
    end if;
    loop
      wait on c;
      report "c UNEXPECTED";
    end loop;
  end process mon_c;

  mon_d : process
  begin
    wait on d;
    if d = 1 and now = 10 ns then
      report "d=1 @10 ns";
    else
      report "d UNEXPECTED";
    end if;
    wait on d;
    if d = 2 and now = 15 ns then
      report "d=2 @15 ns";
    else
      report "d UNEXPECTED";
    end if;
    loop
      wait on d;
      report "d UNEXPECTED";
    end loop;
  end process mon_d;

  mon_d2 : process
  begin
    wait on d2;
    if d2 = 2 and now = 15 ns then
      report "d2=2 @15 ns";
    else
      report "d2 UNEXPECTED";
    end if;
    loop
      wait on d2;
      report "d2 UNEXPECTED";
    end loop;
  end process mon_d2;

  mon_e : process
  begin
    wait on e;
    if e = 1 and now = 5 ns then
      report "e=1 @5 ns";
    else
      report "e UNEXPECTED";
    end if;
    wait on e;
    if e = 2 and now = 10 ns then
      report "e=2 @10 ns";
    else
      report "e UNEXPECTED";
    end if;
    wait on e;
    if e = 3 and now = 15 ns then
      report "e=3 @15 ns";
    else
      report "e UNEXPECTED";
    end if;
    loop
      wait on e;
      report "e UNEXPECTED";
    end loop;
  end process mon_e;

  mon_e2 : process
  begin
    wait on e2;
    if e2 = 1 and now = 5 ns then
      report "e2=1 @5 ns";
    else
      report "e2 UNEXPECTED";
    end if;
    wait on e2;
    if e2 = 2 and now = 10 ns then
      report "e2=2 @10 ns";
    else
      report "e2 UNEXPECTED";
    end if;
    wait on e2;
    if e2 = 9 and now = 11 ns then
      report "e2=9 @11 ns";
    else
      report "e2 UNEXPECTED";
    end if;
    loop
      wait on e2;
      report "e2 UNEXPECTED";
    end loop;
  end process mon_e2;

  mon_e3 : process
  begin
    wait on e3;
    if e3 = 1 and now = 5 ns then
      report "e3=1 @5 ns";
    else
      report "e3 UNEXPECTED";
    end if;
    wait on e3;
    if e3 = 7 and now = 11 ns then
      report "e3=7 @11 ns";
    else
      report "e3 UNEXPECTED";
    end if;
    loop
      wait on e3;
      report "e3 UNEXPECTED";
    end loop;
  end process mon_e3;

  mon_f : process
  begin
    wait on f;
    if f = 5 and now = 0 ns then
      report "f=5 @0 ns";
    else
      report "f UNEXPECTED";
    end if;
    loop
      wait on f;
      report "f UNEXPECTED";
    end loop;
  end process mon_f;

  mon_g : process
  begin
    wait on g;
    if g = 2 and now = 10 ns then
      report "g=2 @10 ns";
    else
      report "g UNEXPECTED";
    end if;
    loop
      wait on g;
      report "g UNEXPECTED";
    end loop;
  end process mon_g;

  mon_j : process
  begin
    wait on j;
    if j = 3 and now = 5 ns then
      report "j=3 @5 ns";
    else
      report "j UNEXPECTED";
    end if;
    loop
      wait on j;
      report "j UNEXPECTED";
    end loop;
  end process mon_j;

  mon_k : process
  begin
    wait on k;
    if k = 4 and now = 10 ns then
      report "k=4 @10 ns";
    else
      report "k UNEXPECTED";
    end if;
    loop
      wait on k;
      report "k UNEXPECTED";
    end loop;
  end process mon_k;

  mon_v : process
  begin
    wait on v;
    if v = "0011" and now = 5 ns then
      report "v=3 @5 ns";
    else
      report "v UNEXPECTED";
    end if;
    wait on v;
    if v = "1011" and now = 6 ns then
      report "v=11 @6 ns";
    else
      report "v UNEXPECTED";
    end if;
    loop
      wait on v;
      report "v UNEXPECTED";
    end loop;
  end process mon_v;

  mon_m : process
  begin
    wait on m;
    if m = '1' and now = 4 ns then
      report "m=1 @4 ns";
    else
      report "m UNEXPECTED";
    end if;
    wait on m;
    if m = 'Z' and now = 8 ns then
      report "m=Z @8 ns";
    else
      report "m UNEXPECTED";
    end if;
    loop
      wait on m;
      report "m UNEXPECTED";
    end loop;
  end process mon_m;
end architecture tb;
