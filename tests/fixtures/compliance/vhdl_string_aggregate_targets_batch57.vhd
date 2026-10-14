-- fsim: top=vhdl_string_aggregate_targets_batch57(sim) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- Constrained STRING values and function results split into CHARACTER targets.
entity vhdl_string_aggregate_targets_batch57 is
end entity;

architecture sim of vhdl_string_aggregate_targets_batch57 is
  subtype character_pair_t is string(1 to 2);

  function returned_pair return character_pair_t is
  begin
    return "cd";
  end function;

  signal first_signal : character;
  signal second_signal : character;
begin
  process
    variable source_text : character_pair_t := ('A', NUL);
    variable first_value : character;
    variable second_value : character;
  begin
    (first_value, second_value) := source_text;
    assert first_value = 'A' and second_value = NUL
      report "FAIL variable STRING aggregate target" severity failure;

    (first_value, second_value) := returned_pair;
    assert first_value = 'c' and second_value = 'd'
      report "FAIL function STRING aggregate target" severity failure;

    (first_signal, second_signal) <= source_text after 1 ns;
    wait for 1 ns;
    assert first_signal = 'A' and second_signal = NUL
      report "FAIL projected STRING signal aggregate target"
      severity failure;

    (first_signal, second_signal) <= returned_pair after 1 ns;
    wait for 1 ns;
    assert first_signal = 'c' and second_signal = 'd'
      report "FAIL projected function STRING signal aggregate target"
      severity failure;
    report "PASS";
    wait;
  end process;
end architecture;
