-- SPDX-License-Identifier: Apache-2.0
-- Compiled as VHDL-1987 (no NUMERIC_STD, no "end entity") and mixed with a
-- VHDL-1993 top: one package family, one IEEE.STD_LOGIC_1164.
-- s(3) of "1010" is '1', so n = 1.
library ieee;
use ieee.std_logic_1164.all;

entity leaf is
  port (s : in std_logic_vector(3 downto 0); n : out integer);
end leaf;

architecture a of leaf is
begin
  n <= 1 when s(3) = '1' else 0;
end a;
