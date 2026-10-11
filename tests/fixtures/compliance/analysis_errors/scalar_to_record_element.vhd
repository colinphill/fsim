-- fsim-expect: FSIM-ELAB-VHASSIGN-001 std=1993
-- IEEE 1076-2008 10.6.2.1: an integer is not a value of a record element
-- whose type is a record.
entity scalar_to_record_element is
end entity;

architecture a of scalar_to_record_element is
  type inner is record
    b : bit;
    n : integer;
  end record;
  type outer is record
    flag : bit;
    item : inner;
  end record;
begin
  process
    variable v : outer;
  begin
    v.item := 10;
    wait;
  end process;
end architecture;
