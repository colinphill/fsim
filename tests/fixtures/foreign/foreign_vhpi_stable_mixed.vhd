-- SPDX-License-Identifier: Apache-2.0
-- The VHDL half of the S'STABLE and S'QUIET mixed-language design: the
-- implicit signals of a port that SystemVerilog drives (IEEE 1076-2019
-- 14.7.4).
library ieee;
use ieee.std_logic_1164.all;

entity stable_leaf is
  port (p : in std_logic);
end entity;

architecture rtl of stable_leaf is
  signal st : boolean;
  signal qt : boolean;
begin
  st <= p'stable;
  qt <= p'quiet;
end architecture;
