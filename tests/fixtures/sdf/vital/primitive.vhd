-- SPDX-License-Identifier: Apache-2.0
-- FSIM-VITAL-MODEL: primitive
library ieee;
use ieee.std_logic_1164.all;
entity fsim_vital_primitive is
  port (a : in std_logic; z : out std_logic);
end entity;
architecture vital of fsim_vital_primitive is
begin
  z <= not a after 1 ns;
end architecture;
