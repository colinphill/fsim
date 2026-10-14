-- fsim: top=boolean_array_procedure_profile(rtl) std=2008
-- SPDX-License-Identifier: Apache-2.0

entity boolean_array_procedure_profile is
end entity;

architecture rtl of boolean_array_procedure_profile is
  type boolean_vector_t is array (natural range <>) of boolean;

  procedure verify(value : in boolean_vector_t; selected : out boolean) is
  begin
    for index in 0 to 3 loop
      assert value(index)
        report "boolean_vector_t actual lost its Boolean values"
        severity failure;
    end loop;
    selected := true;
  end procedure;

  procedure verify(value : in bit_vector; selected : out boolean) is
  begin
    for index in 0 to 3 loop
      assert value(index) = '0'
        report "BIT_VECTOR overload received the wrong actual"
        severity failure;
    end loop;
    selected := false;
  end procedure;

  signal boolean_values : boolean_vector_t(0 to 3) := (others => true);
  signal bit_values : bit_vector(0 to 3) := (others => '0');
begin
  process
    variable boolean_selected : boolean := false;
    variable bit_selected : boolean := true;
  begin
    verify(boolean_values, boolean_selected);
    verify(bit_values, bit_selected);
    wait for 1 ns;
    assert boolean_selected
      report "the Boolean-array overload was not selected"
      severity failure;
    assert not bit_selected
      report "the distinct BIT_VECTOR overload was not selected"
      severity failure;
    report "PASS" severity note;
    wait;
  end process;
end architecture;
