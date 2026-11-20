-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 9.3.2: null is the null literal of the access type given by context.
-- Expected: success.
-- fsim-standard: 2008
entity t23 is
end entity;
architecture a of t23 is
  type ptr is access integer;
begin
  process
    variable p : ptr;
  begin
    p := null;
    wait;
  end process;
end architecture;
