-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2008 10.5.4: exhaustive choices need no others alternative.
-- fsim: top=vhdl_exhaustive_selected_assignment_batch59(test) std=2008
entity vhdl_exhaustive_selected_assignment_batch59 is end entity;
architecture test of vhdl_exhaustive_selected_assignment_batch59 is
  type state_t is (first, second);
  signal state : state_t := first;
  signal bits : bit_vector(1 downto 0) := "00";
  signal enum_value, packed_value : integer := 0;
begin
  with state select enum_value <= 11 when first, 22 when second;
  with bits select packed_value <=
    0 when "00", 1 when "01", 2 when "10", 3 when "11";
  process
  begin
    wait for 1 ns;
    assert enum_value = 11 and packed_value = 0 severity failure;
    state <= second;
    bits <= "10";
    wait for 1 ns;
    assert enum_value = 22 and packed_value = 2 severity failure;
    bits <= "01";
    wait for 1 ns;
    assert packed_value = 1 severity failure;
    bits <= "11";
    wait for 1 ns;
    assert packed_value = 3 severity failure;
    report "PASS";
    wait;
  end process;
end architecture;
