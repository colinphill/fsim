-- SPDX-License-Identifier: Apache-2.0
-- FSIM-VITAL-MODEL: wrapper
library ieee;
use ieee.std_logic_1164.all;
entity fsim_vital_wrapper is
  generic (delay_ticks : time := 1 ns);
  port (a : in std_logic; z : out std_logic);
end entity;
architecture governed of fsim_vital_wrapper is
begin
  z <= transport a after delay_ticks;
end architecture;
