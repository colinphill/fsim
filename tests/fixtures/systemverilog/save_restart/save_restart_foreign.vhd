-- SPDX-License-Identifier: Apache-2.0
-- A foreign VHPI library across `--save-at 50ns` and a fresh-process restart
-- (IEEE 1076-2019 20.7, 20.8, 23.12, 23.27). clk toggles every 5 ns twenty
-- times, so the run ends at 100 ns (100000000 fs); the save is taken at the
-- end of the simulation cycle of 50 ns. The plug-in's trace
-- (foreign_vhpi_save_session_plugin.c):
--
-- saving run: vhpiCbStartOfSimulation, vhpiCbStartOfSave (id 1, two puts of
--   four bytes, a location), vhpiCbEndOfSave, vhpiCbEndOfSimulation at 100 ns.
-- restart run: no vhpiCbStartOfSimulation (20.8 NOTE 2); the saved
--   vhpiCbStartOfRestart / vhpiCbEndOfRestart registrations come back (20.7),
--   the first reads 7,9; vhpi_get(vhpiPhaseP) in vhpiCbEndOfRestart is
--   vhpiRestartPhase (8: the simulation phase follows the callbacks); the
--   startup routine's end-of-simulation callback is gone (only the restart
--   callbacks survive) and the one registered in vhpiCbEndOfRestart reports
--   100 ns and "(after restart)".
entity top is
end entity;

architecture a of top is
  signal clk : bit := '0';
begin
  process
  begin
    for i in 1 to 20 loop
      wait for 5 ns;
      clk <= not clk;
    end loop;
    wait;
  end process;
end architecture;
