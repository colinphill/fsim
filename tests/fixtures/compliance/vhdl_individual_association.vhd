-- fsim: top=individual_association(sim) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2008 6.5.7.1: subelements of a record or array formal of mode
-- in, out or inout associated individually; out actuals that are a record
-- element or an element of a two-dimensional array; 8.3: expanded names
-- whose prefix is an enclosing process, loop or the architecture.
entity individual_association is
end entity;

architecture sim of individual_association is
  type point is record
    x : integer;
    y : integer;
  end record;
  type matrix is array (1 to 2, 1 to 2) of integer;
  type triple is array (0 to 2) of integer;
  signal level : integer := 5;
begin
  check : process
    procedure swap(p : inout point) is
      variable t : integer;
    begin
      t := p.x;
      p.x := p.y;
      p.y := t;
    end procedure;
    procedure fill(m : out matrix) is
    begin
      m := ((1, 2), (3, 4));
    end procedure;
    procedure total(r : in triple; s : out integer) is
    begin
      s := r(0) + r(1) + r(2);
    end procedure;
    procedure set(v : out integer) is
    begin
      v := 9;
    end procedure;
    variable a, b : integer := 0;
    variable q : point := (1, 2);
    variable m : matrix := ((0, 0), (0, 0));
    variable failures : natural := 0;
  begin
    a := 10;
    b := 20;
    swap(p.x => a, p.y => b);
    if a /= 20 or b /= 10 then
      failures := failures + 1;
    end if;
    fill(m(1, 1) => a, m(1, 2) => b, m(2, 1) => q.x, m(2, 2) => q.y);
    if a /= 1 or b /= 2 or q /= (3, 4) then
      failures := failures + 1;
    end if;
    total(r(0) => 1, r(1) => a, r(2) => 5, s => b);
    if b /= 7 then
      failures := failures + 1;
    end if;
    set(q.y);
    set(m(2, 1));
    if q /= (3, 9) or m /= ((0, 0), (9, 0)) then
      failures := failures + 1;
    end if;
    outer : for i in 1 to 2 loop
      check.a := outer.i;
    end loop outer;
    if check.a /= 2 or sim.level /= 5 then
      failures := failures + 1;
    end if;
    if failures = 0 then
      report "PASS";
    else
      report "FAIL count " & integer'image(failures) severity error;
    end if;
    wait;
  end process;
end architecture;
