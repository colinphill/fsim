-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 16.2: A'LENGTH yields universal_integer, usable as an INTEGER.
-- Expected: success.
-- fsim-standard: 2008
entity t26 is
end entity;
architecture a of t26 is
begin
  process
    variable v : bit_vector(1 to 4);
    variable n : integer;
  begin
    n := v'length;
    wait;
  end process;
end architecture;
