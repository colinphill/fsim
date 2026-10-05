library ieee;
use ieee.std_logic_1164.all;

entity falling_edge_delayed_probe is
end entity;

architecture rtl of falling_edge_delayed_probe is
  signal clk : std_logic;
  signal delayed_sample : std_logic;
begin
  stimulus: process
  begin
    clk <= '1';
    wait for 1 ns;
    clk <= '0';
    wait for 1 ns;
    wait;
  end process;

  observe: process
  begin
    wait for 2 ns;
    wait for 0 ns;
    report "SAMPLE clk_delayed=" & std_logic'image(clk'delayed(1 ns));
    delayed_sample <= clk'delayed(1 ns);
    wait for 1 ns;
    report "RESULT delayed_sample=" & std_logic'image(delayed_sample);
    wait;
  end process;
end architecture;
