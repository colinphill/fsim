-- SPDX-License-Identifier: Apache-2.0
--
-- A VITAL bus input: the interconnect delay generic tipd_a is a
-- VitalDelayArrayType01, one (rise, fall) pair for each element of the bus
-- port a, and a generate statement delays each element of a to the wire
-- delayed signal a_ipd with its own VitalWireDelay call (IEEE 1076.4-2000
-- 6.4.1). The output y follows a_ipd without delay.

library ieee;
use ieee.std_logic_1164.all;
use ieee.vital_timing.all;
use ieee.vital_primitives.all;

entity vbus is
  generic (
    tipd_a : VitalDelayArrayType01 (0 to 3) := ((500 ps, 700 ps), (500 ps, 700 ps), (500 ps, 700 ps), (500 ps, 700 ps));
    InstancePath : string := "vbus";
    TimingChecksOn : boolean := true;
    XOn : boolean := true;
    MsgOn : boolean := true);
  port (a : in std_logic_vector (0 to 3); y : out std_logic_vector (0 to 3));
  attribute VITAL_LEVEL0 of vbus : entity is true;
end entity;

architecture vital_level1 of vbus is
  signal a_ipd : std_logic_vector (0 to 3) := (others => 'U');
begin
  WireDelay : block
  begin
    g : for i in a'range generate
      w : VitalWireDelay(a_ipd(i), a(i), tipd_a(i));
    end generate;
  end block;

  y <= a_ipd;
end architecture;
