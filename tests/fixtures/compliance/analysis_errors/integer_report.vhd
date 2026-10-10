-- fsim-expect: FSIM-ELAB-VHREPORT-001 std=1993
-- IEEE 1076-2008 10.3: a report expression has type STRING.
entity integer_report is
end entity;

architecture a of integer_report is
begin
  process
    variable count : integer := 1;
  begin
    report count;
    wait;
  end process;
end architecture;
