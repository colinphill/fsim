-- fsim: top=vhdl_procedure_in_slice_copyin_batch58(sim) std=1993
-- SPDX-License-Identifier: Apache-2.0
-- Constrained IN formals copy in variable and constant array slices.
entity vhdl_procedure_in_slice_copyin_batch58 is
end entity;

architecture sim of vhdl_procedure_in_slice_copyin_batch58 is
  subtype bit_vector_4 is bit_vector(0 to 3);
  subtype bit_vector_4_desc is bit_vector(3 downto 0);
  subtype bit_vector_8 is bit_vector(0 to 7);
  constant constant_source : bit_vector_8 := B"1010_1100";
  signal variable_result : boolean := false;
  signal constant_result : boolean := false;
  signal descending_result : boolean := false;
  signal alias_result : boolean := false;
  signal reversed_result : boolean := false;

  procedure check_variable(
    variable value : in bit_vector_4;
    signal result : out boolean) is
  begin
    result <= value = B"1010";
  end procedure;

  procedure check_constant(
    constant value : in bit_vector_4;
    signal result : out boolean) is
  begin
    result <= value = B"1010";
  end procedure;

  procedure check_descending(
    variable value : in bit_vector_4_desc;
    signal result : out boolean) is
  begin
    result <= value = B"1010";
  end procedure;
begin
  test : process
    variable variable_source : bit_vector_8 := B"1010_1100";
    variable descending_source : bit_vector(7 downto 0)
      := B"1010_1100";
    alias variable_source_alias : bit_vector_8 is variable_source;
  begin
    check_variable(variable_source(0 to 3), variable_result);
    check_constant(constant_source(0 to 3), constant_result);
    check_descending(descending_source(7 downto 4), descending_result);
    check_variable(variable_source_alias(0 to 3), alias_result);
    check_variable(descending_source(7 downto 4), reversed_result);
    wait for 1 ns;
    assert variable_result and constant_result and descending_result
      and alias_result and reversed_result
      report "variable or constant IN slice was not copied in"
      severity failure;
    report "PASS: VHDL variable and constant IN slice copy-in"
      severity note;
    wait;
  end process;
  done : process
  begin
    wait for 1 ns;
    report "PASS" severity note;
    wait;
  end process;
end architecture;
