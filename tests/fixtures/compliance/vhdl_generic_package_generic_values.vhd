-- fsim: top=generic_package_generic_values(sim) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2008 4.9 and 6.5.6.2: a generic package's declarations take the
-- generic values of the instance they belong to: a record constant whose
-- aggregate names a generic, a record constant whose element is sized by a
-- generic, and a protected type's variable initialized from a generic; also
-- when an entity's generic is mapped into the package instance.
package scaled_pkg is
  generic (n : integer);
  type pair_t is record
    a : integer;
    b : integer;
  end record;
  constant pair : pair_t := (a => n, b => n + 5);
  type sized_t is record
    a : integer;
    b : bit_vector(n downto 0);
  end record;
  constant sized : sized_t := (a => n + 3, b => (others => '1'));
  type counter_t is protected
    impure function get return integer;
  end protected;
  shared variable counter : counter_t;
  impure function read_counter return integer;
end package scaled_pkg;

package body scaled_pkg is
  type counter_t is protected body
    variable value : integer := n * 10;
    impure function get return integer is
    begin
      return value;
    end function;
  end protected body;

  impure function read_counter return integer is
  begin
    return counter.get;
  end function;
end package body scaled_pkg;

-- A package instance in an entity takes the generic values that entity's
-- instance maps, so each instance of the entity has its own.
entity generic_package_mapped is
  generic (w : integer);
end entity;

architecture sim of generic_package_mapped is
  package lo is new work.scaled_pkg generic map (n => w);
  package hi is new work.scaled_pkg generic map (n => w + 1);
begin
  process
  begin
    assert lo.pair.a = w and hi.pair.b = w + 6
      and lo.sized.a = w + 3 and hi.sized.a = w + 4
      and lo.sized.b'length = w + 1 and hi.sized.b'length = w + 2
      and lo.counter.get = w * 10 and hi.read_counter = (w + 1) * 10
      report "FAIL mapped " & integer'image(w) & " "
        & integer'image(lo.pair.a) & " " & integer'image(hi.pair.b) & " "
        & integer'image(lo.sized.b'length) & " "
        & integer'image(hi.read_counter)
      severity error;
    wait;
  end process;
end architecture;

entity generic_package_generic_values is
end entity;

architecture sim of generic_package_generic_values is
  package one is new work.scaled_pkg generic map (n => 1);
  package two is new work.scaled_pkg generic map (n => 2);
begin
  mapped_2 : entity work.generic_package_mapped generic map (w => 2);
  mapped_7 : entity work.generic_package_mapped generic map (w => 7);
  process
  begin
    if one.pair.a = 1 and one.pair.b = 6
      and two.pair.a = 2 and two.pair.b = 7
      and one.sized.a = 4 and two.sized.a = 5
      and one.sized.b'length = 2 and two.sized.b'length = 3
      and one.counter.get = 10 and two.counter.get = 20
      and one.read_counter = 10 and two.read_counter = 20 then
      report "PASS";
    else
      report "FAIL " & integer'image(one.pair.a) & " "
        & integer'image(two.pair.b) & " "
        & integer'image(one.sized.a) & " "
        & integer'image(two.sized.b'length) & " "
        & integer'image(one.counter.get) & " "
        & integer'image(two.counter.get) & " "
        & integer'image(one.read_counter) & " "
        & integer'image(two.read_counter);
    end if;
    wait;
  end process;
end architecture;
