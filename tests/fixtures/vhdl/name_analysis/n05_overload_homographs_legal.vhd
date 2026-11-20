-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 12.4: use-visible overloadable declarations with different profiles are all visible.
-- Expected: success (both calls resolve).
package pf1 is
  function F(x : integer) return integer;
end package;
package body pf1 is
  function F(x : integer) return integer is begin return x; end function;
end package body;
package pf2 is
  function F(x : boolean) return integer;
end package;
package body pf2 is
  function F(x : boolean) return integer is begin return 1; end function;
end package body;
use work.pf1.all;
use work.pf2.all;
entity n05 is
end entity;
architecture a of n05 is
  constant a1 : integer := F(3);
  constant a2 : integer := F(true);
begin
end architecture;
