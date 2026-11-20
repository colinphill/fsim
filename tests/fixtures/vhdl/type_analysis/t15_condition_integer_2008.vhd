-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 10.8/9.2.9: INTEGER has no "??" operator, so the condition is not BOOLEAN.
-- Expected: FSIM-VHDL-TYPE-001.
-- fsim-standard: 2008
entity t15 is
end entity;
architecture a of t15 is
begin
  process
    variable i : integer := 1;
  begin
    if i then
      null;
    end if;
    wait;
  end process;
end architecture;
