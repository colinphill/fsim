-- fsim: top=vhdl_function_signal_indexed_actual_batch59(sim) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- An indexed signal name is a legal actual for a scalar signal formal.
entity vhdl_function_signal_indexed_actual_batch59 is
end entity;

architecture sim of vhdl_function_signal_indexed_actual_batch59 is
  signal packed_value : bit_vector(0 to 3) := "1010";
  signal sampled_value : bit := '0';

  function selected_value(signal value : in bit) return bit is
  begin
    return value;
  end function;
begin
  check : process
  begin
    assert selected_value(packed_value(0)) = '1'
       and selected_value(packed_value(1)) = '0'
      report "indexed signal actual selected the wrong scalar value"
      severity failure;

    sampled_value <= selected_value(packed_value(1));
    wait for 0 ns;
    assert sampled_value = '0'
      report "indexed signal actual result was not retained"
      severity failure;

    report "PASS";
    wait;
  end process;
end architecture;
