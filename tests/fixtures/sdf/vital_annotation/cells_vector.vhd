-- SPDX-License-Identifier: Apache-2.0
--
-- Level 0 entities that declare vector delay generics (VitalDelayArrayType01,
-- IEEE 1076.4-2000 9.1) and generics that RETAIN delays fill, and report
-- their values at time zero. They have no function.

library ieee;
use ieee.std_logic_1164.all;
use ieee.vital_timing.all;

entity vvec is
  generic (
    tpd_A_Y : VitalDelayArrayType01 (0 to 3) := ((9 ns, 9 ns), (9 ns, 9 ns), (9 ns, 9 ns), (9 ns, 9 ns));
    tpd_A_Z : VitalDelayArrayType01 (0 to 3) := ((9 ns, 9 ns), (9 ns, 9 ns), (9 ns, 9 ns), (9 ns, 9 ns));
    tpd_DATAIN_DATAOUT : VitalDelayArrayType01 (0 to 3) := ((9 ns, 9 ns), (9 ns, 9 ns), (9 ns, 9 ns), (9 ns, 9 ns));
    tipd_A : VitalDelayArrayType01 (0 to 1) := ((9 ns, 9 ns), (9 ns, 9 ns)));
  port (
    A : in std_logic_vector (0 to 1);
    Y : out std_logic_vector (1 to 2);
    Z : out std_logic_vector (1 to 2);
    DATAIN : in std_logic_vector (0 to 3);
    DATAOUT : out std_logic_vector (1 to 4));
  attribute VITAL_LEVEL0 of vvec : entity is true;
end entity;

architecture vital_level0 of vvec is
begin
  report_generics : process
  begin
    report "TRACE tpd_A_Y(0)(0)=" & time'image(tpd_A_Y(0)(tr01));
    report "TRACE tpd_A_Y(0)(1)=" & time'image(tpd_A_Y(0)(tr10));
    report "TRACE tpd_A_Y(1)(0)=" & time'image(tpd_A_Y(1)(tr01));
    report "TRACE tpd_A_Y(1)(1)=" & time'image(tpd_A_Y(1)(tr10));
    report "TRACE tpd_A_Y(2)(0)=" & time'image(tpd_A_Y(2)(tr01));
    report "TRACE tpd_A_Y(2)(1)=" & time'image(tpd_A_Y(2)(tr10));
    report "TRACE tpd_A_Y(3)(0)=" & time'image(tpd_A_Y(3)(tr01));
    report "TRACE tpd_A_Y(3)(1)=" & time'image(tpd_A_Y(3)(tr10));
    report "TRACE tpd_A_Z(0)(0)=" & time'image(tpd_A_Z(0)(tr01));
    report "TRACE tpd_A_Z(0)(1)=" & time'image(tpd_A_Z(0)(tr10));
    report "TRACE tpd_A_Z(1)(0)=" & time'image(tpd_A_Z(1)(tr01));
    report "TRACE tpd_A_Z(1)(1)=" & time'image(tpd_A_Z(1)(tr10));
    report "TRACE tpd_A_Z(2)(0)=" & time'image(tpd_A_Z(2)(tr01));
    report "TRACE tpd_A_Z(2)(1)=" & time'image(tpd_A_Z(2)(tr10));
    report "TRACE tpd_A_Z(3)(0)=" & time'image(tpd_A_Z(3)(tr01));
    report "TRACE tpd_A_Z(3)(1)=" & time'image(tpd_A_Z(3)(tr10));
    report "TRACE tpd_DATAIN_DATAOUT(0)(0)=" & time'image(tpd_DATAIN_DATAOUT(0)(tr01));
    report "TRACE tpd_DATAIN_DATAOUT(0)(1)=" & time'image(tpd_DATAIN_DATAOUT(0)(tr10));
    report "TRACE tpd_DATAIN_DATAOUT(1)(0)=" & time'image(tpd_DATAIN_DATAOUT(1)(tr01));
    report "TRACE tpd_DATAIN_DATAOUT(1)(1)=" & time'image(tpd_DATAIN_DATAOUT(1)(tr10));
    report "TRACE tpd_DATAIN_DATAOUT(2)(0)=" & time'image(tpd_DATAIN_DATAOUT(2)(tr01));
    report "TRACE tpd_DATAIN_DATAOUT(2)(1)=" & time'image(tpd_DATAIN_DATAOUT(2)(tr10));
    report "TRACE tpd_DATAIN_DATAOUT(3)(0)=" & time'image(tpd_DATAIN_DATAOUT(3)(tr01));
    report "TRACE tpd_DATAIN_DATAOUT(3)(1)=" & time'image(tpd_DATAIN_DATAOUT(3)(tr10));
    report "TRACE tipd_A(0)(0)=" & time'image(tipd_A(0)(tr01));
    report "TRACE tipd_A(0)(1)=" & time'image(tipd_A(0)(tr10));
    report "TRACE tipd_A(1)(0)=" & time'image(tipd_A(1)(tr01));
    report "TRACE tipd_A(1)(1)=" & time'image(tipd_A(1)(tr10));
    wait;
  end process;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.vital_timing.all;

entity vretain is
  generic (
    tpd_r1_y : VitalDelayType01Z := (9 ns, 9 ns, 9 ns, 9 ns, 9 ns, 9 ns);
    tpd_r2_y : VitalDelayType01Z := (9 ns, 9 ns, 9 ns, 9 ns, 9 ns, 9 ns);
    tpd_r3_y : VitalDelayType01ZX := (9 ns, 9 ns, 9 ns, 9 ns, 9 ns, 9 ns, 9 ns, 9 ns, 9 ns, 9 ns, 9 ns, 9 ns);
    tpd_r4_y : VitalDelayType01ZX := (9 ns, 9 ns, 9 ns, 9 ns, 9 ns, 9 ns, 9 ns, 9 ns, 9 ns, 9 ns, 9 ns, 9 ns));
  attribute VITAL_LEVEL0 of vretain : entity is true;
end entity;

architecture vital_level0 of vretain is
begin
  report_generics : process
  begin
    report "TRACE tpd_r1_y(0)=" & time'image(tpd_r1_y(tr01));
    report "TRACE tpd_r1_y(1)=" & time'image(tpd_r1_y(tr10));
    report "TRACE tpd_r1_y(2)=" & time'image(tpd_r1_y(tr0z));
    report "TRACE tpd_r1_y(3)=" & time'image(tpd_r1_y(trz1));
    report "TRACE tpd_r1_y(4)=" & time'image(tpd_r1_y(tr1z));
    report "TRACE tpd_r1_y(5)=" & time'image(tpd_r1_y(trz0));
    report "TRACE tpd_r2_y(0)=" & time'image(tpd_r2_y(tr01));
    report "TRACE tpd_r2_y(1)=" & time'image(tpd_r2_y(tr10));
    report "TRACE tpd_r2_y(2)=" & time'image(tpd_r2_y(tr0z));
    report "TRACE tpd_r2_y(3)=" & time'image(tpd_r2_y(trz1));
    report "TRACE tpd_r2_y(4)=" & time'image(tpd_r2_y(tr1z));
    report "TRACE tpd_r2_y(5)=" & time'image(tpd_r2_y(trz0));
    report "TRACE tpd_r3_y(0)=" & time'image(tpd_r3_y(tr01));
    report "TRACE tpd_r3_y(1)=" & time'image(tpd_r3_y(tr10));
    report "TRACE tpd_r3_y(2)=" & time'image(tpd_r3_y(tr0z));
    report "TRACE tpd_r3_y(3)=" & time'image(tpd_r3_y(trz1));
    report "TRACE tpd_r3_y(4)=" & time'image(tpd_r3_y(tr1z));
    report "TRACE tpd_r3_y(5)=" & time'image(tpd_r3_y(trz0));
    report "TRACE tpd_r3_y(6)=" & time'image(tpd_r3_y(tr0x));
    report "TRACE tpd_r3_y(7)=" & time'image(tpd_r3_y(trx1));
    report "TRACE tpd_r3_y(8)=" & time'image(tpd_r3_y(tr1x));
    report "TRACE tpd_r3_y(9)=" & time'image(tpd_r3_y(trx0));
    report "TRACE tpd_r3_y(10)=" & time'image(tpd_r3_y(trxz));
    report "TRACE tpd_r3_y(11)=" & time'image(tpd_r3_y(trzx));
    report "TRACE tpd_r4_y(0)=" & time'image(tpd_r4_y(tr01));
    report "TRACE tpd_r4_y(1)=" & time'image(tpd_r4_y(tr10));
    report "TRACE tpd_r4_y(2)=" & time'image(tpd_r4_y(tr0z));
    report "TRACE tpd_r4_y(3)=" & time'image(tpd_r4_y(trz1));
    report "TRACE tpd_r4_y(4)=" & time'image(tpd_r4_y(tr1z));
    report "TRACE tpd_r4_y(5)=" & time'image(tpd_r4_y(trz0));
    report "TRACE tpd_r4_y(6)=" & time'image(tpd_r4_y(tr0x));
    report "TRACE tpd_r4_y(7)=" & time'image(tpd_r4_y(trx1));
    report "TRACE tpd_r4_y(8)=" & time'image(tpd_r4_y(tr1x));
    report "TRACE tpd_r4_y(9)=" & time'image(tpd_r4_y(trx0));
    report "TRACE tpd_r4_y(10)=" & time'image(tpd_r4_y(trxz));
    report "TRACE tpd_r4_y(11)=" & time'image(tpd_r4_y(trzx));
    wait;
  end process;
end architecture;
