-- SPDX-License-Identifier: Apache-2.0
--
-- The operators of a classified generic type (IEEE 1076-2019 5.8.3-5.8.5)
-- are operations of the subtype its actual denotes (6.5.3.1): COUNTS'
-- explicit "+" and "<" for COUNT, and LEVELS' "<" and "=" for LEVEL, hide
-- the predefined operators of those types (12.3), so the instances for
-- COUNT and LEVEL call them; the instance for INTEGER keeps the predefined
-- operators (9.2). So do MINIMUM, MAXIMUM and TO_STRING, which a scalar
-- class declares with its operators (5.8.3-5.8.7): COUNTS' explicit MAXIMUM
-- and TO_STRING for COUNT are the instance's, its MINIMUM the predefined
-- one (5.2.6).
package counts is
  type count is range 0 to 100;
  function "+" (l, r : count) return count;
  function "<" (l, r : count) return boolean;
  function maximum (l, r : count) return count;
  function to_string (value : count) return string;
end package;

package body counts is
  function "+" (l, r : count) return count is
  begin
    report "cgop user + called";
    return 7;
  end function;

  function "<" (l, r : count) return boolean is
  begin
    report "cgop user count < called";
    return true;
  end function;

  function maximum (l, r : count) return count is
  begin
    report "cgop user count maximum called";
    return 42;
  end function;

  function to_string (value : count) return string is
  begin
    return "count" & integer'image(integer(value));
  end function;
end package body;

package levels is
  type level is (low, mid, high);
  function "<" (l, r : level) return boolean;
  function "=" (l, r : level) return boolean;
end package;

package body levels is
  function "<" (l, r : level) return boolean is
  begin
    report "cgop user level < called";
    return true;
  end function;

  function "=" (l, r : level) return boolean is
  begin
    report "cgop user level = called";
    return false;
  end function;
end package body;

package arithmetic is
  generic (type n is range <>);
  function sum (l, r : n) return n;
  function less (l, r : n) return boolean;
end package;

package body arithmetic is
  function sum (l, r : n) return n is
  begin
    return l + r;
  end function;

  function less (l, r : n) return boolean is
  begin
    return l < r;
  end function;
end package body;

package extremes is
  generic (type n is range <>);
  function larger (l, r : n) return n;
  function smaller (l, r : n) return n;
  function image (value : n) return string;
end package;

package body extremes is
  function larger (l, r : n) return n is
  begin
    return maximum(l, r);
  end function;

  function smaller (l, r : n) return n is
  begin
    return minimum(l, r);
  end function;

  function image (value : n) return string is
  begin
    return to_string(value);
  end function;
end package body;

package ordering is
  generic (type d is (<>));
  function less (l, r : d) return boolean;
  function same (l, r : d) return boolean;
end package;

package body ordering is
  function less (l, r : d) return boolean is
  begin
    return l < r;
  end function;

  function same (l, r : d) return boolean is
  begin
    return l = r;
  end function;
end package body;

entity top is
end entity;

architecture test of top is
  package count_arithmetic is new work.arithmetic
    generic map (n => work.counts.count);
  package integer_arithmetic is new work.arithmetic
    generic map (n => integer);
  package level_ordering is new work.ordering
    generic map (d => work.levels.level);
  package count_extremes is new work.extremes
    generic map (n => work.counts.count);
  package integer_extremes is new work.extremes
    generic map (n => integer);
begin
  process
    variable c : work.counts.count;
    variable i : integer;
  begin
    c := count_arithmetic.sum(1, 2);
    report "cgop count sum " & integer'image(integer(c));
    report "cgop count less " & boolean'image(count_arithmetic.less(5, 2));
    i := integer_arithmetic.sum(1, 2);
    report "cgop integer sum " & integer'image(i);
    report "cgop integer less " & boolean'image(integer_arithmetic.less(5, 2));
    report "cgop level less " & boolean'image(
      level_ordering.less(work.levels.high, work.levels.low));
    report "cgop level same " & boolean'image(
      level_ordering.same(work.levels.high, work.levels.high));
    c := count_extremes.larger(3, 7);
    report "cgop count larger " & integer'image(integer(c));
    c := count_extremes.smaller(3, 7);
    report "cgop count smaller " & integer'image(integer(c));
    report "cgop count image " & count_extremes.image(5);
    i := integer_extremes.larger(3, 7);
    report "cgop integer larger " & integer'image(i);
    i := integer_extremes.smaller(3, 7);
    report "cgop integer smaller " & integer'image(i);
    report "cgop integer image " & integer_extremes.image(5);
    wait;
  end process;
end architecture;
