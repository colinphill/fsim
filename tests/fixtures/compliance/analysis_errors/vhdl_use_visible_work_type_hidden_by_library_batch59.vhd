-- fsim-expect: FSIM-ELAB-VHTYPE-006 std=1993
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-1993 12.3: the directly visible logical library WORK hides
-- a same-named type that is only potentially visible through a use clause.
package visible_work_type_pkg is
  type work is (first_value, second_value);
end package;

use work.visible_work_type_pkg.all;
entity vhdl_use_visible_work_type_hidden_by_library_batch59 is
end entity;

architecture test of vhdl_use_visible_work_type_hidden_by_library_batch59 is
begin
  check : process
    variable doit : work;
  begin
    wait;
  end process;
end architecture;
