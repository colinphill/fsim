-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 9.3.5: bit'('1') qualifies the literal as BIT.
-- Expected: success.
-- fsim-standard: 2008
entity t20 is
end entity;
architecture a of t20 is
begin
  process
    variable b : bit;
  begin
    b := bit'('1');
    wait;
  end process;
end architecture;
