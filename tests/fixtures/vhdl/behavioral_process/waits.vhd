-- SPDX-License-Identifier: Apache-2.0
--
-- VHDL wait statements (IEEE 1076-2019 10.2) and zero-delay signal
-- assignments (10.5.2.2, update in the next delta cycle, 14.7.5.3) of
-- behavioral processes: wait for (also a computed time and 0 ns), wait on,
-- wait until, and every combination with a timeout, including a condition
-- re-armed under one deadline and a sensitivity list that leaves out a signal
-- of the condition. Each process checks the time it resumes at (and the
-- values it sees) and reports a constant text, so that every process is a
-- compiled kernel member (no string operations); the order of
-- reports within a delta cycle is unspecified (14.7.5.2), so the runner sorts
-- them. The expected lines are derived from the LRM by hand (waits.expected).
library ieee;
use ieee.std_logic_1164.all;

entity waits is
end entity waits;

architecture tb of waits is
  signal clk  : std_logic := '0';
  signal go   : std_logic := '0';
  signal a, b : integer := 0;
  signal hits : integer := 0;
  signal done : boolean := false;
begin
  -- a: 1 at 10 ns, 2 at 15 ns, 3 at 35 ns; b: 1 at 15 ns, 2 one delta later
  -- (wait for 0 ns resumes in the next delta cycle); go: '1' at 25 ns.
  drv : process
  begin
    wait for 10 ns;
    a <= 1;
    wait for 5 ns;
    a <= 2;
    b <= 1;
    wait for 0 ns;
    b <= 2;
    wait for 10 ns;
    go <= '1';
    wait for 10 ns;
    a <= 3;
    wait;
  end process drv;

  -- clk rises at 5, 15, 25 and 35 ns.
  clkgen : process
  begin
    while not done loop
      wait for 5 ns;
      clk <= not clk;
    end loop;
    wait;
  end process clkgen;

  cnt : process
    variable n : integer := 0;
  begin
    wait until clk = '1';
    n := n + 1;
    hits <= n;
    if n = 4 then
      if now = 35 ns then
        report "CLK4 @35 ns";
      else
        report "CLK4 WRONG";
      end if;
      done <= true;
      wait;
    end if;
  end process cnt;

  fin : process
  begin
    wait until done;
    if hits = 4 and now = 35 ns then
      report "DONE hits=4 @35 ns";
    else
      report "DONE WRONG";
    end if;
    wait;
  end process fin;

  -- wait for: 7 and 14 ns.
  wfor : process
  begin
    wait for 7 ns;
    if now = 7 ns then
      report "F1 @7 ns";
    else
      report "F1 WRONG";
    end if;
    wait for 7 ns;
    if now = 14 ns then
      report "F2 @14 ns";
    else
      report "F2 WRONG";
    end if;
    wait;
  end process wfor;

  -- wait for a computed time: 6 ns, 10 ns, then a timeout that go (25 ns)
  -- ends before its 40 ns.
  wdyn : process
    variable d : time := 2 ns;
  begin
    wait for d * 3;
    if now = 6 ns then
      report "DYN1 @6 ns";
    else
      report "DYN1 WRONG";
    end if;
    d := 4 ns;
    wait for d;
    if now = 10 ns then
      report "DYN2 @10 ns";
    else
      report "DYN2 WRONG";
    end if;
    wait on go for d * 10;
    if now = 25 ns then
      report "DYN3 @25 ns";
    else
      report "DYN3 WRONG";
    end if;
    wait;
  end process wdyn;

  -- wait on: every event of a.
  won : process
  begin
    wait on a;
    if a = 1 and now = 10 ns then
      report "ON1 a=1 @10 ns";
    else
      report "ON1 WRONG";
    end if;
    wait on a;
    if a = 2 and now = 15 ns then
      report "ON2 a=2 @15 ns";
    else
      report "ON2 WRONG";
    end if;
    wait on a;
    if a = 3 and now = 35 ns then
      report "ON3 a=3 @35 ns";
    else
      report "ON3 WRONG";
    end if;
    wait;
  end process won;

  -- wait until: the condition is evaluated on events of its signals only.
  wuntil : process
  begin
    wait until go = '1';
    if now = 25 ns then
      report "UG @25 ns";
    else
      report "UG WRONG";
    end if;
    wait;
  end process wuntil;

  wuntil2 : process
  begin
    wait until a >= 2;
    if a = 2 and now = 15 ns then
      report "UA2 a=2 @15 ns";
    else
      report "UA2 WRONG";
    end if;
    wait;
  end process wuntil2;

  -- wait on ... for: timeout, then an event before the timeout, then an
  -- event in the same time step one delta later; wait until ... for: the
  -- timeout, then the event.
  wtime : process
  begin
    wait on b for 3 ns;
    if now = 3 ns then
      report "T1 @3 ns";
    else
      report "T1 WRONG";
    end if;
    wait on b for 20 ns;
    if b = 1 and now = 15 ns then
      report "T2 b=1 @15 ns";
    else
      report "T2 WRONG";
    end if;
    wait on b for 1 ns;
    if b = 2 and now = 15 ns then
      report "T3 b=2 @15 ns";
    else
      report "T3 WRONG";
    end if;
    wait until a = 3 for 5 ns;
    if a /= 3 and now = 20 ns then
      report "TU1 timeout @20 ns";
    else
      report "TU1 WRONG";
    end if;
    wait until a = 3 for 100 ns;
    if a = 3 and now = 35 ns then
      report "TU2 event @35 ns";
    else
      report "TU2 WRONG";
    end if;
    wait;
  end process wtime;

  -- The deadline of the first wait stands while the condition is false at
  -- events at 10 and 15 ns: the process resumes at 30 ns, not after the
  -- event at 35 ns.
  wdead : process
  begin
    wait until a = 99 for 30 ns;
    if now = 30 ns then
      report "DL @30 ns";
    else
      report "DL WRONG";
    end if;
    wait;
  end process wdead;

  -- go is not in the sensitivity list: the condition is evaluated at the
  -- events of a and b only, the first time go is '1' at 35 ns.
  wlist : process
  begin
    wait on a, b until go = '1' for 40 ns;
    if now = 35 ns then
      report "WO @35 ns";
    else
      report "WO WRONG";
    end if;
    wait;
  end process wlist;
end architecture tb;
