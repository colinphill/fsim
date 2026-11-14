-- SPDX-License-Identifier: Apache-2.0
-- Two call sites of one recursive procedure in a process (IEEE 1076-2019
-- 14.6 b, 19.4): each activation elaborates its own declarations, so the
-- variables a call object reads are those of the frame of its own site, and
-- the recursive call reads the frames the recursion makes. The last call, with
-- a variable actual, runs the frame the recursive call runs, so its activations
-- share one set of registers: each reads its own activation, and a deposit
-- reaches the outer activation (22.5.2).
entity top is
end entity;

architecture rtl of top is
begin
  main : process is
    variable total : integer := 0;
    variable k : integer := 4;
    procedure down(n : integer) is
      variable depth : integer := n * 10;
    begin
      depth := depth + 1;
      if n > 1 then
        down(n - 1);
      end if;
      total := total + depth;
    end procedure;
  begin
    down(3);
    wait for 1 ns;
    down(2);
    wait for 1 ns;
    down(k);
    wait for 1 ns;
    wait;
  end process;
end architecture;
