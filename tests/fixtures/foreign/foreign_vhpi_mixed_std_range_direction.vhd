-- SPDX-License-Identifier: Apache-2.0
-- Compiled as VHDL-2008, which has no predefined RANGE_DIRECTION (it is new
-- in IEEE 1076-2019 5.2.5): compiling it is refused whatever other units a
-- design later mixes with it.
entity top is
end entity;

architecture a of top is
  signal d : range_direction;
begin
end architecture;
