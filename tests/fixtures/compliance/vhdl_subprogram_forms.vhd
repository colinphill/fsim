-- fsim: top=vhdl_subprogram_forms(sim) std=2008
-- IEEE 1076-2008: protected type bodies in the same region (5.6.3),
-- parameterless function calls (9.3.4), edge predicates as expressions
-- (16.7), runtime WAIT FOR durations (10.2), and output-port defaults
-- (6.5.6.3) being accepted.
library ieee;
use ieee.std_logic_1164.all;

entity vhdl_subprogram_forms_leaf is
  port (ready : out std_ulogic := '1');
end entity;

architecture rtl of vhdl_subprogram_forms_leaf is
begin
end architecture;

library ieee;
use ieee.std_logic_1164.all;

entity vhdl_subprogram_forms is
end entity;

architecture sim of vhdl_subprogram_forms is
  type counter_t is protected
    procedure increment;
    impure function value return natural;
  end protected;

  type counter_t is protected body
    variable count : natural := 0;
    procedure increment is
    begin
      count := count + 1;
    end procedure;
    impure function value return natural is
    begin
      return count;
    end function;
  end protected body;

  signal clk : std_ulogic := '0';
  signal ready : std_ulogic;
  signal edges, others_seen : natural := 0;
  signal done : boolean := false;
begin
  leaf : entity work.vhdl_subprogram_forms_leaf port map (ready => ready);

  clk <= not clk after 5 ns when not done;

  count_edges : process (clk)
  begin
    if rising_edge(clk) then
      edges <= edges + 1;
    else
      others_seen <= others_seen + 1;
    end if;
  end process;

  stim : process
    variable base : natural := 40;
    variable period : time := 10 ns;
    impure function next_base return natural is
    begin
      base := base + 2;
      return base;
    end function;
    variable failures : natural := 0;
  begin
    if next_base /= 42 or next_base /= 44 then failures := failures + 1; end if;
    wait for period * 3;
    if now /= 30 ns then failures := failures + 1; end if;
    wait until rising_edge(clk);
    if now /= 35 ns or edges /= 3 then failures := failures + 1; end if;
    if failures = 0 then
      report "PASS";
    else
      report "FAIL " & integer'image(failures) severity error;
    end if;
    done <= true;
    wait;
  end process;
end architecture;
