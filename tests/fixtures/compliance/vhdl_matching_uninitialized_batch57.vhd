-- fsim: top=vhdl_matching_uninitialized(sim) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2008 9.2.3: matching equality retains the STD_ULOGIC U state.
library ieee;
use ieee.std_logic_1164.all;

entity vhdl_matching_uninitialized is
end entity;

architecture sim of vhdl_matching_uninitialized is
begin
  process
    variable states : std_ulogic_vector(0 to 8) := "UX01ZWLH-";
    variable equality_table : std_ulogic_vector(0 to 80) :=
    "UUUUUUUU1" & "UXXXXXXX1" & "UX10XX101" &
    "UX01XX011" & "UXXXXXXX1" & "UXXXXXXX1" &
    "UX10XX101" & "UX01XX011" & "111111111";
    variable inequality_table : std_ulogic_vector(0 to 80) :=
    "UUUUUUUU0" & "UXXXXXXX0" & "UX01XX010" &
    "UX10XX100" & "UXXXXXXX0" & "UXXXXXXX0" &
    "UX01XX010" & "UX10XX100" & "000000000";
    variable left_value, right_value, equal_value, unequal_value : std_ulogic;
    variable left_vector, right_vector : std_ulogic_vector(1 downto 0);
  begin
    for i in states'range loop
      for j in states'range loop
        left_value := states(i);
        right_value := states(j);
        equal_value := left_value ?= right_value;
        unequal_value := left_value ?/= right_value;
        assert equal_value = equality_table(i * 9 + j)
          report "FAIL matching equality truth table" severity error;
        assert unequal_value = inequality_table(i * 9 + j)
          report "FAIL matching inequality truth table" severity error;
      end loop;
    end loop;
    left_vector := "U0";
    right_vector := "X1";
    assert (left_vector ?= right_vector) = '0'
      report "FAIL known mismatch dominates uninitialized element" severity error;
    right_vector := "X0";
    assert (left_vector ?= right_vector) = 'U'
      report "FAIL vector matching preserves uninitialized element" severity error;
    right_vector := "-0";
    assert (left_vector ?= right_vector) = '1'
      report "FAIL wildcard masks uninitialized element" severity error;
    report "PASS";
    wait;
  end process;
end architecture;
