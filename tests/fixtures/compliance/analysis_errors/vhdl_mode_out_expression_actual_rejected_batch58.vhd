-- fsim-expect: FSIM-ELAB-VHPROC-018 std=1993
-- SPDX-License-Identifier: Apache-2.0
-- OUT variable formals still require writable actuals.
entity vhdl_mode_out_expression_actual_rejected_batch58 is
end entity;

architecture sim of vhdl_mode_out_expression_actual_rejected_batch58 is
begin
  process
    procedure produce(variable value : out integer) is
    begin
      value := 1;
    end procedure;
  begin
    produce(12);
    wait;
  end process;
end architecture;
