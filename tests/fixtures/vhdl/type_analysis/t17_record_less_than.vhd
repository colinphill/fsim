-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 9.2.3: ordering operators exist only for scalar and discrete-array types, not records.
-- Expected: FSIM-VHDL-TYPE-001.
-- fsim-standard: 2008
entity t17 is
end entity;
architecture a of t17 is
  type rec is record
    a : integer;
  end record;
begin
  process
    variable r1, r2 : rec;
    variable b : boolean;
  begin
    b := r1 < r2;
    wait;
  end process;
end architecture;
