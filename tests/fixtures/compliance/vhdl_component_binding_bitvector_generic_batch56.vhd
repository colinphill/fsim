-- fsim: top=component_binding_bitvector_generic_batch56(sim) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- An unconstrained BIT_VECTOR generic actual remains a packed vector value.
entity component_binding_bitvector_leaf_batch56 is
  generic (vector_value : bit_vector);
end entity;

architecture rtl of component_binding_bitvector_leaf_batch56 is
begin
  check : process
    type bit_vector_record_batch56 is record
      value : bit_vector(0 to 31);
    end record;
    variable local_value : bit_vector(0 to 31) := vector_value;
    variable record_value : bit_vector_record_batch56 :=
      (value => vector_value);
  begin
    wait for 1 ns;
    assert local_value = vector_value
      report "FAIL: unconstrained generic differs from constrained local"
      severity failure;
    assert record_value.value = vector_value
      report "FAIL: unconstrained generic differs from constrained record field"
      severity failure;
    assert vector_value /= B"0"
      report "FAIL: unequal bit_vector extents compared equal"
      severity failure;
    assert vector_value = B"0101_0010_1001_0101_0010_1010_0101_0100"
      report "FAIL: bit_vector generic actual changed during binding"
      severity failure;
    assert false report "PASS" severity note;
    wait;
  end process;
end architecture;

entity component_binding_bitvector_generic_batch56 is
  generic (C11 : bit_vector := B"0011");
end entity;

architecture sim of component_binding_bitvector_generic_batch56 is
  type bit_vector_record_default_batch56 is record
    value : bit_vector(0 to 3);
  end record;

  component bitvector_component_batch56 is
    generic (vector_value : bit_vector);
  end component;

  for instance : bitvector_component_batch56
    use entity work.component_binding_bitvector_leaf_batch56(rtl);
begin
  instance : bitvector_component_batch56
    generic map (B"0101_0010_1001_0101_0010_1010_0101_0100");

  check_default_generic : process
    variable local_default : bit_vector(0 to 3) := C11;
    variable record_default : bit_vector_record_default_batch56 :=
      (value => C11);
  begin
    assert C11 = B"0011"
      report "FAIL: top-level unconstrained generic default changed"
      severity failure;
    assert local_default = C11
      report "FAIL: default generic differs from constrained local"
      severity failure;
    assert record_default.value = C11
      report "FAIL: default generic differs from constrained record field"
      severity failure;
    assert C11 /= B"00011"
      report "FAIL: unequal default generic extents compared equal"
      severity failure;
    wait;
  end process;
end architecture;
