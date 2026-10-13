-- fsim: top=vhdl_aggregate_self_sizing_context(sim) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2008 9.3.3.3: positional array aggregates take bounds from
-- the index subtype in unconstrained call and comparison contexts.
entity vhdl_aggregate_self_sizing_context is
end entity;

architecture sim of vhdl_aggregate_self_sizing_context is
  type int_vector is array (natural range <>) of integer;
  type int_matrix is array (natural range <>, natural range <>) of integer;

  function vector_shape(value : int_vector) return boolean is
  begin
    return value'left = 0 and value'right = 2 and value'length = 3
      and value(0) = 7 and value(2) = 9;
  end function;

  function matrix_shape(value : int_matrix) return boolean is
  begin
    return value'left(1) = 0 and value'right(1) = 1
      and value'left(2) = 0 and value'right(2) = 1
      and value(0, 0) = 1 and value(1, 1) = 4;
  end function;

  constant reference : int_vector(4 to 6) := (10, 20, 30);
begin
  process
    variable empty_left : int_vector(1 to 0);
    variable empty_right : int_vector(4 downto 5);
    variable singleton : int_vector(7 to 7);
  begin
    singleton(7) := 11;
    assert vector_shape((7, 8, 9))
      report "one-dimensional aggregate bounds were not self-sized"
      severity failure;
    assert matrix_shape(((1, 2), (3, 4)))
      report "multidimensional aggregate shape was not self-sized"
      severity failure;
    assert reference = (10, 20, 30)
      report "same-length positional aggregate comparison failed"
      severity failure;
    assert reference /= (10, 20)
      report "different-length positional aggregate comparison failed"
      severity failure;
    assert empty_left = empty_right
      report "two null arrays did not compare equal"
      severity failure;
    assert empty_left /= singleton
      report "null and non-null arrays compared equal"
      severity failure;
    report "PASS";
    wait;
  end process;
end architecture;
