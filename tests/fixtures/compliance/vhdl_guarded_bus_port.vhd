-- fsim: top=vhdl_guarded_bus_port(sim) std=1993
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2008 6.5.2 and 6.4.2.3: an interface signal port may carry the
-- bus guarded-signal kind after its resolved subtype indication.
package vhdl_guarded_bus_port_pkg is
  type bit_values is array (natural range <>) of bit;
  function any_set (values : bit_values) return bit;
  subtype resolved_bit is any_set bit;
end package;

package body vhdl_guarded_bus_port_pkg is
  function any_set (values : bit_values) return bit is
  begin
    for i in values'range loop
      if values(i) = '1' then
        return '1';
      end if;
    end loop;
    return '0';
  end function;
end package body;

use work.vhdl_guarded_bus_port_pkg.all;
entity vhdl_guarded_bus_port is
  port (value : out resolved_bit bus);
end entity;

architecture sim of vhdl_guarded_bus_port is
begin
  value <= '1';

  process
  begin
    report "PASS";
    wait;
  end process;
end architecture;
