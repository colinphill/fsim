-- fsim-expect: FSIM-ELAB-048 std=1993
-- IEEE 1076-2008 10.8: an if condition has type BOOLEAN.
entity integer_condition is
end entity;

architecture a of integer_condition is
begin
  process
    variable count : integer := 1;
  begin
    if count then
      count := 0;
    end if;
    wait;
  end process;
end architecture;
