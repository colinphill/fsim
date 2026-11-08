-- SPDX-License-Identifier: Apache-2.0
-- Compiled as VHDL-1993 and mixed with a VHDL-2002 top: the units are in
-- one package family, so they share one IEEE.STD_LOGIC_1164 and
-- IEEE.NUMERIC_STD and the STD_LOGIC_VECTOR port types are the same type.
-- n = to_integer(unsigned("1010")) = 10.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity leaf is
  port (s : in std_logic_vector(3 downto 0); n : out integer);
end entity;

architecture a of leaf is
begin
  n <= to_integer(unsigned(s));
end architecture;
