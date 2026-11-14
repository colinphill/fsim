-- SPDX-License-Identifier: Apache-2.0
-- Six call sites of two procedures in one process, interleaved (IEEE 1076-2019
-- 14.6 b, 19.4): the sites with variable actuals run one frame, the sites with
-- a literal actual each their own, and a call object reads the formals and
-- variables of the frame its own site runs, not those of another site's.
entity top is
end entity;

architecture rtl of top is
begin
  main : process is
    variable a : integer := 3;
    variable b : integer := 5;
    variable c : integer := 8;
    variable total : integer := 0;
    procedure p(x : integer) is
      variable v : integer := x * 2;
    begin
      v := v + 1;
      total := total + v;
    end procedure;
    procedure q(y : integer) is
      variable w : integer := y + 100;
    begin
      w := w + 1;
      total := total + w;
    end procedure;
  begin
    p(a);
    q(b);
    p(3);
    p(b);
    q(1);
    p(c);
    wait;
  end process;
end architecture;
