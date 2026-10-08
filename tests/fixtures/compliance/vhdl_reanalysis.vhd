-- fsim: top=reanalysis(sim) std=1993 before=vhdl_reanalysis_first.vhd.in
-- IEEE 1076-1993 11.4: analyzing a library unit again replaces the unit of
-- the same name, even when another design file defined it; the secondary
-- units of a replaced primary unit become obsolete.
package limits is
  constant maximum : natural := 7;
end package;

entity counter is
  port (value : out natural);
end entity;

architecture first of counter is
begin
  value <= 10;
end architecture;

use work.limits.all;

entity reanalysis is
end entity;

architecture sim of reanalysis is
  signal value : natural;
begin
  dut : entity work.counter port map (value => value);

  process
  begin
    wait for 1 ns;
    if maximum = 7 and value = 10 then
      report "PASS";
    else
      report "FAIL maximum=" & integer'image(maximum)
        & " value=" & integer'image(value) severity error;
    end if;
    wait;
  end process;
end architecture;
