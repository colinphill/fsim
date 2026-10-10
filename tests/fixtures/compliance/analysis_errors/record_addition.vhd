-- fsim-expect: FSIM-ELAB-VHOPER-001 std=1993
-- IEEE 1076-2008 9.2.5: no predefined "+" exists for a record type.
entity record_addition is
end entity;

architecture a of record_addition is
  type pair is record
    x, y : integer;
  end record;
begin
  process
    variable p : pair := (1, 2);
  begin
    p := p + p;
    wait;
  end process;
end architecture;
