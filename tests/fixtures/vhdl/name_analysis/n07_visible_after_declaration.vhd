-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 12.3: a declaration is visible only after its end (self-reference in its initializer is not visible).
-- Expected: FSIM-VHDL-NAME-001 at line 7 (c uses itself) and line 8 (d uses later e).
entity n07 is
end entity;
architecture a of n07 is
  constant c : integer := c + 1;
  constant d : integer := e;
  constant e : integer := 1;
begin
end architecture;
