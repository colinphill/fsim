-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 9.2.7: universal_real * universal_integer has no predefined operator; no interpretation fits.
-- Expected: FSIM-VHDL-TYPE-001.
-- fsim-standard: 2008
entity t07 is
end entity;
architecture a of t07 is
  signal x : integer;
begin
  x <= 1.5 * 2;
end architecture;
