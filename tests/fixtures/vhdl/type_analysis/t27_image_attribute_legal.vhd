-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-1993 14.1: T'IMAGE(X) yields STRING.
-- Expected: success.
-- fsim-standard: 1993
entity t27 is
end entity;
architecture a of t27 is
begin
  process
    variable s : string(1 to 1);
  begin
    s := integer'image(7);
    wait;
  end process;
end architecture;
