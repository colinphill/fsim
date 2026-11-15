-- SPDX-License-Identifier: Apache-2.0
-- The VHDL half of the S'STABLE and S'QUIET mixed-language design: the
-- implicit signals of a port that SystemVerilog drives (IEEE 1076-2019
-- 14.7.4).
library ieee;
use ieee.std_logic_1164.all;

entity stable_leaf is
  port (p : in std_logic; v : in std_logic_vector(3 downto 0));
end entity;

architecture rtl of stable_leaf is
  signal st : boolean;
  signal qt : boolean;
  signal vst : boolean;
  signal vqt : boolean;
begin
  st <= p'stable;
  qt <= p'quiet;
  vst <= v'stable;
  vqt <= v'quiet;
end architecture;
