-- fsim: top=vhdl_mode_in_variable_defaults_batch58(sim) std=1993
-- SPDX-License-Identifier: Apache-2.0
-- Omitted and OPEN variable-class IN actuals use defaults; explicit variables remain actuals.
entity vhdl_mode_in_variable_defaults_batch58 is
end entity;

architecture sim of vhdl_mode_in_variable_defaults_batch58 is
begin
  process
    variable observed_left : integer := -1;
    variable observed_right : integer := -1;
    variable supplied : integer := 13;

    procedure capture(
      variable left_value : in integer := 7;
      variable right_value : in integer := 9) is
    begin
      observed_left := left_value;
      observed_right := right_value;
    end procedure;
  begin
    capture;
    assert observed_left = 7 and observed_right = 9
      report "omitted variable IN defaults were not used" severity error;

    capture(left_value => open);
    assert observed_left = 7 and observed_right = 9
      report "OPEN variable IN default was not used" severity error;

    capture(left_value => supplied);
    assert observed_left = 13 and observed_right = 9
      report "explicit variable actual or remaining default was lost" severity error;

    capture(right_value => supplied, left_value => open);
    assert observed_left = 7 and observed_right = 13
      report "named OPEN/default association was not preserved" severity error;

    report "PASS";
    wait;
  end process;
end architecture;
