-- fsim: top=vhdl_now_and_string_formals(sim) std=2008
-- IEEE 1076-2008 16.3 (NOW) and 4.2.2.2: STRING-class procedure formals.
entity vhdl_now_and_string_formals is
end entity;

architecture sim of vhdl_now_and_string_formals is
begin
  process
    variable failures : natural := 0;
    variable checks : natural := 0;

    procedure check(condition : boolean; message : string) is
    begin
      checks := checks + 1;
      if not condition then
        failures := failures + 1;
        report "check failed: " & message severity error;
      end if;
    end procedure;
  begin
    check(now = 0 ns, "time zero");
    wait for 3 ns;
    check(now = 3 ns, "after 3 ns: " & integer'image(now / 1 ns));
    wait for 4 ns;
    check(now > 5 ns and now < 8 ns, "after 7 ns");
    check(checks = 3, "string formal calls");
    if failures = 0 then
      report "PASS";
    else
      report "FAIL " & integer'image(failures) severity error;
    end if;
    wait;
  end process;
end architecture;
