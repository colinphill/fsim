-- SPDX-License-Identifier: Apache-2.0
--
-- Independently written VITAL cells for the SDF backannotation tests
-- (IEEE 1076.4-2000 clauses 4 and 5.2). Every cell is a Level 0 entity with
-- the backannotation timing generics of 4.3.2.1.3 and a Level 1 architecture.

library ieee;
use ieee.std_logic_1164.all;
use ieee.vital_timing.all;
use ieee.vital_primitives.all;

-- A buffer: interconnect delay tipd_a on its input and the propagation delay
-- tpd_a_y from a to y. The defaults are (500 ps, 700 ps) and (1 ns, 2 ns).
entity vbuf is
  generic (
    tipd_a : VitalDelayType01 := (500 ps, 700 ps);
    tpd_a_y : VitalDelayType01 := (1 ns, 2 ns);
    InstancePath : string := "vbuf";
    TimingChecksOn : boolean := true;
    XOn : boolean := true;
    MsgOn : boolean := true);
  port (a : in std_logic; y : out std_logic);
  attribute VITAL_LEVEL0 of vbuf : entity is true;
end entity;

architecture vital_level1 of vbuf is
  attribute VITAL_LEVEL1 of vital_level1 : architecture is true;
  signal a_ipd : std_logic := 'U';
begin
  WireDelay : block
  begin
    w_1 : VitalWireDelay(a_ipd, a, tipd_a);
  end block;

  VitalBehavior : process (a_ipd)
    variable y_zd : std_logic;
    variable y_glitch : VitalGlitchDataType;
  begin
    y_zd := a_ipd;
    VitalPathDelay01(
      OutSignal => y, OutSignalName => "y", OutTemp => y_zd,
      Paths => (0 => (InputChangeTime => a_ipd'last_event,
                      PathDelay => tpd_a_y, PathCondition => true)),
      GlitchData => y_glitch);
  end process;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.vital_timing.all;
use ieee.vital_primitives.all;

-- A rising-edge flip-flop with the timing checks of Level 0: setup and hold
-- of d to the rising edge of clk, the pulse width and the period of clk.
entity vff is
  generic (
    tipd_d : VitalDelayType01 := (0 ns, 0 ns);
    tipd_clk : VitalDelayType01 := (0 ns, 0 ns);
    tpd_clk_q_posedge : VitalDelayType01 := (1 ns, 1 ns);
    tsetup_d_clk_noedge_posedge : VitalDelayType := 1 ns;
    thold_d_clk_noedge_posedge : VitalDelayType := 1 ns;
    tpw_clk_posedge : VitalDelayType := 1 ns;
    tperiod_clk_posedge : VitalDelayType := 1 ns;
    InstancePath : string := "vff";
    TimingChecksOn : boolean := true;
    XOn : boolean := true;
    MsgOn : boolean := true);
  port (d : in std_logic; clk : in std_logic; q : out std_logic);
  attribute VITAL_LEVEL0 of vff : entity is true;
end entity;

architecture vital_level1 of vff is
  attribute VITAL_LEVEL1 of vital_level1 : architecture is true;
  signal d_ipd : std_logic := 'U';
  signal clk_ipd : std_logic := 'U';
begin
  WireDelay : block
  begin
    w_1 : VitalWireDelay(d_ipd, d, tipd_d);
    w_2 : VitalWireDelay(clk_ipd, clk, tipd_clk);
  end block;

  VitalBehavior : process (d_ipd, clk_ipd)
    variable tviol_d_clk : std_ulogic := '0';
    variable timing_d_clk : VitalTimingDataType := VitalTimingDataInit;
    variable pviol_clk : std_ulogic := '0';
    variable period_clk : VitalPeriodDataType := VitalPeriodDataInit;
    variable q_zd : std_logic := 'U';
    variable q_glitch : VitalGlitchDataType;
  begin
    if TimingChecksOn then
      VitalSetupHoldCheck(
        Violation => tviol_d_clk, TimingData => timing_d_clk,
        TestSignal => d_ipd, TestSignalName => "d", TestDelay => 0 ns,
        RefSignal => clk_ipd, RefSignalName => "clk", RefDelay => 0 ns,
        SetupHigh => tsetup_d_clk_noedge_posedge,
        SetupLow => tsetup_d_clk_noedge_posedge,
        HoldHigh => thold_d_clk_noedge_posedge,
        HoldLow => thold_d_clk_noedge_posedge,
        CheckEnabled => true, RefTransition => '/',
        HeaderMsg => "vff: ", XOn => XOn, MsgOn => MsgOn,
        MsgSeverity => warning);
      VitalPeriodPulseCheck(
        Violation => pviol_clk, PeriodData => period_clk,
        TestSignal => clk_ipd, TestSignalName => "clk", TestDelay => 0 ns,
        Period => tperiod_clk_posedge, PulseWidthHigh => tpw_clk_posedge,
        PulseWidthLow => 0 ns, CheckEnabled => true,
        HeaderMsg => "vff: ", XOn => XOn, MsgOn => MsgOn,
        MsgSeverity => warning);
      if tviol_d_clk = 'X' then
        report "TRACE setup/hold violation @" & time'image(now);
      end if;
      if pviol_clk = 'X' then
        report "TRACE pulse violation @" & time'image(now);
      end if;
    end if;
    if clk_ipd'event and clk_ipd = '1' and clk_ipd'last_value = '0' then
      q_zd := d_ipd;
    end if;
    VitalPathDelay01(
      OutSignal => q, OutSignalName => "q", OutTemp => q_zd,
      Paths => (0 => (InputChangeTime => clk_ipd'last_event,
                      PathDelay => tpd_clk_q_posedge, PathCondition => true)),
      GlitchData => q_glitch);
  end process;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.vital_timing.all;
use ieee.vital_primitives.all;

-- A delay element for the instance labelled p1: its device delay generic
-- tdevice_p1_z is the delay from a to z (5.2.7.1.6).
entity vdev is
  generic (
    tdevice_p1_z : VitalDelayType := 1 ns;
    InstancePath : string := "vdev";
    TimingChecksOn : boolean := true;
    XOn : boolean := true;
    MsgOn : boolean := true);
  port (a : in std_logic; z : out std_logic);
  attribute VITAL_LEVEL0 of vdev : entity is true;
end entity;

architecture vital_level1 of vdev is
  attribute VITAL_LEVEL1 of vital_level1 : architecture is true;
begin
  VitalBehavior : process (a)
    variable z_zd : std_logic;
    variable z_glitch : VitalGlitchDataType;
  begin
    z_zd := a;
    VitalPathDelay(
      OutSignal => z, OutSignalName => "z", OutTemp => z_zd,
      Paths => (0 => (InputChangeTime => a'last_event,
                      PathDelay => tdevice_p1_z, PathCondition => true)),
      GlitchData => z_glitch);
  end process;
end architecture;
