-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 9.2.3: = and /= are predefined for every non-file type, including records.
-- Expected: success.
-- fsim-standard: 2008
entity t16 is
end entity;
architecture a of t16 is
  type rec is record
    a : integer;
  end record;
begin
  process
    variable r1, r2 : rec;
    variable b : boolean;
  begin
    b := r1 = r2;
    wait;
  end process;
end architecture;
