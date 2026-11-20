-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 12.3 b): an explicit "=" with the same profile hides the implicit predefined "=". Expected: success.
package p9 is
  type rec is record
    a : integer;
  end record;
  function "=" (l, r : rec) return boolean;
end package;
package body p9 is
  function "=" (l, r : rec) return boolean is begin return true; end function;
end package body;
use work.p9.all;
entity n09 is
end entity;
architecture a of n09 is
  signal x, y : rec := (a => 1);
  signal b : boolean;
begin
  b <= (x = y);
end architecture;
