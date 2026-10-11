-- fsim: top=record_defaults_and_resolution(sim) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2008 6.4.2.3: a record signal's STD_LOGIC_VECTOR element
-- starts at 'U'; 4.6: a resolution function takes an unconstrained array
-- of the resolved type; 9.3.5, 6.5.7.1: a port actual qualified by a
-- subtype of the formal's type.
library ieee;
use ieee.std_logic_1164.all;

entity record_defaults_leaf is
  port (value : in bit_vector(3 downto 0); copy : out bit_vector(3 downto 0));
end entity;

architecture rtl of record_defaults_leaf is
begin
  copy <= value;
end architecture;

library ieee;
use ieee.std_logic_1164.all;

entity record_defaults_and_resolution is
end entity;

architecture sim of record_defaults_and_resolution is
  type mode_t is (idle, busy);
  type payload_t is record
    lane : std_logic_vector(3 downto 0);
    mode : mode_t;
  end record;
  type bits is array (natural range <>) of bit;
  function wired_or(values : bits) return bit is
    variable result : bit := '0';
  begin
    for i in values'range loop
      result := result or values(i);
    end loop;
    return result;
  end function;
  subtype or_bit is wired_or bit;
  subtype nibble_t is bit_vector(3 downto 0);
  signal payload : payload_t;
  signal combined : or_bit;
  signal copy : bit_vector(3 downto 0);
begin
  combined <= '1';
  combined <= '0';
  leaf : entity work.record_defaults_leaf
    port map (value => nibble_t'(0 => '1', others => '0'), copy => copy);

  check : process
    variable failures : natural := 0;
  begin
    if payload.lane /= "UUUU" or payload.mode /= idle then
      failures := failures + 1;
    end if;
    wait for 1 ns;
    if combined /= '1' then
      failures := failures + 1;
    end if;
    if copy /= "0001" then
      failures := failures + 1;
    end if;
    if failures = 0 then
      report "PASS";
    else
      report "FAIL count " & integer'image(failures) severity error;
    end if;
    wait;
  end process;
end architecture;
