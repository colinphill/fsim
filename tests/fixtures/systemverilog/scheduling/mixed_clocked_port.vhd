-- SPDX-License-Identifier: Apache-2.0
--
-- The VHDL top of mixed_clocked_port.sv: one VHDL clock drives a
-- SystemVerilog flop (q, s) and a VHDL flop (r, p) on the same edge. r takes
-- the SystemVerilog flop's output q, s takes the VHDL flop's output p; each
-- second stage samples the value from before the edge, a cycle behind its
-- first stage.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use std.textio.all;

entity mixed_clocked_port is
end entity;

architecture rtl of mixed_clocked_port is
  component mixed_clocked_leaf is
    port (clk : in std_ulogic;
          d : in std_ulogic_vector(7 downto 0);
          q : out std_ulogic_vector(7 downto 0);
          p : in std_ulogic_vector(7 downto 0);
          s : out std_ulogic_vector(7 downto 0));
  end component;
  signal clk : std_ulogic := '0';
  signal d, q, r, p, s : std_ulogic_vector(7 downto 0) := (others => '0');
begin
  leaf : mixed_clocked_leaf port map (clk => clk, d => d, q => q, p => p, s => s);
  flop : process (clk)
  begin
    if rising_edge(clk) then
      r <= q;
      p <= d;
    end if;
  end process;
  stimulus : process
  begin
    for i in 1 to 4 loop
      wait until rising_edge(clk);
      d <= std_ulogic_vector(to_unsigned(i, 8));
    end loop;
    wait;
  end process;
  clock : process
  begin
    for i in 1 to 6 loop
      clk <= '1';
      wait for 5 ns;
      clk <= '0';
      wait for 5 ns;
    end loop;
    wait;
  end process;
  watch : process (clk)
    variable text : line;
  begin
    if falling_edge(clk) then
      write(text, string'("MIX d=") & to_hstring(d) & string'(" q=")
        & to_hstring(q) & string'(" r=") & to_hstring(r) & string'(" p=")
        & to_hstring(p) & string'(" s=") & to_hstring(s) & string'(" at ")
        & time'image(now));
      writeline(output, text);
    end if;
  end process;
end architecture;
