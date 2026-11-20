-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 9.2.5: "&" of an array and an element.
-- Expected: success.
-- fsim-standard: 2008
entity t11 is
end entity;
architecture a of t11 is
begin
  process
    variable s : string(1 to 3);
  begin
    s := "ab" & 'c';
    wait;
  end process;
end architecture;
