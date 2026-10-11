-- fsim: top=package_instances_and_blocks_cfg std=2008
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2008 4.9: a design-unit package instantiation of a generic
-- package with constant, type and function generics; 11.7.1: a component
-- instantiated without maps inside a block; 7.3: a configuration
-- specification in a block declarative part, including `use open`.
package scaled_math is
  generic (
    factor : integer;
    type element;
    function combine (a, b : integer) return integer);
  constant doubled_factor : integer := 2 * factor;
  function scale (value : integer) return integer;
  function same (value : element) return element;
end package;

package body scaled_math is
  function scale (value : integer) return integer is
  begin
    return combine(value * factor, doubled_factor);
  end function;

  function same (value : element) return element is
  begin
    return value;
  end function;
end package body;

package combiners is
  function add (a, b : integer) return integer;
end package;

package body combiners is
  function add (a, b : integer) return integer is
  begin
    return a + b;
  end function;
end package body;

package triple_math is new work.scaled_math
  generic map (factor => 3, element => integer,
    combine => work.combiners.add);

entity package_instances_leaf is
  port (value : out integer);
end entity;

architecture rtl of package_instances_leaf is
begin
  value <= 42;
end architecture;

use work.triple_math.all;

entity package_instances_and_blocks is
end entity;

architecture sim of package_instances_and_blocks is
  signal bound_value : integer := 0;
begin
  bound : block
    component leaf
      port (value : out integer);
    end component;
    for unbound_leaf : leaf use open;
  begin
    bound_leaf : leaf port map (value => bound_value);
    unbound_leaf : leaf;
  end block;

  check : process
    variable failures : natural := 0;
  begin
    -- 5 * 3 + 2 * 3
    if scale(5) /= 21 or doubled_factor /= 6 or same(7) /= 7 then
      failures := failures + 1;
    end if;
    wait for 1 ns;
    if bound_value /= 42 then
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

configuration package_instances_and_blocks_cfg of package_instances_and_blocks is
  for sim
    for bound
      for bound_leaf : leaf
        use entity work.package_instances_leaf(rtl);
      end for;
    end for;
  end for;
end configuration;
