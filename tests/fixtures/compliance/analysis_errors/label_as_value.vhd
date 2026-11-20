-- fsim-expect: FSIM-VHDL-TYPE-001 std=1993
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2008 12.3, 12.5: a statement label does not denote a value, so
-- the name has no interpretation at compile time.
entity label_as_value is
end entity;

architecture a of label_as_value is
  signal count : integer := 0;
begin
  driver : count <= 1;

  process
    variable copy : integer;
  begin
    copy := driver;
    wait;
  end process;
end architecture;
