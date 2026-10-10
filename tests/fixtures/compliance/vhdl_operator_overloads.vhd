-- fsim: top=vhdl_operator_overloads(sim)
-- IEEE 1076-2008 9.2.4-9.2.8: predefined adding, multiplying, remainder and
-- exponentiation operators on numeric operands, and visible overloads of
-- those operators for other types (these must not be rejected at analysis).
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

package operator_overloads_pkg is
  type color is (red, green, blue);
  type pair is record
    a, b : integer;
  end record;
  function "+" (l, r : color) return color;
  function "+" (l, r : pair) return pair;
  function "-" (r : pair) return pair;
end package;

package body operator_overloads_pkg is
  function "+" (l, r : color) return color is
  begin
    if l = r then
      return l;
    elsif l = red or r = red then
      return blue;
    else
      return red;
    end if;
  end function;

  function "+" (l, r : pair) return pair is
  begin
    return (l.a + r.a, l.b + r.b);
  end function;

  function "-" (r : pair) return pair is
  begin
    return (-r.a, -r.b);
  end function;
end package body;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.operator_overloads_pkg.all;

entity vhdl_operator_overloads is
end entity;

architecture sim of vhdl_operator_overloads is
  type small is range 0 to 100;
  type ratio is range -1.0 to 1.0;
  constant scaled : real := 2.5 * 2;

  function "-" (l, r : character) return integer is
  begin
    return character'pos(l) - character'pos(r);
  end function;
begin
  process
    variable c : color := green;
    variable p : pair := (1, 2);
    variable k : integer;
    variable s : small := 17;
    variable q : ratio := 0.5;
    variable t : time := 10 ns;
    variable u : unsigned(7 downto 0) := to_unsigned(5, 8);
    variable failures : natural := 0;
  begin
    c := c + blue;
    if c /= red then failures := failures + 1; end if;
    p := p + p;
    if p.a /= 2 or p.b /= 4 then failures := failures + 1; end if;
    p := -p;
    if p.a /= -2 then failures := failures + 1; end if;
    k := 'c' - 'a';
    if k /= 2 then failures := failures + 1; end if;
    s := s mod 5 + s rem 3;
    if s /= 4 then failures := failures + 1; end if;
    q := q * q;
    if q /= 0.25 then failures := failures + 1; end if;
    if scaled /= 5.0 then failures := failures + 1; end if;
    t := t * 3 + 2.0 * t / 4;
    if t /= 35 ns then failures := failures + 1; end if;
    k := t / 5 ns;
    if k /= 7 then failures := failures + 1; end if;
    t := abs (-t);
    if t /= 35 ns then failures := failures + 1; end if;
    k := 2 ** 10 + (-7) mod 3;
    if k /= 1026 then failures := failures + 1; end if;
    if 2.0 ** 3 /= 8.0 then failures := failures + 1; end if;
    u := u + 1;
    if to_integer(u) /= 6 then failures := failures + 1; end if;
    if failures = 0 then
      report "PASS";
    else
      report "FAIL " & integer'image(failures) severity error;
    end if;
    wait;
  end process;
end architecture;
