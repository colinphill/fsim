-- fsim-expect: FSIM-ELAB-VHPROC-018 std=1993
-- SPDX-License-Identifier: Apache-2.0
-- An explicit expression equal to the default is still not a variable actual.
entity vhdl_mode_in_variable_expression_actual_rejected_batch58 is
end entity;

architecture sim of vhdl_mode_in_variable_expression_actual_rejected_batch58 is
begin
  process
    procedure consume(variable value : in integer := 7) is
    begin
      null;
    end procedure;
  begin
    consume(7);
    wait;
  end process;
end architecture;
