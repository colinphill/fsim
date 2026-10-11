-- fsim: top=character_arrays_and_bounds(sim) std=1993
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-1993 7.3.1: a string literal of a one-dimensional array of
-- CHARACTER; 14.1: 'LEFT, 'LOW and 'HIGH of the predefined REAL and TIME
-- types; 14.1: 'LAST_EVENT of a signal without an event is TIME'HIGH;
-- 6.4: an indexed name of an object whose subtype is a named subtype of a
-- predefined vector, with a loop parameter index.
entity character_arrays_and_bounds is
end entity;

architecture sim of character_arrays_and_bounds is
  type chars is array (integer range <>) of character;
  subtype five_chars is chars(1 to 5);
  subtype byte is bit_vector(0 to 7);
  signal quiet : bit;

  function same(a : five_chars) return five_chars is
  begin
    return a;
  end function;
begin
  process
    variable k : five_chars;
    variable r : real;
    variable t : time;
    variable b : byte := "00010000";
    variable ones : natural := 0;
    variable failures : natural := 0;
  begin
    k := same("ab7cd");
    if k /= "ab7cd" or k(3) /= '7' then
      failures := failures + 1;
    end if;
    r := real'high;
    if r <= 1.0e300 or real'low >= -1.0e300 or real'high <= real'low then
      failures := failures + 1;
    end if;
    t := time'high;
    if t <= 1 hr or quiet'last_event /= time'high then
      failures := failures + 1;
    end if;
    for i in 0 to 7 loop
      if b(i) = '1' then
        ones := ones + 1;
      end if;
    end loop;
    if ones /= 1 or b(3) /= '1' then
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
