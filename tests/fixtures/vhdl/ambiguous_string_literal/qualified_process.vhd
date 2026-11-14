-- SPDX-License-Identifier: Apache-2.0
--
-- A qualified expression names the literal's type (IEEE 1076-2019 9.3.5), so
-- the call is not ambiguous.
library ieee;
use ieee.std_logic_1164.all;

entity top is
end entity;

architecture a of top is
begin
  process
    variable r : std_ulogic_vector(1 to 4);
  begin
    r := to_x01(std_ulogic_vector'("01XZ"));
    report "qualified " & to_string(r);
    wait;
  end process;
end architecture;
