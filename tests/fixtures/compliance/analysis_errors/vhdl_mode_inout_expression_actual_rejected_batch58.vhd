-- fsim-expect: FSIM-ELAB-VHPROC-018 std=1993
-- SPDX-License-Identifier: Apache-2.0
-- INOUT variable formals still require writable actuals.
entity vhdl_mode_inout_expression_actual_rejected_batch58 is
end entity;

architecture sim of vhdl_mode_inout_expression_actual_rejected_batch58 is
begin
  process
    procedure update(variable value : inout integer) is
    begin
      value := value + 1;
    end procedure;
  begin
    update(12);
    wait;
  end process;
end architecture;
