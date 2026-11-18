-- SPDX-License-Identifier: Apache-2.0
-- STRING values whose bounds are not static keep their own index ranges
-- (IEEE 1076-2019 5.3.2.2): objects of an access-to-STRING type (9.3.7),
-- a named aggregate's smallest and largest choices with STRING's ascending
-- index subtype POSITIVE (9.3.3.3), a function result with the bounds of
-- the value returned (4.2.1, 16.2), and a formal with its actual's (a slice
-- with run-time bounds, 8.5). STD.TEXTIO.LINE values stay 1 to their
-- length (16.4).

-- Objects an access-to-STRING value designates.
library std;
use std.textio.all;

entity string_shaped_heap is
end entity;

architecture test of string_shaped_heap is
begin
  process
    type string_ptr is access string;
    variable q, r, t, u : string_ptr;
    variable l : line;
    variable n : integer := 7;
  begin
    q := new string(7 to 9);
    report "h1 " & integer'image(q'left) & " " & integer'image(q'right)
      & " " & boolean'image(q'ascending);
    q(8) := 'm';
    report "h2 " & integer'image(character'pos(q(7))) & " "
      & character'image(q(8));
    r := new string(n - 2 downto n - 5);
    report "h3 " & integer'image(r'left) & " " & integer'image(r'right)
      & " " & boolean'image(r'ascending) & " " & integer'image(r'length);
    r.all := "wxyz";
    report "h4 " & r.all & " " & character'image(r(5)) & " "
      & character'image(r(2));
    t := new string'(2 => 'b', 3 => 'c');
    report "h5 " & integer'image(t'left) & " " & integer'image(t'right)
      & " " & t.all;
    deallocate(t);
    u := new string(n to n + 2);
    u.all := "abc";
    report "h6 " & integer'image(u'left) & " " & u(n + 1 to n + 2);
    if u.all = "abc" then
      report "h7 equal";
    end if;
    l := new string'("abc");
    if u.all = l.all then
      report "h8 equal";
    end if;
    deallocate(l);
    l := new string'("xyz");
    u.all := l.all;
    report "h9 " & integer'image(u'left) & " " & u.all;
    t := new string'(l.all & "w");
    report "h10 " & integer'image(t'left) & " " & integer'image(t'right)
      & " " & t.all;
    deallocate(t);
    t := new string'(n to n + 1 => 'e');
    report "h11 " & integer'image(t'left) & " " & integer'image(t'right)
      & " " & t.all;
    deallocate(t);
    t := new string'(n downto n - 1 => 'f');
    report "h12 " & integer'image(t'left) & " " & integer'image(t'right)
      & " " & boolean'image(t'ascending) & " " & t.all;
    deallocate(t);
    -- An element is a CHARACTER (5.3.2.1), compared as one (9.2.3).
    if q(8) = 'm' and q(7) /= 'm' then
      report "h13 equal";
    end if;
    deallocate(q);
    deallocate(r);
    deallocate(u);
    deallocate(l);
    wait;
  end process;
end architecture;

-- A STRING is indexed by POSITIVE (16.3): an aggregate whose range choice
-- has a bound below 1 fails the run when the allocator is evaluated.
entity string_shaped_bounds is
end entity;

architecture test of string_shaped_bounds is
begin
  process
    type string_ptr is access string;
    variable q : string_ptr;
    variable z : integer := 0;
  begin
    q := new string'(z to 1 => 'a');
    report "not reached";
    wait;
  end process;
end architecture;

-- A function result has the bounds of the value its function returns.
entity string_shaped_result is
end entity;

architecture test of string_shaped_result is
  function f(s : string) return string is
  begin
    return s(3 to 5);
  end function;
  -- A slice of a local STRING with run-time bounds (8.5).
  function h(n : integer) return string is
    variable s : string(1 to 6) := "abcdef";
  begin
    return s(n to n + 2);
  end function;
begin
  process
    variable v : string(1 to 6) := "abcdef";
    variable n : integer := 2;
  begin
    report "r1 " & integer'image(f(v)'left) & " " & integer'image(f(v)'right)
      & " " & f(v);
    report "r2 " & integer'image(f(v)'length) & " "
      & boolean'image(f(v)'ascending);
    report "r3 " & integer'image(h(n)'left) & " " & integer'image(h(n)'right)
      & " " & h(n);
    wait;
  end process;
end architecture;

-- A formal of type STRING has its actual's bounds.
entity string_shaped_formal is
end entity;

architecture test of string_shaped_formal is
  procedure p(s : string) is
  begin
    report "p1 " & integer'image(s'left) & " " & integer'image(s'right)
      & " " & s;
  end procedure;
  function g(s : string) return integer is
  begin
    return s'left;
  end function;
begin
  process
    type string_ptr is access string;
    variable q : string_ptr;
    variable v : string(1 to 6) := "abcdef";
    variable i : integer := 3;
  begin
    p(v(i to i + 1));
    report "p2 " & integer'image(g(v(i to i + 1)));
    q := new string(i + 4 to i + 5);
    q.all := "xy";
    p(q.all);
    deallocate(q);
    wait;
  end process;
end architecture;

-- STD.TEXTIO.LINE values are 1 to their length.
library std;
use std.textio.all;

entity string_shaped_line is
end entity;

architecture test of string_shaped_line is
begin
  process
    variable l : line;
    file lines : text;
  begin
    write(l, string'("abc"));
    report "l1 " & integer'image(l'left) & " " & integer'image(l'right)
      & " " & l.all;
    write(l, 42);
    report "l2 " & integer'image(l'left) & " " & integer'image(l'right)
      & " " & l.all;
    file_open(lines, "string_shaped_line.txt", write_mode);
    writeline(lines, l);
    file_close(lines);
    file_open(lines, "string_shaped_line.txt", read_mode);
    readline(lines, l);
    file_close(lines);
    report "l3 " & integer'image(l'left) & " " & integer'image(l'right)
      & " " & l.all;
    l := new string'("xy");
    report "l4 " & integer'image(l'left) & " " & integer'image(l'right)
      & " " & l.all;
    deallocate(l);
    wait;
  end process;
end architecture;
