-- fsim: top=vhdl_overloaded_operator_generic_batch57_cfg std=2008
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076 overload resolution selects the BIT_VECTOR operator from the
-- nominal types of its operands, even when a same-named STD_LOGIC_VECTOR
-- overload is visible.
library ieee;
use ieee.std_logic_1164.all;

package overloaded_operator_generic_batch57_pkg is
  function "+" (l, r : bit_vector) return bit_vector;
  function "+" (l, r : std_logic_vector) return std_logic_vector;
end package;

library ieee;
use ieee.std_logic_1164.all;
package body overloaded_operator_generic_batch57_pkg is
  function "+" (l, r : bit_vector) return bit_vector is
  begin
    return B"1111";
  end function;

  function "+" (l, r : std_logic_vector) return std_logic_vector is
  begin
    return l;
  end function;
end package body;

entity overloaded_operator_generic_batch57_leaf is
  generic (value : bit_vector(3 downto 0));
end entity;

architecture sim of overloaded_operator_generic_batch57_leaf is
begin
  verify : process
  begin
    assert value = B"1111"
      report "FAIL: BIT_VECTOR operator overload was not selected"
      severity failure;
    report "PASS" severity note;
    wait;
  end process;
end architecture;

use work.overloaded_operator_generic_batch57_pkg.all;
entity vhdl_overloaded_operator_generic_batch57 is
end entity;

architecture sim of vhdl_overloaded_operator_generic_batch57 is
  constant l : bit_vector(3 downto 0) := B"0100";
  constant c : bit_vector(3 downto 0) := B"1011";
  component overloaded_operator_generic_batch57_leaf is
    generic (value : bit_vector(3 downto 0));
  end component;
begin
  typed_child : overloaded_operator_generic_batch57_leaf;
  literal_child : overloaded_operator_generic_batch57_leaf;
end architecture;

use work.overloaded_operator_generic_batch57_pkg.all;
configuration vhdl_overloaded_operator_generic_batch57_cfg
  of vhdl_overloaded_operator_generic_batch57 is
  for sim
    for typed_child : overloaded_operator_generic_batch57_leaf
      use entity work.overloaded_operator_generic_batch57_leaf(sim)
        generic map (l + c);
    end for;
    for literal_child : overloaded_operator_generic_batch57_leaf
      use entity work.overloaded_operator_generic_batch57_leaf(sim)
        generic map (B"0100" + c);
    end for;
  end for;
end configuration;
