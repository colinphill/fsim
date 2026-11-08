-- SPDX-License-Identifier: Apache-2.0
-- The full name of a declaration a running call elaborated (IEEE 1076-2019
-- 14.6 b, 19.4.3): the full name of the process that started the chain of
-- calls, then the name of every call of the chain, then the declaration:
-- `:top:main:outer:inner:v` for the variable v of inner, which outer called,
-- which the process main called.
entity top is
end entity;

architecture rtl of top is
begin
  main : process is
    procedure inner(k : integer) is
      variable v : integer := k;
    begin
      v := v + 1;
    end procedure;
    procedure outer(n : integer) is
      variable u : integer := n;
    begin
      inner(n + 1);
      u := u + 1;
    end procedure;
  begin
    outer(1);
    wait;
  end process;
end architecture;
