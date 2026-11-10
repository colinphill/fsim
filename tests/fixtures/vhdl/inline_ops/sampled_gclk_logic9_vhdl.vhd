-- SPDX-License-Identifier: Apache-2.0
--
-- The VHDL side of sampled_gclk_logic9.sv: a top level that drives the
-- SystemVerilog module svg's clock clk ('1' at 10 k ns, '0' at 10 k + 5 ns,
-- k = 1 .. 8) and, in the delta of each rising edge, the std_logic sb and
-- element 65 of the 70 element vector v to the k-th value of the table
-- "0LH1ZZ1L" (the other elements of v '0' from 10 ns).
library ieee;
use ieee.std_logic_1164.all;

entity vtop is
end entity;

architecture a of vtop is
  component svg
    port (clk : in std_logic; sb : in std_logic; v : in std_logic_vector(69 downto 0));
  end component;
  signal clk : std_logic := '0';
  signal sb : std_logic;
  signal v : std_logic_vector(69 downto 0);
  constant codes : string(1 to 8) := "0LH1ZZ1L";
begin
  u : svg port map (clk => clk, sb => sb, v => v);
  drive : process
    variable value : std_logic;
  begin
    wait for 10 ns;
    for k in codes'range loop
      case codes(k) is
        when '0' => value := '0';
        when '1' => value := '1';
        when 'L' => value := 'L';
        when 'H' => value := 'H';
        when others => value := 'Z';
      end case;
      if k = 1 then
        v <= (others => '0');
      end if;
      clk <= '1';
      sb <= value;
      v(65) <= value;
      wait for 5 ns;
      clk <= '0';
      wait for 5 ns;
    end loop;
    wait;
  end process;
end architecture;
