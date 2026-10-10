-- fsim-expect: FSIM-ELAB-VHASSIGN-001 std=1993
-- IEEE 1076-2008 9.3.7: an allocator is a value of an access type.
entity allocator_to_scalar is
end entity;

architecture a of allocator_to_scalar is
begin
  process
    variable count : integer;
  begin
    count := new integer;
    wait;
  end process;
end architecture;
