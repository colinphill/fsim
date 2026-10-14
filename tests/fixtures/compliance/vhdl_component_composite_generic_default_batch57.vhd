-- fsim: top=component_composite_generic_default_batch57(sim) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- A component's omitted generic association supplies its locally static
-- array-of-record default to the entity generic of the bound entity.
package component_default_array_types_batch57 is
  type info is record
    field_1 : integer;
    field_2 : real;
  end record;
  type stuff is array (integer range 1 to 2) of info;
end package;

use work.component_default_array_types_batch57.all;
entity composite_default_array_leaf_batch57 is
  generic (gB : stuff := ((99, 9.0), (88, 8.0)));
end entity;

use work.component_default_array_types_batch57.all;
architecture rtl of composite_default_array_leaf_batch57 is
begin
  verify_default : process
  begin
    assert gB(1).field_1 = 11 and gB(1).field_2 = 1.25
      and gB(2).field_1 = 22 and gB(2).field_2 = 2.5
      report "FAIL composite component generic default was not passed"
      severity failure;
    assert gB = ((11, 1.25), (22, 2.5))
      report "FAIL composite generic aggregate equality"
      severity failure;
    report "PASS" severity note;
    wait;
  end process;
end architecture;

entity component_composite_generic_default_batch57 is
end entity;

use work.component_default_array_types_batch57.all;
architecture sim of component_composite_generic_default_batch57 is
  component composite_default_array_leaf_batch57 is
    generic (gB : stuff := ((11, 1.25), (22, 2.5)));
  end component;
  for child : composite_default_array_leaf_batch57
    use entity work.composite_default_array_leaf_batch57(rtl);
begin
  child : composite_default_array_leaf_batch57;
end architecture;
