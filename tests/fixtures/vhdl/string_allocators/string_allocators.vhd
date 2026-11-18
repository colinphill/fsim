-- SPDX-License-Identifier: Apache-2.0
-- STRING allocators whose index range is known only at run time, and STRING
-- aggregates with range choices or CHARACTER variables (IEEE 1076-2019
-- 9.3.3.3, 9.3.5, 9.3.7): `new string(1 to n)` holds n CHARACTER'LEFT
-- elements, `new string'(1 to n => c)` n copies of c, a null range none,
-- for an access-to-STRING type and for STD.TEXTIO.LINE alike.
library std;
use std.textio.all;

entity string_allocators is
end entity;

architecture test of string_allocators is
begin
  allocators : process
    type string_ptr is access string;
    variable q : string_ptr;
    variable l : line;
    variable n : integer := 4;
    variable z : integer := 0;
    variable c : character := 'k';
  begin
    q := new string(1 to n);
    report "a1 " & integer'image(q'length) & " " & integer'image(character'pos(q(1)));
    q := new string(n downto 2);
    report "a2 " & integer'image(q'length);
    q := new string(1 to z);
    report "a3 " & integer'image(q'length);
    q := new string(n to 1);
    report "a4 " & integer'image(q'length);
    q := new string'(1 to n => 'q');
    report "a5 " & q.all;
    q := new string'(1 to 3 => c);
    report "a6 " & q.all;
    q := new string'(2 => 'b', 3 => 'c');
    report "a7 " & q.all;
    deallocate(q);
    l := new string(1 to n + 1);
    report "l1 " & integer'image(l'length);
    l := new string'(1 to n => 'w');
    report "l2 " & l.all;
    l := new string'(1 to 2 => 'v');
    report "l3 " & l.all;
    deallocate(l);
    wait;
  end process;

  aggregates : process
    variable n : integer := 3;
    variable c : character := 'k';
    variable s : string(1 to 3);
  begin
    report "g1 " & string'(1 to 3 => 'z');
    report "g2 " & string'('a', 'b', c);
    report "g3 " & string'(1 to n => c);
    report "g4 " & string'(2 => 'b', 3 => 'c', 4 => c);
    s := string'('x', c, 'y');
    report "g5 " & s;
    report string'('a', 'b');
    wait;
  end process;
end architecture;

-- A STRING is indexed by POSITIVE (16.3): a non-null allocator range with
-- a bound below 1 fails the run when the allocator is evaluated.
entity string_allocator_bounds is
end entity;

architecture test of string_allocator_bounds is
begin
  process
    type string_ptr is access string;
    variable q : string_ptr;
    variable z : integer := 0;
  begin
    q := new string(z to 2);
    report "not reached";
    wait;
  end process;
end architecture;
