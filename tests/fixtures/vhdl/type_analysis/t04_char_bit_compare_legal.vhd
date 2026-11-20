-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 9.3.2/9.2.3: '1' compared with a BIT signal is a BIT literal.
-- Expected: success.
-- fsim-standard: 2008
entity t04 is
end entity;
architecture a of t04 is
  signal b : bit;
  signal r : boolean;
begin
  r <= b = '1';
end architecture;
