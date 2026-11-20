-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 9.3.6 and 9.2.2: universal_integer operands convert implicitly to INTEGER.
-- Expected: success.
-- fsim-standard: 2008
entity t06 is
end entity;
architecture a of t06 is
  constant c : integer := 2 * 3;
begin
end architecture;
