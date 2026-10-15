-- fsim-expect: FSIM-ELAB-VHTYPE-006 std=1993
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-1993 12.3: a type mark is ambiguous when same-named type and
-- function declarations are potentially visible from separate packages.
package enum_type_pkg is
  type mc is (low, high);
end package;

package function_pkg is
  function mc return boolean;
end package;

package body function_pkg is
  function mc return boolean is
  begin
    return false;
  end function;
end package body;

use work.enum_type_pkg.all;
use work.function_pkg.all;
entity vhdl_use_visible_type_function_ambiguity_batch59 is
end entity;

architecture test of vhdl_use_visible_type_function_ambiguity_batch59 is
begin
  check : process
    variable value : mc;
  begin
    wait;
  end process;
end architecture;
