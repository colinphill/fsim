-- fsim-compile-only: std=2008 top=vhdl_function_signal_indexed_actual_compile_batch59(test)
-- SPDX-License-Identifier: Apache-2.0
-- An indexed signal name is a legal actual for a scalar signal formal.
entity vhdl_function_signal_indexed_actual_compile_batch59 is
end entity;

architecture test of vhdl_function_signal_indexed_actual_compile_batch59 is
  signal packed_value : bit_vector(0 to 3) := "1010";
  signal sampled_value : bit := '0';

  function selected_value(signal value : in bit) return bit is
  begin
    return value;
  end function;
begin
  check : process
  begin
    sampled_value <= selected_value(packed_value(1));
    wait;
  end process;
end architecture;
