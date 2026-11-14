-- SPDX-License-Identifier: Apache-2.0
-- 'EVENT, 'ACTIVE and resumption around deposits made near the first
-- simulation cycle (IEEE 1076-2019 14.7.5.2, 14.7.5.3, 16.2.4, 22.5.2); see
-- the library for where each deposit is made.
--   ps, ds: deposited before step c) of initialization, which sets their
--   values from their (absent) drivers and drops the scheduled deposit: '0',
--   never an event.
--   p: deposited with vhpiDepositPropagate after initialization: '0' during
--   initialization, updated in the first simulation cycle (an event, active),
--   which resumes watch_p; first_cycle resumes from its zero timeout in the
--   same cycle.
--   d: deposited with vhpiDeposit after initialization: '1' from then on,
--   never an event or active, so watch_d does not resume.
--   q: deposited with vhpiDeposit at 5 ns, before at5 resumes there.
-- The library prints the recorded integers at 6 ns.
library ieee;
use ieee.std_logic_1164.all;

entity top is
end entity;

architecture rtl of top is
  signal p : std_logic := '0';
  signal d : std_logic := '0';
  signal p_init_event, p_init_value, p_wakes : integer := -1;
  signal p_wake_event, p_wake_active : integer := -1;
  signal d_init_event, d_init_value, d_wakes : integer := -1;
  signal first_p_event, first_p_active : integer := -1;
  signal first_d_event, first_d_active, first_d_value : integer := -1;
  signal ps : std_logic := '0';
  signal ds : std_logic := '0';
  signal ps_init_value, ps_wakes, ds_init_value, ds_wakes : integer := -1;
  signal q : std_logic := '0';
  signal q_event, q_active, q_value, q_wakes : integer := -1;

  function flag(b : boolean) return integer is
  begin
    if b then
      return 1;
    end if;
    return 0;
  end function;

  function level(v : std_logic) return integer is
  begin
    if v = '1' then
      return 1;
    end if;
    return 0;
  end function;
begin
  watch_p: process
    variable wakes : natural := 0;
  begin
    p_init_event <= flag(p'event);
    p_init_value <= level(p);
    p_wakes <= wakes;
    loop
      wait on p;
      wakes := wakes + 1;
      p_wakes <= wakes;
      p_wake_event <= flag(p'event);
      p_wake_active <= flag(p'active);
    end loop;
  end process;

  watch_d: process
    variable wakes : natural := 0;
  begin
    d_init_event <= flag(d'event);
    d_init_value <= level(d);
    d_wakes <= wakes;
    loop
      wait on d;
      wakes := wakes + 1;
      d_wakes <= wakes;
    end loop;
  end process;

  watch_ps: process
    variable wakes : natural := 0;
  begin
    ps_init_value <= level(ps);
    ps_wakes <= wakes;
    loop
      wait on ps;
      wakes := wakes + 1;
      ps_wakes <= wakes;
    end loop;
  end process;

  watch_ds: process
    variable wakes : natural := 0;
  begin
    ds_init_value <= level(ds);
    ds_wakes <= wakes;
    loop
      wait on ds;
      wakes := wakes + 1;
      ds_wakes <= wakes;
    end loop;
  end process;

  first_cycle: process
  begin
    wait for 0 ns;
    first_p_event <= flag(p'event);
    first_p_active <= flag(p'active);
    first_d_event <= flag(d'event);
    first_d_active <= flag(d'active);
    first_d_value <= level(d);
    wait;
  end process;

  watch_q: process
    variable wakes : natural := 0;
  begin
    q_wakes <= wakes;
    loop
      wait on q;
      wakes := wakes + 1;
      q_wakes <= wakes;
    end loop;
  end process;

  at5: process
  begin
    wait for 5 ns;
    q_event <= flag(q'event);
    q_active <= flag(q'active);
    q_value <= level(q);
    wait;
  end process;
end architecture;
