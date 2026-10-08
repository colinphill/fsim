-- fsim: top=vhdl_signal_formals(sim) std=2008
-- IEEE 1076-2008 4.2.2.3: signal-class procedure formals denote the actual.
library ieee;
use ieee.std_logic_1164.all;

package vhdl_signal_formals_pkg is
  procedure pulse(signal s : out std_ulogic; constant width : time);
  procedure wait_edge(signal clk : in std_ulogic; constant count : positive);
end package;

package body vhdl_signal_formals_pkg is
  procedure pulse(signal s : out std_ulogic; constant width : time) is
  begin
    s <= '1';
    wait for width;
    s <= '0';
  end procedure;

  procedure wait_edge(signal clk : in std_ulogic; constant count : positive) is
  begin
    for i in 1 to count loop
      wait until rising_edge(clk);
    end loop;
  end procedure;
end package body;

library ieee;
use ieee.std_logic_1164.all;
use work.vhdl_signal_formals_pkg.all;

entity vhdl_signal_formals is
end entity;

architecture sim of vhdl_signal_formals is
  signal clk : std_ulogic := '0';
  signal a, b : std_ulogic := '0';
  signal done : boolean := false;
  signal edges : natural := 0;
begin
  clk <= not clk after 5 ns when not done;

  count_edges : process (clk)
  begin
    if rising_edge(clk) then
      edges <= edges + 1;
    end if;
  end process;

  stim : process
    variable failures : natural := 0;
  begin
    pulse(a, 3 ns);
    wait for 0 ns;  -- the final driver update matures one delta later
    if a /= '0' or now /= 3 ns then failures := failures + 1; end if;
    pulse(b, 7 ns);
    wait for 0 ns;
    if b /= '0' or now /= 10 ns then failures := failures + 1; end if;
    wait_edge(clk, 3);
    -- Rising edges at 5, 15, 25 ns: the third after 10 ns is at 35 ns.
    if now /= 35 ns then failures := failures + 1; end if;
    if a'last_event /= 32 ns then failures := failures + 1; end if;
    if failures = 0 then
      report "PASS";
    else
      report "FAIL " & integer'image(failures) severity error;
    end if;
    done <= true;
    wait;
  end process;
end architecture;
