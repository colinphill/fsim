-- fsim-expect: FSIM-ELAB-VHCALL-003 std=2008
-- SPDX-License-Identifier: Apache-2.0
entity vhdl_function_signal_formal_variable_actual_rejected_batch59 is
end entity;

architecture sim of vhdl_function_signal_formal_variable_actual_rejected_batch59 is
  function event_now(signal value : in bit_vector) return boolean is
  begin
    return value'event;
  end function;
begin
  check : process
    variable local_value : bit_vector(0 to 2) := "000";
  begin
    assert not event_now(local_value) severity failure;
    wait;
  end process;
end architecture;
