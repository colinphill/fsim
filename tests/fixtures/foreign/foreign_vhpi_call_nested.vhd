-- SPDX-License-Identifier: Apache-2.0
-- Calls inside the body of another subprogram (IEEE 1076-2019 14.6 b, 19.4):
-- the procedure outer calls inner three times, twice with a variable and once
-- with a literal. Each call of inner elaborates its own declarations, and the
-- call objects in the body of outer read the frames they run, below the frame
-- of the call of outer.
entity top is
end entity;

architecture rtl of top is
begin
  main : process is
    variable a : integer := 4;
    variable total : integer := 0;
    procedure inner(n : integer) is
      variable w : integer := n + 100;
    begin
      w := w + 1;
      total := total + w;
    end procedure;
    procedure outer(m : integer) is
      variable u : integer := m * 10;
    begin
      inner(u);
      inner(7);
      u := u + 1;
      inner(u);
    end procedure;
  begin
    outer(a);
    wait;
  end process;
end architecture;
