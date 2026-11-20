-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 9.3.6: integer(r) converts between closely related numeric types.
-- Expected: success.
-- fsim-standard: 2008
entity t18 is
end entity;
architecture a of t18 is
begin
  process
    variable r : real := 1.0;
    variable i : integer;
  begin
    i := integer(r);
    wait;
  end process;
end architecture;
