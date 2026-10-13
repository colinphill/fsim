-- fsim-expect: FSIM-VHDL-SEM-129 std=1993
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2008 11.6: a guarded signal is the target only of a guarded
-- concurrent signal assignment.
entity unguarded_guarded_target is
end entity;

architecture a of unguarded_guarded_target is
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
  signal shared_line : any_one bit bus;
  signal source : bit;
begin
  shared_line <= source;
end architecture;
