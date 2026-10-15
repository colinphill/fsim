-- SPDX-License-Identifier: Apache-2.0
-- Direct local and package declarations named WORK remain legal, a
-- package-qualified WORK type remains reachable from a separate unit where
-- the logical WORK library prefix is not shadowed, and overloaded function
-- calls continue through normal callable resolution.
package enum_type_pkg is
  type mc is (low, high);
end package;

package visibility_pkg is
  type work is (package_first, package_second);
  constant package_value : work := package_first;
end package;

package overload_pkg is
  function pick return boolean;
  function pick(value : boolean) return boolean;
  function mc return boolean;
end package;

package body overload_pkg is
  function pick return boolean is
  begin
    return false;
  end function;
  function pick(value : boolean) return boolean is
  begin
    return value;
  end function;
  function mc return boolean is
  begin
    return false;
  end function;
end package body;

use work.enum_type_pkg.all;
use work.visibility_pkg.all;
use work.overload_pkg.all;
entity legal_direct_qualified_and_overloaded_batch59 is
end entity;

architecture test of legal_direct_qualified_and_overloaded_batch59 is
  type work is (local_first, local_second);
  type mc is (local_mc);
begin
  check : process
    variable direct_work : work;
    variable direct_mc : mc;
    variable flag : boolean;
  begin
    flag := pick;
    flag := pick(flag);
    wait;
  end process;
end architecture;

-- This separate design unit has no local declaration named WORK, so the
-- selected name's first WORK denotes the implicit logical library.
use work.visibility_pkg.all;
entity qualified_package_work_control_batch59 is
end entity;

use work.visibility_pkg.all;
architecture qualified of qualified_package_work_control_batch59 is
begin
  check : process
    variable qualified_package_work : work.visibility_pkg.work;
  begin
    wait;
  end process;
end architecture;
