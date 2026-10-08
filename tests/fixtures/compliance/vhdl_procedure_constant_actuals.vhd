-- fsim: top=vhdl_procedure_constant_actuals(sim) std=2008
-- Each call of a procedure receives its own actual values (IEEE 1076-2008
-- 4.2.2.2), including locally static ones.
entity vhdl_procedure_constant_actuals is
end entity;

architecture sim of vhdl_procedure_constant_actuals is
begin
  process
    variable total : integer := 0;
    procedure accumulate(n : integer) is
    begin
      for i in 1 to n loop
        total := total + 1;
      end loop;
      total := total + n * 100;
    end procedure;
  begin
    accumulate(3);
    accumulate(5);
    -- 3 + 300 + 5 + 500
    if total = 808 then
      report "PASS";
    else
      report "FAIL total=" & integer'image(total) severity error;
    end if;
    wait;
  end process;
end architecture;
