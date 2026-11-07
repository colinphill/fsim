-- SPDX-License-Identifier: Apache-2.0
--
-- A VITAL Level 0 entity with a Level 1 architecture (IEEE 1076.4-2000): the
-- backannotation timing generic tpd_a_z is the propagation delay from a to z,
-- and an SDF `IOPATH A Z` entry sets it when the design is elaborated.
-- vital_cell is the VHDL cell that the SystemVerilog boundary instantiates; it
-- holds the VITAL model vital_core, whose output is a standard-logic signal
-- of its own (a VITAL path delay drives a std_logic signal).

library ieee;
use ieee.std_logic_1164.all;
use ieee.vital_timing.all;
use ieee.vital_primitives.all;

entity vital_core is
  generic (
    tpd_a_z : VitalDelayType01 := (1 ns, 1 ns);
    InstancePath : string := "vital_core";
    TimingChecksOn : boolean := true;
    XOn : boolean := true;
    MsgOn : boolean := true);
  port (a : in std_logic; z : out std_logic);
  attribute VITAL_LEVEL0 of vital_core : entity is true;
end entity;

architecture timing of vital_core is
  attribute VITAL_LEVEL1 of timing : architecture is true;
begin
  VitalBehavior : process (a)
    variable z_zd : std_logic;
    variable z_glitch : VitalGlitchDataType;
  begin
    z_zd := a;
    VitalPathDelay01(
      OutSignal => z, OutSignalName => "z", OutTemp => z_zd,
      Paths => (0 => (InputChangeTime => a'last_event,
                      PathDelay => tpd_a_z, PathCondition => true)),
      GlitchData => z_glitch);
  end process;
end architecture;

library ieee;
use ieee.std_logic_1164.all;

entity vital_cell is
  port (a : in std_logic; z : out std_logic);
end entity;

architecture wrapper of vital_cell is
  component vital_core is
    port (a : in std_logic; z : out std_logic);
  end component;
  signal z_core : std_logic;
begin
  u_core : vital_core port map (a => a, z => z_core);
  z <= z_core;
end architecture;
