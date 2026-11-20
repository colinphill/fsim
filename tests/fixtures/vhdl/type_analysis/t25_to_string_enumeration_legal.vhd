-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 5.2.6: TO_STRING is implicitly declared for scalar types from VHDL-2008.
-- Expected: success.
-- fsim-standard: 2008
entity t25 is
end entity;
architecture a of t25 is
begin
  process
    variable s : string(1 to 5);
  begin
    s := to_string(true);
    wait;
  end process;
end architecture;
