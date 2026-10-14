-- fsim: top=vhdl_component_bitvector_input_port_batch57(sim) std=1993
-- SPDX-License-Identifier: Apache-2.0

entity b57_bitvector_child is
  port (vector_in : in bit_vector);
end entity;

architecture rtl of b57_bitvector_child is
begin
  check_actual : process
    variable expected : bit_vector(31 downto 0)
      := B"01010010100101010010101001010100";
  begin
    assert vector_in'left = 31 and vector_in'right = 0
      report "FAIL unconstrained BIT_VECTOR port lost actual bounds"
      severity failure;
    for index in expected'range loop
      assert vector_in(index) = expected(index)
        report "FAIL BIT_VECTOR port element index "
          & integer'image(index)
        severity error;
    end loop;
    assert vector_in = expected
      report "FAIL unconstrained BIT_VECTOR port lost actual value"
      severity failure;
    assert false report "PASS" severity note;
    wait;
  end process;
end architecture;

entity vhdl_component_bitvector_input_port_batch57 is
end entity;

architecture sim of vhdl_component_bitvector_input_port_batch57 is
  subtype reg32 is bit_vector(31 downto 0);
  signal input_vector : reg32 := B"01010010100101010010101001010100";

  component b57_bitvector_child is
    port (vector_in : in bit_vector);
  end component;
  for u : b57_bitvector_child use entity work.b57_bitvector_child(rtl);
begin
  check_parent_value : process
  begin
    assert input_vector = B"01010010100101010010101001010100"
      report "FAIL parent BIT_VECTOR initializer is incorrect"
      severity failure;
    wait;
  end process;

  u : b57_bitvector_child port map (vector_in => input_vector);
end architecture;
