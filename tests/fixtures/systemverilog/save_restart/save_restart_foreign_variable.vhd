-- SPDX-License-Identifier: Apache-2.0
-- A variable a VHPI library forces is forced until it is released, also
-- across `--save-at 50ns` and a fresh-process restart (IEEE 1076-2019 22.5.2,
-- 20.7, 20.8). The force is run state of the simulation, not a callback, so
-- the restart (which keeps only the restart callbacks) finds the variables
-- forced. The plug-in's trace (foreign_vhpi_save_variable_plugin.c):
--
-- saving run: n and v(3) are forced at 22 ns and keep their forced values
--   through the rising edges of clk at 25, 35, 45 and 55 ns; the save is taken
--   at 50 ns; both are released at 60 ns and follow the process again at 65 ns.
-- restart run: the restart callbacks are all that is left of the library, so
--   it reads the variables at the end of the restart (still forced), and
--   releases them 10 ns later, at 60 ns.
entity top is
end entity;

architecture a of top is
  signal clk : bit := '0';
  signal o : integer := 0;
  signal ov : bit_vector(3 downto 0) := "0000";
begin
  process
  begin
    for i in 1 to 20 loop
      wait for 5 ns;
      clk <= not clk;
    end loop;
    wait;
  end process;

  p : process (clk) is
    variable n : integer := 0;
    variable v : bit_vector(3 downto 0) := "0000";
  begin
    if clk = '1' then
      n := n + 1;
      v := v(2 downto 0) & '1';
    end if;
    o <= n;
    ov <= v;
  end process;
end architecture;
