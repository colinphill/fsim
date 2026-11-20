-- fsim-expect: FSIM-ELAB-VHCALL-003 std=2008
-- SPDX-License-Identifier: Apache-2.0
-- A record element of a variable is not a signal name (IEEE 1076-2019 8.3,
-- 4.2.2.2), so it is no actual for a user function's signal formal.
library ieee;
use ieee.std_logic_1164.all;

entity vhdl_function_signal_formal_variable_record_element_rejected_bd2 is
end entity;

architecture sim of vhdl_function_signal_formal_variable_record_element_rejected_bd2 is
  type pins is record
    clk : std_logic;
    d : std_logic;
  end record;
  function event_now(signal s : in std_logic) return boolean is
  begin
    return s'event;
  end function;
begin
  check : process
    variable local_pins : pins := ('0', '0');
  begin
    assert not event_now(local_pins.clk) severity failure;
    wait;
  end process;
end architecture;
