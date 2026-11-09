-- SPDX-License-Identifier: Apache-2.0
-- The formals and locals of a running function call, and the parameter of a
-- `for` loop of a running procedure, are elaborated for the call (IEEE
-- 1076-2019 14.6 a, b) and their values are readable while it runs
-- (22.3, 19.10).
entity top is
end entity;

architecture t of top is
begin
  main : process is
    function f(a : integer; b : integer) return integer is
      variable t : integer := a * 10;
    begin
      t := t + b;
      return t;
    end function;
    procedure p(n : integer) is
      variable s : integer := 0;
    begin
      for i in 1 to n loop
        s := s + i;
      end loop;
    end procedure;
    -- f is called from a procedure with a formal, so the call is not folded
    -- when the design is built and the function body runs.
    procedure q(n : integer) is
      variable r : integer;
    begin
      r := f(n, 4);
    end procedure;
  begin
    q(3);
    p(3);
    wait;
  end process;
end architecture;
