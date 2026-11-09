-- SPDX-License-Identifier: Apache-2.0
--
-- Recursive subprograms with formals and locals, ten thousand calls deep,
-- in processes the v4 kernel compiles (IEEE 1076-2019 4.3, 4.2.2: each call
-- has its own formals and locals, however deep the recursion; 10.2: a wait
-- inside a procedure keeps the process's whole call stack, frames included).
--   sum(n) = n + sum(n - 1), sum(0) = 0 (a function with a formal):
--          sum(10000) = 10000 * 10001 / 2 = 50005000.
--   walk(n, acc): twice := 2 n (a local), the nested call walk(n - 1, acc),
--          then acc := acc + twice: the local of each call must survive the
--          nested calls, so acc = 2 * 50005000 = 100010000.
--   count(n, acc): mine := n (a local), wait for 1 ps, the nested call
--          count(n - 1, acc), then acc := acc + mine, so acc = 50005000 and
--          the last return is at 10 ns.
-- The reports are at 20 ns.
entity top is
end entity;

architecture sim of top is
  signal go : bit := '0';
  signal total : integer := 0;
  signal walked : integer := 0;
  signal counted : integer := 0;

  function sum(n : natural) return natural is
  begin
    if n = 0 then
      return 0;
    end if;
    return n + sum(n - 1);
  end function;

  procedure walk(n : natural; acc : inout integer) is
    variable twice : integer := 0;
  begin
    if n > 0 then
      twice := 2 * n;
      walk(n - 1, acc);
      acc := acc + twice;
    end if;
  end procedure;

  procedure count(n : natural; acc : inout integer) is
    variable mine : integer := 0;
  begin
    if n > 0 then
      mine := n;
      wait for 1 ps;
      count(n - 1, acc);
      acc := acc + mine;
    end if;
  end procedure;
begin
  go <= '1' after 1 ns;

  summer : process (go)
  begin
    if go = '1' then
      total <= sum(10000);
    end if;
  end process;

  walker : process (go)
    variable acc : integer;
  begin
    if go = '1' then
      acc := 0;
      walk(10000, acc);
      walked <= acc;
    end if;
  end process;

  counter : process
    variable acc : integer := 0;
  begin
    count(10000, acc);
    counted <= acc;
    wait;
  end process;

  reporter : process
  begin
    wait for 20 ns;
    report "wide sum=" & integer'image(total) & ";";
    report "wide walk=" & integer'image(walked) & ";";
    report "wide count=" & integer'image(counted) & ";";
    wait;
  end process;
end architecture;
