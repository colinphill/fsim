-- SPDX-License-Identifier: Apache-2.0
--
-- Every way a kernel signal changes or is assigned keeps its history
-- (IEEE 1076-2019 16.2.4, 14.7.3.1): a port formal wired to its actual, a
-- transport delay line (the projected waveform of a driver matures at its
-- own time), a waveform with several elements, a resolved signal and an
-- element of a vector.
--   x (2 bits, "00"): "01" at 5 ns, "01" again at 10 ns (a transaction
--     without an event), "11" at 15 ns. The formal p of u is x itself: its
--     process resumes in the cycle of each event, seeing event and active
--     true, the value before and 0 ns since the event. At 12 ns the formal
--     is 7 ns past its event and 2 ns past its transaction: it is active
--     when the actual is, though the value stays (14.7.3.3).
--   d <= transport x after 3 ns: "01" at 8 ns and "11" at 18 ns (the
--     assignment runs on the events of x, not on its transaction at 10 ns),
--     each a transaction and an event.
--   w, assigned "01" after 2 ns, "11" after 4 ns.
--   r (std_logic, 'Z'), driven by r1 ('0' at 0, 'Z' at 7 ns, '1' at 14 ns)
--     and r2 ('Z', '0' at 14 ns): '0' at 0, 'Z' at 7 ns, 'X' at 14 ns.
--   v (4 bits, "0000"): v(1) set at 20 ns, then the whole vector assigned
--     "0010" at 25 ns: v(1)'s last_active is 25 and its last_event 20, while
--     v(0) never changes and is active only at 25 ns.
--   rec (a record, clk '0'): data "0001" at 30 ns, clk '1' at 31 ns, the
--     whole record assigned the same value at 32 ns (a transaction on every
--     element), clk '0' at 33 ns and '1' at 34 ns. RISING_EDGE(rec.clk) is
--     true at 31 and 34 ns: an event on the element, to '1' from '0'.
library ieee;
use ieee.std_logic_1164.all;

entity child is
  port (p : in std_logic_vector(1 downto 0));
end entity;

architecture rtl of child is
begin
  watch: process
  begin
    wait on p;
    report "wide child at " & time'image(now) & " p=" & to_string(p)
      & " event=" & boolean'image(p'event) & " active=" & boolean'image(p'active)
      & " last_value=" & to_string(p'last_value)
      & " last_event=" & time'image(p'last_event);
  end process;

  late: process
  begin
    wait for 12 ns;
    report "wide child late at " & time'image(now)
      & " last_event=" & time'image(p'last_event)
      & " last_active=" & time'image(p'last_active)
      & " last_value=" & to_string(p'last_value);
    wait;
  end process;
end architecture;

library ieee;
use ieee.std_logic_1164.all;

entity top is
end entity;

architecture rtl of top is
  type rec_t is record
    clk : std_logic;
    data : std_logic_vector(3 downto 0);
  end record;
  signal rec : rec_t := ('0', "0000");
  signal x : std_logic_vector(1 downto 0) := "00";
  signal d : std_logic_vector(1 downto 0) := "00";
  signal w : std_logic_vector(1 downto 0) := "00";
  signal r : std_logic := 'Z';
  signal v : std_logic_vector(3 downto 0) := "0000";
begin
  u: entity work.child port map (p => x);

  drive: process
  begin
    wait for 5 ns;
    x <= "01";
    wait for 5 ns;
    x <= "01";
    wait for 5 ns;
    x <= "11";
    wait;
  end process;

  d <= transport x after 3 ns;

  wave: process
  begin
    w <= "01" after 2 ns, "11" after 4 ns;
    wait;
  end process;

  r1: process
  begin
    r <= '0';
    wait for 7 ns;
    r <= 'Z';
    wait for 7 ns;
    r <= '1';
    wait;
  end process;

  r2: process
  begin
    r <= 'Z';
    wait for 14 ns;
    r <= '0';
    wait;
  end process;

  vdrive: process
  begin
    wait for 20 ns;
    v(1) <= '1';
    wait for 5 ns;
    v <= "0010";
    wait;
  end process;

  rec_drive: process
  begin
    wait for 30 ns;
    rec.data <= "0001";
    wait for 1 ns;
    rec.clk <= '1';
    wait for 1 ns;
    rec <= ('1', "0001");
    wait for 1 ns;
    rec.clk <= '0';
    wait for 1 ns;
    rec.clk <= '1';
    wait;
  end process;

  rec_watch: process
  begin
    wait on rec;
    if rising_edge(rec.clk) then
      report "wide rec rise at " & time'image(now);
    end if;
  end process;

  probe: process
  begin
    wait for 1 ns;
    report "wide q1 at " & time'image(now) & " r=" & std_logic'image(r)
      & " last_event=" & time'image(r'last_event) & " last_active=" & time'image(r'last_active)
      & " last_value=" & std_logic'image(r'last_value);
    wait for 2 ns;
    report "wide q3 at " & time'image(now) & " w=" & to_string(w)
      & " last_event=" & time'image(w'last_event) & " last_active=" & time'image(w'last_active)
      & " last_value=" & to_string(w'last_value);
    wait for 2 ns;
    report "wide q5 at " & time'image(now) & " w=" & to_string(w)
      & " last_event=" & time'image(w'last_event) & " last_active=" & time'image(w'last_active)
      & " last_value=" & to_string(w'last_value);
    wait for 3 ns;
    report "wide q8 at " & time'image(now) & " r=" & std_logic'image(r)
      & " last_event=" & time'image(r'last_event) & " last_value=" & std_logic'image(r'last_value);
    wait for 2 ns;
    report "wide q10 at " & time'image(now) & " d=" & to_string(d)
      & " last_event=" & time'image(d'last_event) & " last_active=" & time'image(d'last_active)
      & " last_value=" & to_string(d'last_value);
    wait for 4 ns;
    report "wide q14 at " & time'image(now) & " d=" & to_string(d)
      & " last_event=" & time'image(d'last_event) & " last_active=" & time'image(d'last_active)
      & " last_value=" & to_string(d'last_value);
    wait for 2 ns;
    report "wide q16 at " & time'image(now) & " r=" & std_logic'image(r)
      & " last_event=" & time'image(r'last_event) & " last_value=" & std_logic'image(r'last_value);
    wait for 4 ns;
    report "wide q20 at " & time'image(now) & " d=" & to_string(d)
      & " last_event=" & time'image(d'last_event) & " last_active=" & time'image(d'last_active)
      & " last_value=" & to_string(d'last_value);
    wait for 7 ns;
    report "wide q27 at " & time'image(now) & " v=" & to_string(v)
      & " v(1) last_event=" & time'image(v(1)'last_event) & " last_active=" & time'image(v(1)'last_active)
      & " v(0) last_event=" & time'image(v(0)'last_event) & " last_active=" & time'image(v(0)'last_active);
    wait;
  end process;
end architecture;
