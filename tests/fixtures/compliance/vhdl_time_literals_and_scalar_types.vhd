-- fsim: top=vhdl_time_literals_and_scalar_types(sim) std=1993
-- IEEE 1076-2008 5.2.4.3: the resolution limit lets every TIME literal of
-- the design be represented (here 1500 ps outside any delay); 5.2.3:
-- integer type definitions declare distinct types; 10.9: case choices given
-- as ranges of the selector's subtype.
entity vhdl_time_literals_and_scalar_types is
end entity;

architecture sim of vhdl_time_literals_and_scalar_types is
  type small is range 0 to 15;
  type other_small is range 0 to 15;
  subtype window is integer range 20 to 45;
  constant step : time := 1500 ps;
begin
  process
    variable failures : natural := 0;
    variable a : small := 3;
    variable b : other_small := 4;
    variable w : window := 30;
    variable bucket : natural := 0;
  begin
    if step + step /= 3 ns then failures := failures + 1; end if;
    a := a + 2;
    b := other_small(a) * 2;
    if a /= 5 or b /= 10 then failures := failures + 1; end if;
    case w is
      when 20 to 29 => bucket := 1;
      when 30 to 39 => bucket := 2;
      when others => bucket := 3;
    end case;
    if bucket /= 2 then failures := failures + 1; end if;
    wait for step;
    if now /= 1500 ps then failures := failures + 1; end if;
    if failures = 0 then
      report "PASS";
    else
      report "FAIL " & integer'image(failures) severity error;
    end if;
    wait;
  end process;
end architecture;
