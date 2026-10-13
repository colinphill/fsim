-- fsim: top=vhdl_aggregate_index_subtype_effects(sim) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2008: unconstrained array aggregate bounds follow the left
-- bound and direction of the index subtype; unequal-length comparisons still
-- evaluate both an impure aggregate element and an impure array operand.
entity vhdl_aggregate_index_subtype_effects is
end entity;

architecture sim of vhdl_aggregate_index_subtype_effects is
  subtype reverse_index is integer range 3 downto 0;
  type reverse_vector is array (reverse_index range <>) of integer;

  function reverse_shape(value : reverse_vector) return boolean is
  begin
    return value'left = 3 and value'right = 2 and not value'ascending
      and value(3) = 21 and value(2) = 22;
  end function;
begin
  process
    variable scalar_calls : natural := 0;
    variable array_calls : natural := 0;
    impure function next_scalar return integer is
    begin
      scalar_calls := scalar_calls + 1;
      return 31;
    end function;
    impure function next_vector return reverse_vector is
      variable result : reverse_vector(3 downto 3);
    begin
      array_calls := array_calls + 1;
      result(3) := 31;
      return result;
    end function;
  begin
    assert reverse_shape((21, 22))
      report "descending index subtype aggregate bounds were incorrect"
      severity failure;
    assert (next_scalar, 32) /= next_vector
      report "different-length arrays compared equal"
      severity failure;
    assert scalar_calls = 1 and array_calls = 1
      report "array comparison skipped an impure operand"
      severity failure;
    report "PASS";
    wait;
  end process;
end architecture;
