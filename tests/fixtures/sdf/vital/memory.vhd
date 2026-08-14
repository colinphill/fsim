-- SPDX-License-Identifier: Apache-2.0
-- FSIM-VITAL-MODEL: memory
library ieee;
use ieee.std_logic_1164.all;
entity fsim_vital_memory is
  port (write_enable : in std_logic; data : in std_logic; q : out std_logic);
end entity;
architecture vital of fsim_vital_memory is
  signal stored : std_logic := '0';
begin
  stored <= data after 1 ns when write_enable = '1';
  q <= stored after 1 ns;
end architecture;
