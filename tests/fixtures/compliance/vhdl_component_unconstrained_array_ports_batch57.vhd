-- fsim: top=component_unconstrained_array_ports_batch57(sim) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 5.3.2.2 and 14.3.5: undefined array index ranges on
-- component ports are obtained from their whole-object actual associations.
entity component_bits_echo_batch57 is
  port (
    data_in : in bit_vector;
    data_out : out bit_vector);
end entity;

architecture rtl of component_bits_echo_batch57 is
begin
  data_out <= data_in;

  check_bounds : process
  begin
    assert data_in'left = 4 and data_in'right = 7
      report "FAIL input bounds were not taken from the component actual"
      severity failure;
    assert data_out'left = 4 and data_out'right = 7
      report "FAIL output bounds were not taken from the component actual"
      severity failure;
    wait;
  end process;
end architecture;

entity component_string_echo_batch57 is
  port (
    text_in : in string;
    text_out : out string);
end entity;

architecture rtl of component_string_echo_batch57 is
begin
  text_out <= text_in;

  check_bounds : process
  begin
    assert text_in'left = 9 and text_in'right = 5
      report "FAIL STRING input bounds were not taken from the actual"
      severity failure;
    assert text_out'left = 9 and text_out'right = 5
      report "FAIL STRING output bounds were not taken from the actual"
      severity failure;
    wait;
  end process;
end architecture;

entity component_unconstrained_array_ports_batch57 is
end entity;

architecture sim of component_unconstrained_array_ports_batch57 is
  signal bits_in : bit_vector(4 to 7) := "1010";
  signal bits_out : bit_vector(4 to 7);
  signal text_in : string(9 downto 5) := "hello";
  signal text_out : string(9 downto 5);

  component component_bits_echo_batch57 is
    port (
      data_in : in bit_vector;
      data_out : out bit_vector);
  end component;

  component component_string_echo_batch57 is
    port (
      text_in : in string;
      text_out : out string);
  end component;

  for bits_child : component_bits_echo_batch57
    use entity work.component_bits_echo_batch57(rtl);
  for string_child : component_string_echo_batch57
    use entity work.component_string_echo_batch57(rtl);
begin
  bits_child : component_bits_echo_batch57
    port map (data_in => bits_in, data_out => bits_out);
  string_child : component_string_echo_batch57
    port map (text_in => text_in, text_out => text_out);

  check_values : process
  begin
    wait for 1 ns;
    assert bits_out = "1010"
      report "FAIL unconstrained component BIT_VECTOR port value"
      severity failure;
    assert text_out = "hello"
      report "FAIL unconstrained component STRING port value"
      severity failure;
    report "PASS" severity note;
    wait;
  end process;
end architecture;
