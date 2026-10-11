-- fsim: top=qualified_aggregates_and_imports(sim) std=1993
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-1993 10.4: use clauses naming a whole library, a package, and
-- the contents of a package made visible by an earlier use clause; 7.3.2:
-- aggregates of CHARACTER and TIME arrays and qualified aggregates of
-- process-local types; 7.2.2: lexicographic ordering of discrete arrays of
-- different lengths, including aggregate operands; 3.3: a record with an
-- access-typed element.
package qai_pkg is
  constant offset : integer := 7;
  type time_vector is array (natural range <>) of time;
end package;

use work.all;
use qai_pkg.all;

entity qualified_aggregates_and_imports is
end entity;

use work.qai_pkg;

architecture sim of qualified_aggregates_and_imports is
  type chars is array (1 to 3) of character;
  type int_array is array (integer range <>) of integer;
  type int_ptr is access integer;
  type holder is record
    n : natural;
    p : int_ptr;
  end record;
begin
  process
    type nibble is array (3 downto 0) of bit;
    type triple is record
      x : bit;
      y : integer;
      z : boolean;
    end record;
    variable c : chars := ('a', 'b', 'c');
    variable t : time_vector(0 to 3) := (1 ns, 2 ns, 3 ns, 4 ns);
    variable n : nibble;
    variable r : triple;
    variable x : int_array(1 to 3) := (1, 2, 3);
    variable h : holder;
    variable failures : natural := 0;
  begin
    if c(2) /= 'b' or t(3) /= 4 ns or qai_pkg.offset /= offset then
      failures := failures + 1;
    end if;
    n := nibble'('1', '1', '1', '0');
    r := triple'(bit'('0'), 1, true);
    if n(3) /= '1' or n(0) /= '0' or r.y /= 1 or not r.z then
      failures := failures + 1;
    end if;
    if not (x >= (1, 1, 1)) or not (x < (1, 2, 3, 4)) or x < (1, 2)
      or not ((0, 9) < x) then
      failures := failures + 1;
    end if;
    if h.n /= 0 or h.p /= null then
      failures := failures + 1;
    end if;
    h.p := new integer'(5);
    if h.p.all /= 5 then
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
