-- fsim-expect: FSIM-ELAB-VHASSIGN-001 std=1993
-- IEEE 1076-2008 10.5.2.1: the CHARACTER literal NUL is not a BIT value.
entity character_to_bit is
end entity;

architecture a of character_to_bit is
  signal k : bit;
begin
  process
  begin
    k <= nul after 10 ns;
    wait;
  end process;
end architecture;
