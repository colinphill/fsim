-- SPDX-License-Identifier: Apache-2.0
--
-- The same ambiguity (IEEE 1076-2019 9.3.2, 12.5) in a process: the target's
-- type selects no overload, because both return STD_ULOGIC_VECTOR.
library ieee;
use ieee.std_logic_1164.all;

entity top is
end entity;

architecture a of top is
begin
  process
    variable r : std_ulogic_vector(1 to 4);
  begin
    r := to_x01("01XZ");
    report "ambiguous " & to_string(r);
    wait;
  end process;
end architecture;
