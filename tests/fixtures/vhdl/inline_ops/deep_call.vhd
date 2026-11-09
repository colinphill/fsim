-- SPDX-License-Identifier: Apache-2.0
--
-- Procedure calls nested ten thousand deep in processes that the v4 kernel
-- compiles (IEEE 1076-2019 4.3: a subprogram may call itself; the depth is
-- limited only by the machine). The procedures have no formals or locals,
-- so a nested call needs no saved frame: the call stack's return targets
-- are the whole state. They work on the process's variables.
--   add: while n > 0, acc := acc + n, n := n - 1 and add again (recursion
--        ten thousand deep), so acc = 10000 * 10001 / 2 = 50005000.
--   tick: waits 1 ps at each of the ten thousand levels (10.2: the whole
--        call stack of the process survives each wait), counting them in
--        ticks = 10000 at 10 ns.
-- The reports are at 20 ns.
entity top is
end entity;

architecture sim of top is
  signal go : bit := '0';
  signal total : integer := 0;
  signal waited : integer := 0;
begin
  go <= '1' after 1 ns;

  summer : process (go)
    variable n : integer;
    variable acc : integer;

    procedure add is
    begin
      if n > 0 then
        acc := acc + n;
        n := n - 1;
        add;
      end if;
    end procedure;
  begin
    if go = '1' then
      n := 10000;
      acc := 0;
      add;
      total <= acc;
    end if;
  end process;

  waiter : process
    variable n : integer := 10000;
    variable ticks : integer := 0;

    procedure tick is
    begin
      if n > 0 then
        wait for 1 ps;
        ticks := ticks + 1;
        n := n - 1;
        tick;
      end if;
    end procedure;
  begin
    tick;
    waited <= ticks;
    wait;
  end process;

  reporter : process
  begin
    wait for 20 ns;
    report "wide sum=" & integer'image(total) & ";";
    report "wide waited=" & integer'image(waited) & ";";
    wait;
  end process;
end architecture;
