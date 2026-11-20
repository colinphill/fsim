-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 9.2.7: time * integer is TIME; time / time is universal_integer, convertible to INTEGER.
-- Expected: success.
-- fsim-standard: 2008
entity t09 is
end entity;
architecture a of t09 is
begin
  process
    variable t : time := 1 ns;
    variable i : integer;
  begin
    t := t * 2;
    i := t / 2 ns;
    wait;
  end process;
end architecture;
