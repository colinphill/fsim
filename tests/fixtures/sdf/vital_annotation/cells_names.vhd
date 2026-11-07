-- SPDX-License-Identifier: Apache-2.0
--
-- A Level 0 entity that declares the backannotation timing generics every
-- construct of the VITAL SDF map (IEEE 1076.4-2000 5.2.7) can name, and reports
-- their values at time zero. It has no function: the test compares the
-- generics the SDF annotation leaves with the values the map prescribes.

library ieee;
use ieee.std_logic_1164.all;
use ieee.vital_timing.all;

entity vnames is
  generic (
    tpd_a_y : VitalDelayType01Z := (9 ns, 9 ns, 9 ns, 9 ns, 9 ns, 9 ns);
    tpd_b_y : VitalDelayType01ZX := (9 ns, 9 ns, 9 ns, 9 ns, 9 ns, 9 ns, 9 ns, 9 ns, 9 ns, 9 ns, 9 ns, 9 ns);
    tpd_c_y : VitalDelayType01ZX := (9 ns, 9 ns, 9 ns, 9 ns, 9 ns, 9 ns, 9 ns, 9 ns, 9 ns, 9 ns, 9 ns, 9 ns);
    tpd_d_y : VitalDelayType01ZX := (9 ns, 9 ns, 9 ns, 9 ns, 9 ns, 9 ns, 9 ns, 9 ns, 9 ns, 9 ns, 9 ns, 9 ns);
    tpd_e_y : VitalDelayType01Z := (9 ns, 9 ns, 9 ns, 9 ns, 9 ns, 9 ns);
    tpd_f_y : VitalDelayType01Z := (9 ns, 9 ns, 9 ns, 9 ns, 9 ns, 9 ns);
    tpd_a_y_en_EQ_1 : VitalDelayType01 := (9 ns, 9 ns);
    tpd_clk_q_negedge : VitalDelayType01 := (9 ns, 9 ns);
    tpd_g_y_a_EQ_1_AN_b_EQ_0_posedge : VitalDelayType01 := (9 ns, 9 ns);
    tipd_a : VitalDelayType01Z := (9 ns, 9 ns, 9 ns, 9 ns, 9 ns, 9 ns);
    tdevice_n1_z : VitalDelayType := 9 ns;
    tdevice_n1 : VitalDelayType := 9 ns;
    tsetup_d_clk_noedge_posedge : VitalDelayType := 9 ns;
    thold_d_clk_noedge_posedge : VitalDelayType := 9 ns;
    tsetup_d_clk_noedge_negedge : VitalDelayType := 9 ns;
    thold_d_clk_noedge_negedge : VitalDelayType := 9 ns;
    trecovery_rst_clk_noedge_posedge : VitalDelayType := 9 ns;
    tremoval_rst_clk_noedge_posedge : VitalDelayType := 9 ns;
    trecovery_rst_clk_noedge_negedge : VitalDelayType := 9 ns;
    tremoval_rst_clk_noedge_negedge : VitalDelayType := 9 ns;
    tskew_a_b_noedge : VitalDelayType := 9 ns;
    tskew_a_b_posedge_posedge : VitalDelayType := 9 ns;
    tskew_b_a_posedge_posedge : VitalDelayType := 9 ns;
    tncsetup_a_b_noedge_posedge : VitalDelayType := 9 ns;
    tnchold_a_b_noedge_posedge : VitalDelayType := 9 ns;
    tpw_clk_negedge : VitalDelayType := 9 ns;
    tperiod_clk_posedge : VitalDelayType := 9 ns;
    tpw_clk : VitalDelayType := 9 ns;
    tsetup_data_clk_Reset_EQ_1_noedge_posedge : VitalDelayType := 9 ns;
    thold_data_clk_noedge_mode_NE_0_posedge : VitalDelayType := 9 ns;
    InstancePath : string := "vnames");
  attribute VITAL_LEVEL0 of vnames : entity is true;
end entity;

architecture vital_level0 of vnames is
begin
  report_generics : process
  begin
    report "TRACE tpd_a_y(0)=" & time'image(tpd_a_y(tr01));
    report "TRACE tpd_a_y(1)=" & time'image(tpd_a_y(tr10));
    report "TRACE tpd_a_y(2)=" & time'image(tpd_a_y(tr0z));
    report "TRACE tpd_a_y(3)=" & time'image(tpd_a_y(trz1));
    report "TRACE tpd_a_y(4)=" & time'image(tpd_a_y(tr1z));
    report "TRACE tpd_a_y(5)=" & time'image(tpd_a_y(trz0));
    report "TRACE tpd_b_y(0)=" & time'image(tpd_b_y(tr01));
    report "TRACE tpd_b_y(1)=" & time'image(tpd_b_y(tr10));
    report "TRACE tpd_b_y(2)=" & time'image(tpd_b_y(tr0z));
    report "TRACE tpd_b_y(3)=" & time'image(tpd_b_y(trz1));
    report "TRACE tpd_b_y(4)=" & time'image(tpd_b_y(tr1z));
    report "TRACE tpd_b_y(5)=" & time'image(tpd_b_y(trz0));
    report "TRACE tpd_b_y(6)=" & time'image(tpd_b_y(tr0x));
    report "TRACE tpd_b_y(7)=" & time'image(tpd_b_y(trx1));
    report "TRACE tpd_b_y(8)=" & time'image(tpd_b_y(tr1x));
    report "TRACE tpd_b_y(9)=" & time'image(tpd_b_y(trx0));
    report "TRACE tpd_b_y(10)=" & time'image(tpd_b_y(trxz));
    report "TRACE tpd_b_y(11)=" & time'image(tpd_b_y(trzx));
    report "TRACE tpd_c_y(0)=" & time'image(tpd_c_y(tr01));
    report "TRACE tpd_c_y(1)=" & time'image(tpd_c_y(tr10));
    report "TRACE tpd_c_y(2)=" & time'image(tpd_c_y(tr0z));
    report "TRACE tpd_c_y(3)=" & time'image(tpd_c_y(trz1));
    report "TRACE tpd_c_y(4)=" & time'image(tpd_c_y(tr1z));
    report "TRACE tpd_c_y(5)=" & time'image(tpd_c_y(trz0));
    report "TRACE tpd_c_y(6)=" & time'image(tpd_c_y(tr0x));
    report "TRACE tpd_c_y(7)=" & time'image(tpd_c_y(trx1));
    report "TRACE tpd_c_y(8)=" & time'image(tpd_c_y(tr1x));
    report "TRACE tpd_c_y(9)=" & time'image(tpd_c_y(trx0));
    report "TRACE tpd_c_y(10)=" & time'image(tpd_c_y(trxz));
    report "TRACE tpd_c_y(11)=" & time'image(tpd_c_y(trzx));
    report "TRACE tpd_d_y(0)=" & time'image(tpd_d_y(tr01));
    report "TRACE tpd_d_y(1)=" & time'image(tpd_d_y(tr10));
    report "TRACE tpd_d_y(2)=" & time'image(tpd_d_y(tr0z));
    report "TRACE tpd_d_y(3)=" & time'image(tpd_d_y(trz1));
    report "TRACE tpd_d_y(4)=" & time'image(tpd_d_y(tr1z));
    report "TRACE tpd_d_y(5)=" & time'image(tpd_d_y(trz0));
    report "TRACE tpd_d_y(6)=" & time'image(tpd_d_y(tr0x));
    report "TRACE tpd_d_y(7)=" & time'image(tpd_d_y(trx1));
    report "TRACE tpd_d_y(8)=" & time'image(tpd_d_y(tr1x));
    report "TRACE tpd_d_y(9)=" & time'image(tpd_d_y(trx0));
    report "TRACE tpd_d_y(10)=" & time'image(tpd_d_y(trxz));
    report "TRACE tpd_d_y(11)=" & time'image(tpd_d_y(trzx));
    report "TRACE tpd_e_y(0)=" & time'image(tpd_e_y(tr01));
    report "TRACE tpd_e_y(1)=" & time'image(tpd_e_y(tr10));
    report "TRACE tpd_e_y(2)=" & time'image(tpd_e_y(tr0z));
    report "TRACE tpd_e_y(3)=" & time'image(tpd_e_y(trz1));
    report "TRACE tpd_e_y(4)=" & time'image(tpd_e_y(tr1z));
    report "TRACE tpd_e_y(5)=" & time'image(tpd_e_y(trz0));
    report "TRACE tpd_f_y(0)=" & time'image(tpd_f_y(tr01));
    report "TRACE tpd_f_y(1)=" & time'image(tpd_f_y(tr10));
    report "TRACE tpd_f_y(2)=" & time'image(tpd_f_y(tr0z));
    report "TRACE tpd_f_y(3)=" & time'image(tpd_f_y(trz1));
    report "TRACE tpd_f_y(4)=" & time'image(tpd_f_y(tr1z));
    report "TRACE tpd_f_y(5)=" & time'image(tpd_f_y(trz0));
    report "TRACE tpd_a_y_en_EQ_1(0)=" & time'image(tpd_a_y_en_EQ_1(tr01));
    report "TRACE tpd_a_y_en_EQ_1(1)=" & time'image(tpd_a_y_en_EQ_1(tr10));
    report "TRACE tpd_clk_q_negedge(0)=" & time'image(tpd_clk_q_negedge(tr01));
    report "TRACE tpd_clk_q_negedge(1)=" & time'image(tpd_clk_q_negedge(tr10));
    report "TRACE tpd_g_y_a_EQ_1_AN_b_EQ_0_posedge(0)=" & time'image(tpd_g_y_a_EQ_1_AN_b_EQ_0_posedge(tr01));
    report "TRACE tpd_g_y_a_EQ_1_AN_b_EQ_0_posedge(1)=" & time'image(tpd_g_y_a_EQ_1_AN_b_EQ_0_posedge(tr10));
    report "TRACE tipd_a(0)=" & time'image(tipd_a(tr01));
    report "TRACE tipd_a(1)=" & time'image(tipd_a(tr10));
    report "TRACE tipd_a(2)=" & time'image(tipd_a(tr0z));
    report "TRACE tipd_a(3)=" & time'image(tipd_a(trz1));
    report "TRACE tipd_a(4)=" & time'image(tipd_a(tr1z));
    report "TRACE tipd_a(5)=" & time'image(tipd_a(trz0));
    report "TRACE tdevice_n1_z=" & time'image(tdevice_n1_z);
    report "TRACE tdevice_n1=" & time'image(tdevice_n1);
    report "TRACE tsetup_d_clk_noedge_posedge=" & time'image(tsetup_d_clk_noedge_posedge);
    report "TRACE thold_d_clk_noedge_posedge=" & time'image(thold_d_clk_noedge_posedge);
    report "TRACE tsetup_d_clk_noedge_negedge=" & time'image(tsetup_d_clk_noedge_negedge);
    report "TRACE thold_d_clk_noedge_negedge=" & time'image(thold_d_clk_noedge_negedge);
    report "TRACE trecovery_rst_clk_noedge_posedge=" & time'image(trecovery_rst_clk_noedge_posedge);
    report "TRACE tremoval_rst_clk_noedge_posedge=" & time'image(tremoval_rst_clk_noedge_posedge);
    report "TRACE trecovery_rst_clk_noedge_negedge=" & time'image(trecovery_rst_clk_noedge_negedge);
    report "TRACE tremoval_rst_clk_noedge_negedge=" & time'image(tremoval_rst_clk_noedge_negedge);
    report "TRACE tskew_a_b_noedge=" & time'image(tskew_a_b_noedge);
    report "TRACE tskew_a_b_posedge_posedge=" & time'image(tskew_a_b_posedge_posedge);
    report "TRACE tskew_b_a_posedge_posedge=" & time'image(tskew_b_a_posedge_posedge);
    report "TRACE tncsetup_a_b_noedge_posedge=" & time'image(tncsetup_a_b_noedge_posedge);
    report "TRACE tnchold_a_b_noedge_posedge=" & time'image(tnchold_a_b_noedge_posedge);
    report "TRACE tpw_clk_negedge=" & time'image(tpw_clk_negedge);
    report "TRACE tperiod_clk_posedge=" & time'image(tperiod_clk_posedge);
    report "TRACE tpw_clk=" & time'image(tpw_clk);
    report "TRACE tsetup_data_clk_Reset_EQ_1_noedge_posedge=" & time'image(tsetup_data_clk_Reset_EQ_1_noedge_posedge);
    report "TRACE thold_data_clk_noedge_mode_NE_0_posedge=" & time'image(thold_data_clk_noedge_mode_NE_0_posedge);
    wait;
  end process;
end architecture;
