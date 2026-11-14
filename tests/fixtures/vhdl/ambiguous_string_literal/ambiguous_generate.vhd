-- SPDX-License-Identifier: Apache-2.0
--
-- TO_X01 has BIT_VECTOR and STD_ULOGIC_VECTOR overloads. A string literal's
-- type is determined solely from its context, excluding the literal itself
-- (IEEE 1076-2019 9.3.2), so "01XZ" fits both and the call is ambiguous
-- (12.5), whatever its characters.
library ieee;
use ieee.std_logic_1164.all;

entity top is
end entity;

architecture a of top is
begin
  g : if to_x01("01XZ")(1) = '0' generate
    process begin wait; end process;
  end generate;
end architecture;
