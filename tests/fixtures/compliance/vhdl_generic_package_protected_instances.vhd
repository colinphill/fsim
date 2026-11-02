-- fsim: top=generic_package_protected_instances(sim) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2008 4.9 and 5.6.2: each instance of a generic package has its
-- own protected shared variable, which the instance's subprograms reach by
-- its simple name, also through a subprogram of the same package.
package counter_pkg is
  generic (start : integer);
  type counter_t is protected
    procedure set(x : integer);
    procedure inc;
    impure function get return integer;
  end protected;
  shared variable counter : counter_t;
  procedure setup;
  procedure bump;
  procedure bump_twice;
  impure function read return integer;
end package;

package body counter_pkg is
  type counter_t is protected body
    variable value : integer := 0;
    procedure set(x : integer) is
    begin
      value := x;
    end procedure;
    procedure inc is
    begin
      value := value + 1;
    end procedure;
    impure function get return integer is
    begin
      return value;
    end function;
  end protected body;

  procedure setup is
  begin
    counter.set(start);
  end procedure;

  procedure bump is
  begin
    counter.inc;
  end procedure;

  procedure bump_twice is
  begin
    bump;
    bump;
  end procedure;

  impure function read return integer is
  begin
    return counter.get;
  end function;
end package body;

entity generic_package_protected_instances is
end entity;

architecture sim of generic_package_protected_instances is
  package first is new work.counter_pkg generic map (start => 10);
  package second is new work.counter_pkg generic map (start => 20);
begin
  process
  begin
    first.setup;
    second.setup;
    first.bump_twice;
    first.bump;
    second.bump;
    if first.counter.get = 13 and second.counter.get = 21
      and first.read = 13 and second.read = 21 then
      report "PASS";
    else
      report "FAIL first=" & integer'image(first.counter.get)
        & "/" & integer'image(first.read)
        & " second=" & integer'image(second.counter.get)
        & "/" & integer'image(second.read) severity error;
    end if;
    wait;
  end process;
end architecture;
