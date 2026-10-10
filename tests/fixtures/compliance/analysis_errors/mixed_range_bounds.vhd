-- fsim-expect: FSIM-ELAB-VHRANGE-001 std=1993
-- IEEE 1076-2008 5.2.3.1: an integer type definition has integer bounds.
entity mixed_range_bounds is
end entity;

architecture a of mixed_range_bounds is
  type level is range 0 to 7.5;
begin
end architecture;
