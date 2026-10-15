-- fsim: top=vhdl_string_concat_context_controls_batch59(sim) std=2008
-- SPDX-License-Identifier: Apache-2.0
entity vhdl_string_concat_context_controls_batch59 is
end entity;

architecture sim of vhdl_string_concat_context_controls_batch59 is
  signal source_text : string(5 downto 3) := "aba";

  function count_as(
    value : string;
    expected_left : integer;
    expected_right : integer;
    expected_ascending : boolean;
    expected_count : integer) return integer is
    variable count : integer := 0;
  begin
    assert value'left = expected_left
      report "STRING concatenation context control left bound was incorrect"
      severity failure;
    assert value'right = expected_right
      report "STRING concatenation context control right bound was incorrect"
      severity failure;
    assert value'ascending = expected_ascending
      report "STRING concatenation context control direction was incorrect"
      severity failure;
    for index in value'range loop
      if value(index) = 'a' then
        count := count + 1;
      end if;
    end loop;
    assert count = expected_count
      report "STRING concatenation context control contents were incorrect"
      severity failure;
    return count;
  end function;

  impure function make_piece(selector : integer) return string is
  begin
    assert selector = 0 severity failure;
    return source_text;
  end function;

  function append_a(value : string) return integer is
  begin
    return count_as(value & 'a', 1, 4, true, 3);
  end function;
begin
  context_controls : process
  begin
    -- The function result has runtime data and an unconstrained STRING
    -- profile, so the concatenation has no statically known operand length.
    assert count_as(make_piece(0) & "x", 1, 4, true, 2) = 2
      report "call-valued STRING concatenation context failed"
      severity failure;

    -- The formal is unconstrained and the expression's value is read through
    -- a signal. This exercises dynamic formal context and bounds propagation.
    assert append_a(source_text) = 3
      report "dynamic unconstrained STRING formal concatenation failed"
      severity failure;

    -- Context must reach aggregates through both concatenation levels.
    assert count_as((('b', 'a') & source_text) & ('a', 'b'),
        1, 7, true, 4) = 4
      report "nested positional CHARACTER aggregate context failed"
      severity failure;

    report "PASS" severity note;
    wait;
  end process;
end architecture;
