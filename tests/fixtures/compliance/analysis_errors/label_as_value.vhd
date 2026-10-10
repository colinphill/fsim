-- fsim-expect: FSIM-ELAB-VHNAME-002 std=1993
-- IEEE 1076-2008 12.3: a statement label does not denote a value.
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
