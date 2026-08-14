-- SPDX-License-Identifier: Apache-2.0
-- FSIM-VITAL-MODEL: state-table
library ieee;
use ieee.std_logic_1164.all;
entity fsim_vital_state_table is
  port (clock : in std_logic; data : in std_logic; q : out std_logic);
end entity;
architecture vital of fsim_vital_state_table is
begin
  process (clock) is
  begin
    if rising_edge(clock) then q <= data after 1 ns; end if;
  end process;
end architecture;
