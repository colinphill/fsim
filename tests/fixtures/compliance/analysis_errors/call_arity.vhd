-- fsim-expect: FSIM-ELAB-VHCALL-001 std=1993
-- IEEE 1076-2008 4.2.2.1: every formal without a default has an actual.
entity call_arity is
end entity;

architecture a of call_arity is
  function sum(left : integer; right : integer) return integer is
  begin
    return left + right;
  end function;
begin
  process
    variable total : integer;
  begin
    total := sum(1);
    wait;
  end process;
end architecture;
