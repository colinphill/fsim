-- fsim: top=vhdl_scalar_type_declarations(sim) std=2008
-- IEEE 1076-2008 5.2.3 and 5.2.5: integer and floating type declarations.
entity vhdl_scalar_type_declarations is
end entity;

architecture sim of vhdl_scalar_type_declarations is
  type small_t is range 0 to 10;
  type down_t is range 7 downto -3;
  type ratio_t is range 0.5 to 3.5;
begin
  process
    variable k : small_t := 1;
    variable d : down_t := 0;
    variable r : ratio_t := 1.0;
    variable failures : natural := 0;
  begin
    k := k + 4;
    if k /= 5 then failures := failures + 1; end if;
    if small_t'high /= 10 or small_t'low /= 0 then failures := failures + 1; end if;
    if down_t'left /= 7 or down_t'right /= -3 then failures := failures + 1; end if;
    d := d - 2;
    if d /= -2 then failures := failures + 1; end if;
    r := r * 2.5;
    if r /= 2.5 then failures := failures + 1; end if;
    if failures = 0 then
      report "PASS";
    else
      report "FAIL " & integer'image(failures) severity error;
    end if;
    wait;
  end process;
end architecture;
