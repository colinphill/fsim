-- SPDX-License-Identifier: Apache-2.0
--
-- Signals no process drives keep their initial value for the whole run
-- (IEEE 1076-2019 14.7.3.1, 14.7.5.2): the kernel holds them, as it holds
-- driven signals, and the processes that read them read that value.
--   i = 42, b = TRUE (reported as boolean'pos(b) = 1), v = "10XZ", r = 2.5 (reported as integer(r * 10.0) = 25)
--   and t = 7 ns (reported as t / 1 ns), each read at 1 ns, after a process that waits on i (which
--   never has an event) has stayed suspended:
--     wide a i=42 b=1 v=10XZ r=25 t=7
--   The process on o, a signal driven at 2 ns, sees its event once, and
--   reads the undriven signals again:
--     wide b o=1 i=42 v=10XZ
library ieee;
use ieee.std_logic_1164.all;

entity top is
end entity;

architecture sim of top is
  signal i : integer := 42;
  signal b : boolean := true;
  signal v : std_logic_vector(3 downto 0) := "10XZ";
  signal r : real := 2.5;
  signal t : time := 7 ns;
  signal o : integer := 0;
begin
  o <= 1 after 2 ns;

  on_i : process (i)
  begin
    if now > 0 ns then
      report "wide never fires";
    end if;
  end process;

  on_o : process (o)
  begin
    if now > 0 ns then
      report "wide b o=" & integer'image(o) & " i=" & integer'image(i)
        & " v=" & to_string(v);
    end if;
  end process;

  check : process
  begin
    wait for 1 ns;
    report "wide a i=" & integer'image(i) & " b=" & integer'image(boolean'pos(b))
      & " v=" & to_string(v) & " r=" & integer'image(integer(r * 10.0))
      & " t=" & integer'image(t / 1 ns);
    wait;
  end process;
end architecture;
