-- SPDX-License-Identifier: Apache-2.0
--
-- The compiled objects of one design (each design unit is one object) load,
-- link and take their IEEE packages from one provider in an order decided by
-- what they define, never by checksums of their contents, which carry the
-- paths of the sources and the workspace. So the design compiles,
-- elaborates and simulates the same in every workspace directory. The array
-- types indexed by STD_LOGIC_1164's subtypes are the case whose lengths once
-- depended on which object supplied the IEEE packages (IEEE 1076-2019
-- 5.3.2.2, 9.3.2, 16.7).
library ieee;
use ieee.std_logic_1164.all;
package up is
  type by_x01z is array (X01Z) of integer;
  type by_ux01 is array (UX01) of integer;
  function l1 return integer;
  function l2 return integer;
end package;
package body up is
  function l1 return integer is begin return by_x01z'length; end function;
  function l2 return integer is begin return by_ux01'length; end function;
end package body;
library ieee;
use ieee.std_logic_1164.all;
use work.up.all;
entity top is end entity;
architecture test of top is
begin
  process
  begin
    report "objorder " & integer'image(l1) & " " & integer'image(l2) & " " & integer'image(by_ux01'length);
    wait;
  end process;
end architecture;
