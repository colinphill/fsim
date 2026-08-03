-- SPDX-License-Identifier: Apache-2.0
library ieee;
use ieee.std_logic_1164.all;

entity LogicStage is
  port (
    value  : in  std_logic;
    result : out std_logic
  );
end entity LogicStage;

architecture rtl of LogicStage is
begin
  -- SystemC inverts the SV stimulus once; VHDL inverts it again. The value
  -- observed by the SV top should therefore equal the original stimulus.
  result <= not value;
end architecture rtl;
