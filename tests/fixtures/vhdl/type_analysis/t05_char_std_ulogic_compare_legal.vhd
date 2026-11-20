-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 9.3.2: '1' compared with a STD_ULOGIC signal is a STD_ULOGIC literal.
-- Expected: success.
-- fsim-standard: 2008
library ieee;
use ieee.std_logic_1164.all;
entity t05 is
end entity;
architecture a of t05 is
  signal s : std_ulogic;
  signal r : boolean;
begin
  r <= s = '1';
end architecture;
