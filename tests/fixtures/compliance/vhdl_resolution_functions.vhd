-- fsim: top=resolution_functions(sim) std=1993
-- IEEE 1076-1993 2.4 and 12.6.1: a resolution function computes a resolved
-- signal's value from the array of its drivers' values, indexed from the
-- low bound of the formal's index subtype; the initial value is the
-- resolution of the drivers' initial values. 4.3.1.2: objects without an
-- initial value start at their subtype's leftmost value (REAL'LEFT,
-- NATURAL'LEFT in records). 3.1.3: physical literals of a user type.
entity resolution_functions is
end entity;

architecture sim of resolution_functions is
  type int_vec is array (natural range <>) of integer;
  function sum(v : int_vec) return integer is
    variable s : integer := 0;
  begin
    for i in v'range loop
      s := s + v(i);
    end loop;
    return s;
  end function;
  subtype summed is sum integer;

  type flag_vec is array (positive range <>) of bit;
  function any_set(v : flag_vec) return bit is
  begin
    for i in v'range loop
      if v(i) = '1' then
        return '1';
      end if;
    end loop;
    return '0';
  end function;
  subtype wired is any_set bit;

  type pair is record
    x, y : natural;
  end record;
  type pair_vec is array (natural range <>) of pair;
  function first(v : pair_vec) return pair is
  begin
    return v(v'low);
  end function;
  subtype first_pair is first pair;

  type current is range -2147483647 to 2147483647
    units
      na;
      ua = 1000 na;
      ma = 1000 ua;
    end units;
  constant one_ma : current := 1 ma;
  constant letter : character := 's';

  signal total : summed := 0;
  signal flag : wired;
  signal p : first_pair;
  signal r : real;
  signal c : current := one_ma;
  signal ch : character := letter;
begin
  total <= 3;
  flag <= '0';
  p <= (1, 2);

  process
    variable failures : natural := 0;
    variable v : real;
    variable q : pair;
  begin
    if p.x /= 0 or p.y /= 0 or v /= real'left or r /= real'left
      or q.x /= 0 then
      failures := failures + 1;
    end if;
    if c /= 1000000 na or ch /= 's' then
      failures := failures + 1;
    end if;
    total <= 4;
    flag <= '1';
    wait for 1 ns;
    if total /= 7 or flag /= '1' or p.x /= 1 or p.y /= 2 then
      failures := failures + 1;
    end if;
    total <= 10;
    flag <= '0';
    wait for 1 ns;
    if total /= 13 or flag /= '0' then
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
