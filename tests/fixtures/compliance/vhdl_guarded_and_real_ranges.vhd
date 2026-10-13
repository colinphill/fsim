-- fsim: top=vhdl_guarded_and_real_ranges(a) std=1993
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2008 6.4.2.3 and 11.6: guarded bus and register signals driven
-- by guarded concurrent assignments; a register signal whose drivers are
-- all disconnected keeps its value; 5.2.5: assignments within a REAL
-- subtype's range; 4.8: a deferred constant named after its full
-- declaration.
package vhdl_guarded_and_real_ranges_pkg is
  constant scale : integer;
  constant offset : integer;
end package;

package body vhdl_guarded_and_real_ranges_pkg is
  constant scale : integer := 3;
  constant offset : integer := scale + 1;
end package body;

use work.vhdl_guarded_and_real_ranges_pkg.all;
entity vhdl_guarded_and_real_ranges is
end entity;

architecture a of vhdl_guarded_and_real_ranges is
  type bits is array (natural range <>) of bit;
  function any_one (drivers : bits) return bit is
  begin
    for i in drivers'range loop
      if drivers(i) = '1' then
        return '1';
      end if;
    end loop;
    return '0';
  end function;
  subtype unit_interval is real range 0.0 to 1.0;
  signal enable : boolean := true;
  signal source : bit := '1';
  signal shared_line : any_one bit bus;
  signal held : any_one bit register;
begin
  driver : block (enable)
  begin
    shared_line <= guarded source;
    held <= guarded source;
  end block;

  process
    variable fraction : unit_interval := 0.25;
  begin
    wait for 1 ns;
    assert shared_line = '1' and held = '1' report "connected" severity error;
    enable <= false;
    wait for 1 ns;
    assert held = '1' report "register keeps its value" severity error;
    fraction := fraction * 4.0;
    assert fraction = 1.0 report "upper bound" severity error;
    fraction := fraction - 1.0;
    assert fraction = 0.0 report "lower bound" severity error;
    assert scale = 3 and offset = 4 report "deferred constants" severity error;
    report "PASS";
    wait;
  end process;
end architecture;
